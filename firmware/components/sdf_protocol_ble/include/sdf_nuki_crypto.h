#ifndef SDF_NUKI_CRYPTO_H
#define SDF_NUKI_CRYPTO_H

#ifdef __cplusplus
extern "C" {
#endif

int crypto_secretbox(
    unsigned char *c,
    const unsigned char *m,
    unsigned long long mlen,
    const unsigned char *n,
    const unsigned char *k);

int crypto_secretbox_open(
    unsigned char *m,
    const unsigned char *c,
    unsigned long long mlen,
    const unsigned char *n,
    const unsigned char *k);

int crypto_core_hsalsa20(
    unsigned char *out,
    const unsigned char *in,
    const unsigned char *k,
    const unsigned char *c);

int crypto_scalarmult(
    unsigned char *q,
    const unsigned char *n,
    const unsigned char *p);

#ifdef __cplusplus
}
#endif

#endif /* SDF_NUKI_CRYPTO_H */
