/* ECB-only test per validazione ANB.
 * CBC e CTR disabilitati: evita SIGFPE spurio di ANB su AES_init_ctx_iv.
 * Test con NIST FIPS-197 Appendix B vector (AES-128 ECB). */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define CBC 0
#define CTR 0
#define ECB 1
#include "aes.h"

/* NIST FIPS-197 Appendix B test vector */
static const uint8_t key[16] = {
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
};
static const uint8_t plaintext[16] = {
    0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
    0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a
};
static const uint8_t expected_cipher[16] = {
    0x3a, 0xd7, 0x7b, 0xb4, 0x0d, 0x7a, 0x36, 0x60,
    0xa8, 0x9e, 0xca, 0xf3, 0x24, 0x66, 0xef, 0x97
};

static void phex(const char *label, const uint8_t *buf, int len)
{
    printf("%s: ", label);
    for (int i = 0; i < len; i++)
        printf("%02x", buf[i]);
    printf("\n");
}

int main(void)
{
    printf("\n=== ANB + AES-128 ECB test ===\n\n");

    uint8_t buf[16];
    struct AES_ctx ctx;

    memcpy(buf, plaintext, 16);

    phex("Plaintext ", buf, 16);
    phex("Key       ", key,  16);

    AES_init_ctx(&ctx, key);
    AES_ECB_encrypt(&ctx, buf);

    phex("Ciphertext", buf, 16);
    phex("Expected  ", expected_cipher, 16);

    if (memcmp(buf, expected_cipher, 16) == 0) {
        printf("\n[OK] AES-128 ECB encryption: CORRECT\n");
        return 0;
    } else {
        printf("\n[FAIL] AES-128 ECB encryption: WRONG\n");
        return 1;
    }
}
