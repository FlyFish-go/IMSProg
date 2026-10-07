#ifndef IMSPROG_TEST_FAKE_WINSOCK2_H
#define IMSPROG_TEST_FAKE_WINSOCK2_H
#include <arpa/inet.h>
/* ch347.h supplies Windows spellings; avoid the host libc's endian macros. */
#undef htobe16
#undef be16toh
#undef htobe32
#undef be32toh
#undef htole16
#undef le16toh
#undef htole32
#undef le32toh
#endif
