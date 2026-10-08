/*
 * Copyright (C) 2018 Southern Storm Software, Pty Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "AES.h"
#include "Crypto.h"
#include <string.h>

// AES implementation for ESP32 using the hardware crypto module.

#if defined(CRYPTO_AES_ESP32)

// Declare the functions in the esp-idf SDK that we need.
extern "C" {
int esp_aes_setkey(unsigned char *ctx, const unsigned char *key,
                   unsigned int keybits);
int esp_aes_crypt_ecb(unsigned char *ctx, int mode,
                      const unsigned char *input,
                      unsigned char *output);
};

AESCommon::AESCommon(uint8_t keySize)
{
    memset(ctx, 0, sizeof(ctx));
    ctx[0] = keySize;
}

AESCommon::~AESCommon()
{
    clean(ctx, sizeof(ctx));
}

size_t AESCommon::blockSize() const
{
    return 16;
}

size_t AESCommon::keySize() const
{
    return ctx[0];
}

bool AESCommon::setKey(const uint8_t *key, size_t len)
{
    if (len == ctx[0]) {
        esp_aes_setkey(ctx, key, len * 8);
        return true;
    }
    return false;
}

void AESCommon::encryptBlock(uint8_t *output, const uint8_t *input)
{
    esp_aes_crypt_ecb(ctx, 1, input, output);
}

void AESCommon::decryptBlock(uint8_t *output, const uint8_t *input)
{
    esp_aes_crypt_ecb(ctx, 0, input, output);
}

void AESCommon::clear()
{
    uint8_t keySize = ctx[0];
    clean(ctx, sizeof(ctx));
    ctx[0] = keySize;
}

AES128::~AES128()
{
}

AES192::~AES192()
{
}

AES256::~AES256()
{
}

#endif // CRYPTO_AES_ESP32
