/* persist.c — EigenOS persistent root.
 *
 * Serialises the entire files[] ramfs table (nodes + payloads) to a raw
 * disk region through the IDE block layer, alternating between two CRC-
 * protected slots so a torn write never destroys the last good snapshot.
 * Restored verbatim at boot: node indices are preserved exactly, which
 * keeps every parent_dir reference valid.
 *
 * On-disk layout (LBA 4096 onward, 32 MB per slot):
 *   slot header : 1 sector   (magic, version, seq, counts, crc32)
 *   records     : MAX_FILES * sizeof(pk_rec_t)   (index == slot index)
 *   payloads    : file bodies concatenated in slot order
 */

#include "filesystem/persist.h"
#include "filesystem/filesystem.h"
#include "drivers/storage/storage.h"
#include "kernel/time/timer.h"
#include <stdint.h>
#include <string.h>

extern void serial_puts(const char* s);
extern void serial_u64(uint64_t v);
void* kmalloc(unsigned long sz);
void kfree(void* p);
void sleep_task(uint32_t ms);

#define PERSIST_MAGIC      "EGNDSK02"  /* v2: new tree layout; v1 snapshots rejected */
#define PERSIST_VERSION    1
#define PERSIST_BASE_LBA   4096ULL                      /* 2 MiB in        */
#define PERSIST_SLOT_SECT  ((32ULL * 1024 * 1024) / 512)
#define SECTOR             512

static block_device_t* dev;
static uint32_t seq;
static int dirty;

void persist_mark_dirty(void) { dirty = 1; }
int  persist_dirty(void)       { return dirty; }

static uint32_t crc32_buf(const uint8_t* p, uint64_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (uint64_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1)));
    }
    return ~c;
}

typedef struct __attribute__((packed)) {
    char     magic[8];
    uint32_t version;
    uint32_t seq;
    uint32_t node_count;
    uint32_t rec_bytes;
    uint64_t payload_bytes;
    uint32_t crc;          /* over records + payloads */
    uint32_t reserved;
} pk_hdr_t;

typedef struct __attribute__((packed)) {
    uint8_t  used;
    char     name[64];
    int32_t  parent;
    uint8_t  type;
    uint32_t flags;
    uint32_t size;
    uint32_t ctime;
    uint32_t mtime;
} pk_rec_t;

#define REC_SIZE   ((uint64_t)sizeof(pk_rec_t))
#define REC_REGION ((uint64_t)MAX_FILES * REC_SIZE)

static uint8_t* snap;
static uint64_t snap_len;
static uint32_t snap_nodes;

static int build_snapshot(void) {
    uint64_t payload = 0;
    for (int i = 0; i < MAX_FILES; i++)
        if (fs_table_entry(i)->in_use && fs_table_entry(i)->type == FS_FILE &&
            fs_table_entry(i)->data && fs_table_entry(i)->size > 0 &&
            fs_table_entry(i)->capacity > 0)   /* skip external aliases */
            payload += (uint64_t)fs_table_entry(i)->size;

    uint64_t total = REC_REGION + payload;
    uint8_t* buf = (uint8_t*)kmalloc((size_t)(total ? total : 1));
    if (!buf) return 0;

    memset(buf, 0, (size_t)total);
    uint64_t off = 0, pay = REC_REGION;
    uint32_t nodes = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        pk_rec_t r;
        memset(&r, 0, sizeof(r));
        file_t* fp = fs_table_entry(i);
        if (fp->in_use) {
            r.used  = 1;
            strncpy(r.name, fp->name, sizeof(r.name) - 1);
            r.parent = fp->parent_dir;
            r.type   = (uint8_t)fp->type;
            r.flags  = fp->flags;
            r.size   = (uint32_t)fp->size;
            r.ctime  = fp->creation_time;
            r.mtime  = fp->modified_time;
            nodes++;
            if (fp->type == FS_FILE && fp->data && fp->size > 0 &&
                fp->capacity > 0) {
                memcpy(buf + pay, fp->data, (size_t)fp->size);
                pay += (uint64_t)fp->size;
            }
        }
        memcpy(buf + off, &r, sizeof(r));
        off += REC_SIZE;
    }

    snap = buf; snap_len = total; snap_nodes = nodes;
    return 1;
}

static int write_slot(uint64_t base_lba) {
    static uint8_t sec[SECTOR];
    uint64_t need_sect = (snap_len + SECTOR - 1) / SECTOR;
    if (need_sect + 1 > PERSIST_SLOT_SECT) return 0;

    uint64_t left = snap_len, off = 0, lba = base_lba + 1;
    while (left > 0) {
        memset(sec, 0, SECTOR);
        uint32_t chunk = left > SECTOR ? SECTOR : (uint32_t)left;
        memcpy(sec, snap + off, chunk);
        if (dev->write(dev, lba, 1, sec) != 0) return 0;
        off += chunk; left -= chunk; lba++;
    }

    pk_hdr_t h;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, PERSIST_MAGIC, 8);
    h.version = PERSIST_VERSION;
    h.seq = seq;
    h.node_count = snap_nodes;
    h.rec_bytes = (uint32_t)REC_REGION;
    h.payload_bytes = snap_len - REC_REGION;
    h.crc = crc32_buf(snap, snap_len);

    memset(sec, 0, SECTOR);
    memcpy(sec, &h, sizeof(h));
    return dev->write(dev, base_lba, 1, sec) == 0;
}

static int read_sector_into(uint64_t lba, void* out) {
    return dev->read(dev, lba, 1, out) == 0;
}

static uint8_t* load_slot(uint64_t base_lba) {
    static uint8_t sec[SECTOR];
    if (!read_sector_into(base_lba, sec)) return 0;
    pk_hdr_t h;
    memset(&h, 0, sizeof(h));
    memcpy(&h, sec, sizeof(h));
    if (memcmp(h.magic, PERSIST_MAGIC, 8) != 0) return 0;
    if (h.version != PERSIST_VERSION) return 0;
    if (h.rec_bytes != REC_REGION) return 0;
    uint64_t total = (uint64_t)h.rec_bytes + h.payload_bytes;
    if (total == 0 || total > PERSIST_SLOT_SECT * SECTOR - SECTOR) return 0;

    uint8_t* buf = (uint8_t*)kmalloc((size_t)total);
    if (!buf) return 0;

    uint64_t left = total, off = 0, lba = base_lba + 1;
    while (left > 0) {
        uint32_t chunk = left > SECTOR ? SECTOR : (uint32_t)left;
        if (!read_sector_into(lba, sec)) { kfree(buf); return 0; }
        memcpy(buf + off, sec, chunk);
        off += chunk; left -= chunk; lba++;
    }

    if (crc32_buf(buf, total) != h.crc) {
        serial_puts("[PRST] slot crc mismatch (lba ");
        serial_u64(base_lba); serial_puts(")\n");
        kfree(buf); return 0;
    }

    /* publish header info for caller */
    h.seq = h.seq; /* noop */
    snap_nodes = h.node_count;
    seq = h.seq;
    return buf;
}

void persist_load(void) {
    dev = storage_get_device(0);
    if (!dev || !dev->read) return;

    uint8_t* best = 0; uint64_t best_len = 0; uint32_t best_seq = 0;
    int found = 0;
    for (int s = 0; s < 2; s++) {
        uint64_t base = PERSIST_BASE_LBA + s * PERSIST_SLOT_SECT;
        static uint8_t sec[SECTOR];
        if (!read_sector_into(base, sec)) continue;
        pk_hdr_t h; memset(&h, 0, sizeof(h)); memcpy(&h, sec, sizeof(h));
        if (memcmp(h.magic, PERSIST_MAGIC, 8) != 0) continue;
        if (best && h.seq <= best_seq) continue;
        if (best) kfree(best);
        best = load_slot(base);
        if (best) { best_len = (uint64_t)h.rec_bytes + h.payload_bytes;
                    best_seq = h.seq; found = 1; }
        else if (found && best_seq == h.seq) found = 0;
    }
    if (!found || !best) {
        if (best) kfree(best);
        serial_puts("[PRST] no valid snapshot — fresh boot\n");
        return;
    }

    /* Rebuild the table verbatim. */
    for (int i = 0; i < MAX_FILES; i++) {
        pk_rec_t r;
        memcpy(&r, best + (uint64_t)i * REC_SIZE, sizeof(r));
        memset(fs_table_entry(i), 0, sizeof(file_t));
        file_t* fp = fs_table_entry(i);
        fp->in_use = r.used;
        fp->type = (fs_type_t)r.type;
        if (!r.used) { fp->name[0] = 0; fp->parent_dir = -1; continue; }
        memcpy(fp->name, r.name, sizeof(r.name));
        fp->name[sizeof(fp->name) - 1] = 0;
        fp->parent_dir    = r.parent;
        fp->flags         = r.flags;
        fp->size          = r.size;
        fp->creation_time = r.ctime;
        fp->modified_time = r.mtime;
        fp->capacity      = 0;
        fp->data          = 0;
        if (r.type == FS_FILE && r.size > 0) {
            uint64_t off = REC_REGION;
            for (int j = 0; j < i; j++) {
                const pk_rec_t* pr = (const pk_rec_t*)(best + (uint64_t)j * REC_SIZE);
                if (pr->used && pr->type == FS_FILE)
                    off += (uint64_t)pr->size;
            }
            uint8_t* d = (uint8_t*)kmalloc((size_t)r.size);
            if (d) {
                memcpy(d, best + off, (size_t)r.size);
                fp->data = d;
                fp->capacity = r.size;
            } else {
                fp->size = 0;   /* OOM: drop body, keep node */
            }
        }
    }

    serial_puts("[PRST] restored ");
    serial_u64((uint64_t)snap_nodes);
    serial_puts(" nodes, ");
    serial_u64(best_len - REC_REGION);
    serial_puts(" payload bytes (seq ");
    serial_u64((uint64_t)best_seq);
    serial_puts(")\n");
    kfree(best);
    dirty = 0;
}

void persist_sync(const char* reason) {
    if (!dev) dev = storage_get_device(0);
    if (!dev || !dev->write) return;
    if (!dirty) return;
    if (!build_snapshot()) return;

    uint64_t need = (snap_len + SECTOR - 1) / SECTOR + 1;
    if (need > PERSIST_SLOT_SECT) {
        serial_puts("[PRST] snapshot exceeds slot capacity\n");
        kfree(snap); snap = 0;
        return;
    }

    seq++;
    uint64_t base = PERSIST_BASE_LBA + (seq & 1) * PERSIST_SLOT_SECT;
    if (write_slot(base)) {
        dirty = 0;
        serial_puts("[PRST] saved (");
        serial_puts(reason);
        serial_puts(") nodes=");
        serial_u64((uint64_t)snap_nodes);
        serial_puts(" bytes=");
        serial_u64(snap_len);
        serial_puts("\n");
    }
    kfree(snap); snap = 0;
}

/* First boot has nothing dirty (kernel-side seeding bypasses syscalls),
 * but we still want a warm baseline snapshot immediately. */
void persist_force_first_save(void) {
    dev = storage_get_device(0);
    if (!dev || !dev->write) return;
    dirty = 1;
    persist_sync("first-save");
}

void persistd_entry(void) {
    for (;;) {
        uint64_t t = timer_get_ms();
        while (timer_get_ms() - t < 15000)
            sleep_task(100);
        if (dirty) persist_sync("autosave");
    }
}
