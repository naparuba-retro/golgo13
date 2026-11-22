#ifndef GOLGO_INFLATE_H
#define GOLGO_INFLATE_H
#include <stdint.h>
#include <stddef.h>
int rt_inflate(const uint8_t *src, size_t n, uint8_t *dst, size_t cap, size_t *outlen);
#endif
