/* stub random */
#define RNDADDENTROPY 0x5207
#define RNDZAPENTCNT 0x5204
#define RNDCLEARPOOL 0x5206
struct rand_pool_info { int entropy_count, buf_size; unsigned int buf[0]; };
