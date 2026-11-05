#ifndef PORTABLE_CRYPTO_H
#define PORTABLE_CRYPTO_H
#include <stddef.h>
void *portable_hash_new(void);
void portable_hash_add(void *ctx, const void *data, size_t size);
void portable_hash_end(void *ctx, unsigned char out[32]);
int portable_decrypt(const unsigned char key[32], const unsigned char header[64],
                     const unsigned char *cipher, const unsigned char tag[16],
                     unsigned char *plain, size_t size);
#endif
