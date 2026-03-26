#pragma once
#include <stdio.h>

// Minimal Unity framework stub for native testing
#define TEST_ASSERT_EQUAL_UINT8(expected, actual) \
    if ((expected) != (actual)) { \
        printf("ASSERT FAILED: %d != %d\n", (int)(expected), (int)(actual)); \
        return; \
    }

#define TEST_ASSERT_EQUAL_UINT16(expected, actual) \
    if ((expected) != (actual)) { \
        printf("ASSERT FAILED: %d != %d\n", (int)(expected), (int)(actual)); \
        return; \
    }

#define TEST_ASSERT_EQUAL_UINT32(expected, actual) \
    if ((expected) != (actual)) { \
        printf("ASSERT FAILED: %u != %u\n", (unsigned)(expected), (unsigned)(actual)); \
        return; \
    }

#define TEST_ASSERT_EQUAL_INT(expected, actual) \
    if ((expected) != (actual)) { \
        printf("ASSERT FAILED: %d != %d\n", (int)(expected), (int)(actual)); \
        return; \
    }

#define TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, len) \
    for (size_t i = 0; i < (len); i++) { \
        if ((expected)[i] != (actual)[i]) { \
            printf("ASSERT ARRAY FAILED at [%zu]: %02x != %02x\n", i, (expected)[i], (actual)[i]); \
            return; \
        } \
    }

#define TEST_ASSERT_TRUE(condition) \
    if (!(condition)) { \
        printf("ASSERT TRUE FAILED: condition was false\n"); \
        return; \
    }

#define TEST_ASSERT_FALSE(condition) \
    if ((condition)) { \
        printf("ASSERT FALSE FAILED: condition was true\n"); \
        return; \
    }

#define TEST_ASSERT_NOT_EQUAL(a, b) \
    if ((a) == (b)) { \
        printf("ASSERT NOT EQUAL FAILED: values were equal\n"); \
        return; \
    }
