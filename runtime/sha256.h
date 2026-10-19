#ifndef TP_SHA256_H
#define TP_SHA256_H
#include <stddef.h>
#include <stdint.h>
typedef struct { uint32_t h[8]; uint64_t len; uint8_t buf[64]; size_t n; } TpSha256;
void tp_sha256_init(TpSha256 *c);
void tp_sha256_update(TpSha256 *c, const void *data, size_t len);
void tp_sha256_final(TpSha256 *c, uint8_t out[32]);
void tp_sha256(const void *data, size_t len, uint8_t out[32]);
#endif
