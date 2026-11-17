#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define ENABLE_COMPRESSION 1
#include "helpers/Compression.h"

// Typical English text fragments for different lengths
static const char* get_test_message(int target_len) {
    static const char base[] =
        "Hello from node Alpha, please relay this message to all nearby stations. "
        "Weather is clear, signal strong, battery at 85 percent. "
        "Meet at the usual coordinates at sunset. Over and out.";
    static char buf[256];
    int base_len = (int)strlen(base);
    int copy_len = target_len < base_len ? target_len : base_len;
    memcpy(buf, base, copy_len);
    buf[copy_len] = '\0';
    return buf;
}

int main() {
    printf("=== Unishox2 Compression Benchmark ===\n\n");
    printf("%-10s %-12s %-12s %-10s %-10s\n",
           "Size", "Original", "Compressed", "Ratio", "Worthwhile?");
    printf("%-10s %-12s %-12s %-10s %-10s\n",
           "------", "--------", "----------", "-----", "-----------");

    const int test_sizes[] = {10, 30, 50, 100, 150};
    const int num_tests = 5;

    int passed = 0;
    int failed_roundtrip = 0;

    for (int t = 0; t < num_tests; t++) {
        int target = test_sizes[t];
        const char* msg = get_test_message(target);
        int in_len = (int)strlen(msg);

        uint8_t compressed[256];
        uint8_t decompressed[256];
        memset(compressed, 0, sizeof(compressed));
        memset(decompressed, 0, sizeof(decompressed));

        // Compress
        int comp_len = mesh::compressPayload(
            (const uint8_t*)msg, in_len,
            compressed, (int)sizeof(compressed)
        );

        const char* worthwhile;
        float ratio = 0.0f;

        if (comp_len > 0) {
            ratio = (float)comp_len / (float)in_len;
            worthwhile = (ratio < 1.0f) ? "YES" : "NO";
        } else {
            worthwhile = "NO (larger)";
            ratio = 1.0f;
        }

        printf("%-10d %-12d %-12d %-10.3f %-10s\n",
               target, in_len,
               comp_len > 0 ? comp_len : in_len,
               ratio,
               worthwhile);

        // Round-trip verification
        if (comp_len > 0) {
            int decomp_len = mesh::decompressPayload(
                compressed, comp_len,
                decompressed, (int)sizeof(decompressed)
            );

            if (decomp_len == in_len && memcmp(msg, decompressed, in_len) == 0) {
                passed++;
            } else {
                printf("  *** ROUND-TRIP FAILED for size %d (decomp_len=%d, expected=%d)\n",
                       target, decomp_len, in_len);
                failed_roundtrip++;
            }
        }
    }

    printf("\n=== Round-trip verification ===\n");
    printf("Passed: %d / %d\n", passed, num_tests);
    if (failed_roundtrip > 0) {
        printf("FAILURES: %d\n", failed_roundtrip);
        return 1;
    }

    printf("\nAll round-trips verified. Compression library is functional.\n");
    return 0;
}
