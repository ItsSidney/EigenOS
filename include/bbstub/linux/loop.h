/* stub loop */
#define LOOP_CTL_GET_FREE 0x4C82
#define LOOP_SET_STATUS64 0x4C04
#define LOOP_GET_STATUS64 0x4C05
#define LOOP_SET_FD 0x4C00
#define LOOP_CLR_FD 0x4C01
#define LOOP_SET_DIRECT_IO 0x4C08
#define LO_FLAGS_AUTOCLEAR 4
#define LO_FLAGS_READ_ONLY 1
#define LO_NAME_SIZE 64
#define LO_KEY_SIZE 32
struct loop_info64 {
    unsigned long long lo_device, lo_inode, lo_rdevice, lo_offset, lo_sizelimit, lo_number, lo_encrypt_type, lo_encrypt_key_size, lo_flags;
    char lo_file_name[64], lo_crypt_name[32], lo_encrypt_key[32], lo_init[16];
};
