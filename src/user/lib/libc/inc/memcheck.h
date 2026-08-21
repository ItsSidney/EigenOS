#ifndef EIGEN_SHIM_MEMCHECK_H
#define EIGEN_SHIM_MEMCHECK_H

/* Dummy Valgrind memcheck.h for EigenOS */
#define VALGRIND_MAKE_MEM_UNDEFINED(_qzz_addr, _qzz_len) (0)
#define VALGRIND_MAKE_MEM_DEFINED(_qzz_addr, _qzz_len)   (0)
#define VALGRIND_MAKE_MEM_DEFINED_IF_ADDRESSABLE(_qzz_addr, _qzz_len) (0)
#define VALGRIND_CREATE_BLOCK(_qzz_addr, _qzz_len, _qzz_desc) (0)
#define VALGRIND_DISCARD(_qzz_blk) (0)
#define VALGRIND_CHECK_MEM_IS_DEFINED(_qzz_addr, _qzz_len) (0)
#define VALGRIND_CHECK_MEM_IS_ADDRESSABLE(_qzz_addr, _qzz_len) (0)
#define VALGRIND_CHECK_VALUE_IS_DEFINED(_qzz_v) (0)
#define VALGRIND_DO_LEAK_CHECK (0)

#endif /* EIGEN_SHIM_MEMCHECK_H */
