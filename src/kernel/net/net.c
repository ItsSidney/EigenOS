/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/net/if.h"
#include "kernel/net/in.h"
#include "drivers/bus/pci.h"
#include "drivers/video/framebuffer.h"
#include "string.h"

/*
 * Coordination for networking initialization in Eigen.
 * Supports: Intel e1000 (em0), VirtIO-net (vn0), AMD PCnet (pn0)
 */

extern void e1000_attach(pci_device_t* dev);
extern void e1000_poll(struct ifnet* ifp);

extern void virtio_net_attach(pci_device_t* dev);
extern void virtio_net_poll(struct ifnet* ifp);

extern void pcnet_attach(pci_device_t* dev);
extern void pcnet_poll(struct ifnet* ifp);

extern void loopback_init();

extern int dhcp_request(struct ifnet* ifp);

extern void boot_log_add(const char* tag, const char* msg, uint32_t tag_color, uint32_t msg_color);
extern void draw_boot_log(void);

#include "kernel/net/net_log.h"

/* Try DHCP; if no server answers, fall back to the classic QEMU user-net
 * static lease (10.0.2.15/24, gateway 10.0.2.2, DNS 10.0.2.3) so the dev
 * VM keeps working. */
static void configure_iface(struct ifnet* ifp, const char* if_name) {
    if (!ifp) return;

    if (dhcp_request(ifp) == 0) {
        uint32_t h = ntohl(ifp->if_ip);
        char ipbuf[48];
        extern int snprintf(char*, int, const char*, ...);
        snprintf(ipbuf, sizeof(ipbuf), "%s configured via DHCP: %u.%u.%u.%u",
            if_name, (h >> 24) & 0xFF, (h >> 16) & 0xFF, (h >> 8) & 0xFF, h & 0xFF);
        boot_log_add("NET", ipbuf, 0x39D2C0, 0x3FB950);
        net_log_addf("NET", 0x3FB950, "%s MAC %02X:%02X:%02X:%02X:%02X:%02X IP(DHCP): %u.%u.%u.%u",
            if_name,
            ifp->if_hwaddr[0], ifp->if_hwaddr[1], ifp->if_hwaddr[2],
            ifp->if_hwaddr[3], ifp->if_hwaddr[4], ifp->if_hwaddr[5],
            (h >> 24) & 0xFF, (h >> 16) & 0xFF, (h >> 8) & 0xFF, h & 0xFF);
        draw_boot_log();
        extern int dns_set_server(uint32_t);
        dns_set_server(ifp->if_dns);
        return;
    }

    /* Static fallback (QEMU user-net) */
    ifp->if_ip      = htonl(0x0A00020F); /* 10.0.2.15 */
    ifp->if_netmask = htonl(0xFFFFFF00u);
    ifp->if_gateway = htonl(0x0A000202); /* 10.0.2.2 */
    ifp->if_dns     = htonl(0x0A000203); /* 10.0.2.3 */
    ifp->if_dhcp    = 0;
    uint32_t h = 0x0A00020F;
    char ipbuf[48];
    extern int snprintf(char*, int, const char*, ...);
    snprintf(ipbuf, sizeof(ipbuf), "%s assigned static IP %u.%u.%u.%u",
        if_name, (h >> 24) & 0xFF, (h >> 16) & 0xFF, (h >> 8) & 0xFF, h & 0xFF);
    boot_log_add("NET", ipbuf, 0x39D2C0, 0x3FB950);
    net_log_addf("NET", 0x3FB950, "%s MAC %02X:%02X:%02X:%02X:%02X:%02X IP: %u.%u.%u.%u",
        if_name,
        ifp->if_hwaddr[0], ifp->if_hwaddr[1], ifp->if_hwaddr[2],
        ifp->if_hwaddr[3], ifp->if_hwaddr[4], ifp->if_hwaddr[5],
        (h >> 24) & 0xFF, (h >> 16) & 0xFF, (h >> 8) & 0xFF, h & 0xFF);
    draw_boot_log();
    extern int dns_set_server(uint32_t);
    dns_set_server(ifp->if_dns);
}

static int is_known_e1000(uint16_t dev_id) {
    /* Classic 8254x/8257x gigabit controllers and early e1000e
     * (82571/82572/82573/82574/82579/I217/I218) that this driver handles. */
    switch (dev_id) {
        case 0x1000: case 0x1001: case 0x1004: case 0x1008: case 0x1009:
        case 0x100C: case 0x100D: case 0x100E: case 0x100F: case 0x1010:
        case 0x1011: case 0x1012: case 0x1013: case 0x1015: case 0x1016:
        case 0x1017: case 0x1018: case 0x1019: case 0x1026: case 0x1027:
        case 0x1028: case 0x105E: case 0x105F: case 0x1060: case 0x1075:
        case 0x1076: case 0x1077: case 0x1078: case 0x1079: case 0x107A:
        case 0x107B: case 0x107C: case 0x108A: case 0x108B: case 0x108C:
        case 0x1096: case 0x1098: case 0x1099: case 0x10A4: case 0x10A5:
        case 0x10A7: case 0x10A8: case 0x10A9: case 0x10B5: case 0x10B7:
        case 0x10B9: case 0x10BA: case 0x10D3: case 0x10D5: case 0x10D6:
        case 0x10D7: case 0x10DE: case 0x10DF: case 0x1502: case 0x1503:
        case 0x1521: case 0x1522: case 0x1523: case 0x15A0: case 0x15A1:
        case 0x15A2: case 0x15A3:
            return 1;
        default:
            return 0;
    }
}

void net_init() {
    net_log_init();
    net_log_add("NET", "FreeBSD-style network stack starting...", 0x39D2C0);

    boot_log_add("NET", "Initializing FreeBSD-style stack...", 0x39D2C0, 0x8B949E);
    draw_boot_log();

    loopback_init();

    /* Scan PCI for supported network cards */
    int count = pci_get_device_count();
    int found_nic = 0;

    for (int i = 0; i < count; i++) {
        pci_device_t* dev = pci_get_device(i);
        if (dev->class_id != 0x02) continue; /* Not a NIC */

        /* Intel NICs (em0): only attach models this driver actually handles.
         * Modern I219/I225/I226 use different register semantics; probing
         * them as a legacy e1000 can wedge the device and hang boot. */
        if (dev->vendor_id == 0x8086 && is_known_e1000(dev->device_id)) {
            boot_log_add("NET", "Intel Gigabit NIC found, attaching...", 0x39D2C0, 0x3FB950);
            draw_boot_log();
            e1000_attach(dev);
            struct ifnet* ifp = if_find("em0");
            configure_iface(ifp, "em0");
            found_nic++;
            continue;
        }

        if (dev->vendor_id == 0x8086) {
            char msg2[64];
            extern int snprintf(char*, int, const char*, ...);
            snprintf(msg2, sizeof(msg2), "Unsupported Intel NIC dev=0x%04X (skipped)", dev->device_id);
            boot_log_add("NET", msg2, 0x39D2C0, 0xF0883E);
            draw_boot_log();
            continue;
        }

        /* VirtIO-net (vn0) — vendor 0x1AF4, device 0x1000 or 0x1041 */
        if (dev->vendor_id == 0x1AF4 &&
            (dev->device_id == 0x1000 || dev->device_id == 0x1041)) {
            boot_log_add("NET", "VirtIO-net found, attaching...", 0x39D2C0, 0x3FB950);
            draw_boot_log();
            virtio_net_attach(dev);
            struct ifnet* ifp = if_find("vn0");
            configure_iface(ifp, "vn0");
            found_nic++;
            continue;
        }

        /* AMD PCnet (pn0) — vendor 0x1022, device 0x2000 (Am79C970A) */
        if (dev->vendor_id == 0x1022 && dev->device_id == 0x2000) {
            boot_log_add("NET", "AMD PCnet NIC found, attaching...", 0x39D2C0, 0x3FB950);
            draw_boot_log();
            pcnet_attach(dev);
            struct ifnet* ifp = if_find("pn0");
            configure_iface(ifp, "pn0");
            found_nic++;
            continue;
        }

        /* Unknown NIC */
        char msg[64];
        extern int snprintf(char*, int, const char*, ...);
        snprintf(msg, sizeof(msg), "Unknown NIC vendor=0x%04X dev=0x%04X",
            dev->vendor_id, dev->device_id);
        boot_log_add("NET", msg, 0x39D2C0, 0xF0883E);
        net_log_addf("NET", 0xF0883E, "Unsupported PCI NIC vendor=0x%04X dev=0x%04X",
            dev->vendor_id, dev->device_id);
        draw_boot_log();
    }

    if (found_nic == 0) {
        boot_log_add("NET", "No supported NIC found — network unavailable", 0x39D2C0, 0xF85149);
        net_log_add("NET", "No NIC found", 0xF85149);
        draw_boot_log();
    }
}

void net_poll() {
    struct ifnet* ifp = if_list_head();
    while (ifp) {
        if (ifp->if_flags & IFF_RUNNING) {
            /* Poll e1000 (em0) */
            if (ifp->if_xname[0] == 'e' && ifp->if_xname[1] == 'm') {
                e1000_poll(ifp);
            }
            /* Poll virtio-net (vn0) */
            else if (ifp->if_xname[0] == 'v' && ifp->if_xname[1] == 'n') {
                virtio_net_poll(ifp);
            }
            /* Poll AMD PCnet (pn0) */
            else if (ifp->if_xname[0] == 'p' && ifp->if_xname[1] == 'n') {
                pcnet_poll(ifp);
            }
        }
        ifp = ifp->if_next;
    }
}
