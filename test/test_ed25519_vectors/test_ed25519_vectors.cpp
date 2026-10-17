// Ed25519 known-answer tests (RFC 8032 section 7.1, TEST 1 and TEST 2). Run with the full fixed-base table
// ([env:native]) and with the small one ([env:native_ed25519_small], -D ED25519_SMALL_BASE_TABLE).
#include "ed_25519.h"

#include <gtest/gtest.h>
#include <string.h>
extern "C" {
#include "ge.h"
#include "precomp_data.h"
}

static void unhex(uint8_t *out, const char *hex) {
  for (size_t i = 0; hex[2 * i]; i++) {
    unsigned v;
    sscanf(hex + 2 * i, "%2x", &v);
    out[i] = (uint8_t)v;
  }
}

struct Vec {
  const char *seed, *pub, *msg, *sig;
};
static const Vec VECTORS[] = {
  { "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
    "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
    "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0"
    "595bbe24655141438e7a100b" },
  { "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
    "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
    "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eae"
    "b4302aeeb00d291612bb0c00" },
};

TEST(Ed25519Vectors, KeypairSignVerifyMatchRfc8032) {
  for (const Vec &v : VECTORS) {
    uint8_t seed[32], pub_want[32], sig_want[64], msg[8], pub[32], prv[64], sig[64];
    size_t msg_len = strlen(v.msg) / 2;
    unhex(seed, v.seed);
    unhex(pub_want, v.pub);
    unhex(sig_want, v.sig);
    unhex(msg, v.msg);
    ed25519_create_keypair(pub, prv, seed);
    EXPECT_EQ(memcmp(pub, pub_want, 32), 0);
    uint8_t derived[32];
    ed25519_derive_pub(derived, prv);
    EXPECT_EQ(memcmp(derived, pub_want, 32), 0);
    ed25519_sign(sig, msg, msg_len, pub, prv);
    EXPECT_EQ(memcmp(sig, sig_want, 64), 0);
    EXPECT_EQ(ed25519_verify(sig, msg, msg_len, pub), 1);
    sig[5] ^= 0x04;
    EXPECT_EQ(ed25519_verify(sig, msg, msg_len, pub), 0);
  }
}

TEST(Ed25519Vectors, KeyExchangeIsSymmetric) {
  uint8_t sa[32], sb[32], pa[32], pb[32], ka[64], kb[64], s1[32], s2[32];
  unhex(sa, VECTORS[0].seed);
  unhex(sb, VECTORS[1].seed);
  ed25519_create_keypair(pa, ka, sa);
  ed25519_create_keypair(pb, kb, sb);
  ed25519_key_exchange(s1, pb, ka);
  ed25519_key_exchange(s2, pa, kb);
  EXPECT_EQ(memcmp(s1, s2, 32), 0);
}

#ifdef ED25519_SMALL_BASE_TABLE
TEST(Ed25519Vectors, SmallBuildKeepsOnlyOneTableRow) {
  EXPECT_EQ(ED25519_BASE_ROWS, 1);
}
#endif

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
