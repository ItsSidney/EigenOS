/* EigenOS port: capabilities unsupported. All entry points no-op.
   Callers are config-disabled (CONFIG_FEATURE_SETPRIV=n); this TU only
   exists because libbb compiles unconditionally. */
#include "libbb.h"
#include <linux/capability.h>
DEFINE_STRUCT_CAPS;

static const char *const capabilities[] ALIGN_PTR = {
	"chown",
	"dac_override",
	"dac_read_search",
	"fowner",
	"fsetid",
	"kill",
	"setgid",
	"setuid",
	"setpcap",
	"linux_immutable",
	"net_bind_service",
	"net_broadcast",
	"net_admin",
	"net_raw",
	"ipc_lock",
	"ipc_owner",
	"sys_module",
	"sys_rawio",
	"sys_chroot",
	"sys_ptrace",
	"sys_pacct",
	"sys_admin",
	"sys_boot",
	"sys_nice",
	"sys_resource",
	"sys_time",
	"sys_tty_config",
	"mknod",
	"lease",
	"audit_write",
	"audit_control",
	"setfcap",
	"mac_override",
	"mac_admin",
	"syslog",
	"wake_alarm",
	"block_suspend",
	"audit_read",
};
long capget(void* hdr, void* data) { (void)hdr; (void)data; return -1; }
long capset(void* hdr, const void* data) { (void)hdr; (void)data; return -1; }
const char* cap_num_to_name(unsigned num) { (void)num; return 0; }
int drop_capabilities(void) { return 0; }

unsigned FAST_FUNC cap_name_to_number(const char *cap)
{
	unsigned i, n;

	if ((sscanf(cap, "cap_%u", &n)) == 1) {
		i = n;
		goto found;
	}
	for (i = 0; i < ARRAY_SIZE(capabilities); i++) {
		if (strcasecmp(capabilities[i], cap) == 0)
			goto found;
	}
	bb_error_msg_and_die("unknown capability '%s'", cap);

 found:
	if (!cap_valid(i))
		bb_error_msg_and_die("unknown capability '%s'", cap);
	return i;
}

void FAST_FUNC getcaps(void *arg)
{
	static const uint8_t versions[] = {
		_LINUX_CAPABILITY_U32S_3, /* = 2 (fits into byte) */
		_LINUX_CAPABILITY_U32S_2, /* = 2 */
		_LINUX_CAPABILITY_U32S_1, /* = 1 */
	};
	int i;
	struct caps *caps = arg;

	caps->header.pid = 0;
	for (i = 0; i < ARRAY_SIZE(versions); i++) {
		caps->header.version = versions[i];
		if (capget(&caps->header, NULL) == 0)
			goto got_it;
	}
	bb_simple_perror_msg_and_die("capget");
 got_it:

	switch (caps->header.version) {
		case _LINUX_CAPABILITY_VERSION_1:
			caps->u32s = _LINUX_CAPABILITY_U32S_1;
			break;
		case _LINUX_CAPABILITY_VERSION_2:
			caps->u32s = _LINUX_CAPABILITY_U32S_2;
			break;
		case _LINUX_CAPABILITY_VERSION_3:
			caps->u32s = _LINUX_CAPABILITY_U32S_3;
			break;
		default:
			bb_simple_error_msg_and_die("unsupported capability version");
	}

	if (capget(&caps->header, caps->data) != 0)
		bb_simple_perror_msg_and_die("capget");
}
