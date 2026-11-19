#pragma once

#include <stddef.h>

// Mock HKDF for native testing — deterministic but not cryptographic.
template <typename T> void hkdf
    (void *out, size_t outLen, const void *key, size_t keyLen,
     const void *salt, size_t saltLen, const void *info, size_t infoLen)
{
    int j = 0, k = 0;
    for (int i = 0; i < outLen; i++) {
        ((uint8_t *)out)[i] = ((const uint8_t *)key)[k] ^ ((const uint8_t *)info)[j];
        k = (k + 1) % keyLen;
        j = (j + 1) % infoLen;
    }
}
