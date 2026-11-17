/* GENERATED FILE - DO NOT EDIT BY HAND. */
/* MCOtxt v1 static TOP-4 model (START + AFTER_PUNCT): DE (wire id 3). */
#ifndef MCOTXT_MODEL_DE_H
#define MCOTXT_MODEL_DE_H

#include <stdint.h>

#ifndef MCOTXT_UPPERCASE_PAIR_T_DEFINED
#define MCOTXT_UPPERCASE_PAIR_T_DEFINED
#if defined(__GNUC__) || defined(__clang__)
#define MCOTXT_PACKED __attribute__((packed))
#else
#define MCOTXT_PACKED
#endif
typedef struct MCOTXT_PACKED {
  uint16_t uppercase_codepoint;
  uint8_t lowercase_symbol_index;
} mcotxt_uppercase_pair_t;
#endif

static const uint8_t mcotxt_de_available = 1u;
static const char mcotxt_de_wire_hash[] = "00b971288fee56f37535ce03b4ac0d8ebddfc12ecbd5ef282ab4b0f4a659f451";
static const uint8_t mcotxt_de_language_id = 3u;
static const uint8_t mcotxt_de_primary_count = 32u;
static const uint8_t mcotxt_de_extension_count = 9u;
static const uint8_t mcotxt_de_symbol_count = 41u;
static const uint8_t mcotxt_de_uppercase_count = 30u;

static const uint16_t mcotxt_de_primary_symbols[32] = {
  0x0020u, 0x0073u, 0x0074u, 0x0062u, 0x0068u, 0x006Fu, 0x0067u, 0x006Cu, 0x0069u, 0x006Du,
  0x006Eu, 0x0072u, 0x0075u, 0x0077u, 0x006Bu, 0x0066u, 0x0065u, 0x0063u, 0x0061u, 0x0064u,
  0x007Au, 0x0070u, 0x00FCu, 0x0076u, 0x006Au, 0x00F6u, 0x00E4u, 0x0031u, 0x0033u, 0x0032u,
  0x0079u, 0x0035u,
};

static const uint16_t mcotxt_de_extension_symbols[9] = {
  0x0039u, 0x0034u, 0x0036u, 0x0037u, 0x0038u, 0x00DFu, 0x0078u, 0x0030u, 0x0071u,
};

static const uint8_t mcotxt_de_start_top4[4] = {
  6u, 9u, 4u, 19u,
};

static const uint8_t mcotxt_de_punct_start_top4[4] = {
  0u, 3u, 19u, 18u,
};

static const uint8_t mcotxt_de_top4[164] = {
  18u, 19u, 9u, 8u, 0u, 2u, 16u, 17u, 0u, 16u, 18u, 2u, 16u, 8u, 18u, 0u, 0u, 18u, 16u, 2u, 11u,
  8u, 10u, 21u, 16u, 12u, 11u, 0u, 7u, 16u, 8u, 0u, 10u, 17u, 16u, 2u, 5u, 16u, 8u, 18u, 0u, 19u,
  16u, 18u, 0u, 16u, 6u, 18u, 1u, 2u, 10u, 11u, 16u, 8u, 18u, 5u, 16u, 5u, 18u, 0u, 16u, 18u, 0u,
  11u, 10u, 11u, 0u, 8u, 4u, 14u, 5u, 0u, 12u, 10u, 7u, 17u, 16u, 18u, 0u, 8u, 12u, 16u, 2u, 0u,
  1u, 16u, 21u, 15u, 37u, 11u, 3u, 17u, 5u, 16u, 8u, 12u, 16u, 18u, 5u, 12u, 10u, 11u, 7u, 6u, 2u,
  26u, 12u, 10u, 0u, 39u, 27u, 29u, 0u, 27u, 39u, 36u, 0u, 39u, 35u, 28u, 2u, 0u, 16u, 5u, 0u, 39u,
  4u, 35u, 0u, 4u, 39u, 36u, 0u, 35u, 39u, 34u, 0u, 27u, 28u, 34u, 0u, 28u, 27u, 33u, 0u, 27u, 28u,
  34u, 16u, 0u, 2u, 8u, 0u, 8u, 2u, 16u, 0u, 39u, 34u, 35u, 12u, 0u, 2u, 1u,
};

static const mcotxt_uppercase_pair_t mcotxt_de_uppercase_map[30] = {
  { 0x0041u, 18u }, /* A -> a */
  { 0x0042u, 3u }, /* B -> b */
  { 0x0043u, 17u }, /* C -> c */
  { 0x0044u, 19u }, /* D -> d */
  { 0x0045u, 16u }, /* E -> e */
  { 0x0046u, 15u }, /* F -> f */
  { 0x0047u, 6u }, /* G -> g */
  { 0x0048u, 4u }, /* H -> h */
  { 0x0049u, 8u }, /* I -> i */
  { 0x004Au, 24u }, /* J -> j */
  { 0x004Bu, 14u }, /* K -> k */
  { 0x004Cu, 7u }, /* L -> l */
  { 0x004Du, 9u }, /* M -> m */
  { 0x004Eu, 10u }, /* N -> n */
  { 0x004Fu, 5u }, /* O -> o */
  { 0x0050u, 21u }, /* P -> p */
  { 0x0051u, 40u }, /* Q -> q */
  { 0x0052u, 11u }, /* R -> r */
  { 0x0053u, 1u }, /* S -> s */
  { 0x0054u, 2u }, /* T -> t */
  { 0x0055u, 12u }, /* U -> u */
  { 0x0056u, 23u }, /* V -> v */
  { 0x0057u, 13u }, /* W -> w */
  { 0x0058u, 38u }, /* X -> x */
  { 0x0059u, 30u }, /* Y -> y */
  { 0x005Au, 20u }, /* Z -> z */
  { 0x00C4u, 26u }, /* Ä -> ä */
  { 0x00D6u, 25u }, /* Ö -> ö */
  { 0x00DCu, 22u }, /* Ü -> ü */
  { 0x1E9Eu, 37u }, /* ẞ -> ß */
};

#endif /* MCOTXT_MODEL_DE_H */
