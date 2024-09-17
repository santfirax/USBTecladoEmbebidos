
#ifndef UTIL_H_
#define UTIL_H_

#include <stdio.h>


#define ASSERT(x) if (!(x)) {printf("Assertion failed! " DEBUG_CNTX ":%i -> " #x "\n", __LINE__); while (1);}
#define DEBUG(fmt, ...) printf(DEBUG_CNTX " - %i: " fmt "\n", __LINE__, ##__VA_ARGS__)

#define swap_bytes(val) ((0xff & (val >> 8)) | (0xff00 & (val << 8)))


int hexdump(FILE *fd, void const *ptr, size_t length, int linelen, int split);

#endif