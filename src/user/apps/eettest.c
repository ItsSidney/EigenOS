/* eettest.c — verify the EigenOS Eet port (raw + data-descriptor round-trips). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Eina.h>
#include <Emile.h>
#include <Eet.h>

typedef struct {
    int        id;
    char      *name;
    double     value;
    unsigned char flag;
} MyStruct;

static void
hexprint(const char *label, const void *p, int n) {
    const unsigned char *b = (const unsigned char *)p;
    printf("[EETTEST] %s (%d bytes): ", label, n);
    for (int i = 0; i < n && i < 48; i++)
        printf("%02x ", b[i]);
    printf("\n");
}

#define CHECK(c, msg) do { \
    if (!(c)) { printf("[EETTEST] FAIL: %s\n", msg); fails++; } \
    else      { printf("[EETTEST] ok:   %s\n", msg); } \
} while (0)

int main(void) {
    int fails = 0;

    eina_init();
    eet_init();

    /* ---- direct emile compress/decompress self-test ---- */
    {
        const char *m = "hello eet compression round trip test 12345";
        int mn = (int)strlen(m) + 1;
        Eina_Binbuf *raw = eina_binbuf_manage_new((void *)m, mn, EINA_FALSE);
        Eina_Binbuf *cb = emile_compress(raw, EMILE_ZLIB, EMILE_COMPRESSOR_BEST);
        printf("[EETTEST] emile compress: %d -> %d\n",
               mn, cb ? (int)eina_binbuf_length_get(cb) : -1);
        Eina_Binbuf *out2 = emile_decompress(cb, EMILE_ZLIB, (unsigned int)mn);
        int ok = out2 && (int)eina_binbuf_length_get(out2) == mn &&
                 memcmp(eina_binbuf_string_get(out2), m, mn) == 0;
        printf("[EETTEST] emile decompress match=%d\n", ok);
        if (!ok) fails++;
    }

    /* ---- raw read/write round-trip (compressed) ---- */
    Eet_File *ef = eet_open("eet_test.eet", EET_FILE_MODE_WRITE);
    CHECK(ef != NULL, "eet_open (write)");

    const char *greeting = "hello eet";
    int w1 = eet_write(ef, "greeting", greeting, (int)strlen(greeting) + 1, 1);
    CHECK(w1 == 1, "eet_write 'greeting'");

    int num = 0xDEADBEEF;
    int w2 = eet_write(ef, "magic", &num, sizeof(num), 1);
    CHECK(w2 == 1, "eet_write 'magic'");

    Eet_Error se = eet_sync(ef);
    CHECK(se == EET_ERROR_NONE, "eet_sync");
    eet_close(ef);

    ef = eet_open("eet_test.eet", EET_FILE_MODE_READ);
    CHECK(ef != NULL, "eet_open (read)");

    int rsz = 0;
    void *rd = eet_read(ef, "greeting", &rsz);
    hexprint("raw greeting", rd, rsz > 0 ? rsz : 0);
    printf("[EETTEST] rsz=%d expected=%d\n", rsz, (int)strlen(greeting) + 1);
    CHECK(rd != NULL && rsz == (int)strlen(greeting) + 1 &&
          strcmp((char *)rd, greeting) == 0, "eet_read 'greeting' round-trip");
    if (rd) free(rd);

    int rdnum = 0, rsz2 = 0;
    void *rdm = eet_read(ef, "magic", &rsz2);
    if (rdm) { memcpy(&rdnum, rdm, sizeof(rdnum)); free(rdm); }
    CHECK(rdm != NULL && rsz2 == sizeof(int) && rdnum == 0xDEADBEEF,
          "eet_read 'magic' round-trip");

    char **names = eet_list(ef, NULL, &rsz);
    CHECK(names != NULL && rsz == 2, "eet_list returns 2 entries");
    if (names) {
        for (int i = 0; names[i]; i++) free(names[i]);
        free(names);
    }
    eet_close(ef);

    /* ---- data descriptor round-trip ---- */
    Eet_Data_Descriptor_Class eddc;
    EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, MyStruct);
    Eet_Data_Descriptor *edd = eet_data_descriptor_stream_new(&eddc);
    CHECK(edd != NULL, "eet_data_descriptor_stream_new");
    EET_DATA_DESCRIPTOR_ADD_BASIC(edd, MyStruct, "id",    id,    EET_T_INT);
    EET_DATA_DESCRIPTOR_ADD_BASIC(edd, MyStruct, "name",  name,  EET_T_STRING);
    EET_DATA_DESCRIPTOR_ADD_BASIC(edd, MyStruct, "value", value, EET_T_DOUBLE);
    EET_DATA_DESCRIPTOR_ADD_BASIC(edd, MyStruct, "flag",  flag,  EET_T_UCHAR);

    MyStruct in;
    memset(&in, 0, sizeof(in));
    in.id = 42;
    in.name = strdup("widget");
    in.value = 3.14159;
    in.flag = 1;

    Eet_File *ef2 = eet_open("eet_desc.eet", EET_FILE_MODE_WRITE);
    CHECK(ef2 != NULL, "eet_open desc (write)");
    int dw = eet_data_write(ef2, edd, "obj", &in, 1);
    CHECK(dw > 0, "eet_data_write");
    CHECK(eet_sync(ef2) == EET_ERROR_NONE, "eet_sync desc");
    eet_close(ef2);

    ef2 = eet_open("eet_desc.eet", EET_FILE_MODE_READ);
    CHECK(ef2 != NULL, "eet_open desc (read)");
    MyStruct *out = (MyStruct *)eet_data_read(ef2, edd, "obj");
    CHECK(out != NULL, "eet_data_read");
    if (out) {
        CHECK(out->id == 42, "descriptor id round-trip");
        CHECK(out->name && strcmp(out->name, "widget") == 0, "descriptor name round-trip");
        CHECK(out->value > 3.14 && out->value < 3.15, "descriptor value round-trip");
        CHECK(out->flag == 1, "descriptor flag round-trip");
        if (out->name) free(out->name);
        free(out);
    }
    eet_close(ef2);

    eet_data_descriptor_free(edd);
    eet_shutdown();
    eina_shutdown();

    if (fails == 0)
        printf("[EETTEST] ALL PASS\n");
    else
        printf("[EETTEST] %d FAILURES\n", fails);
    return fails ? 1 : 0;
}
