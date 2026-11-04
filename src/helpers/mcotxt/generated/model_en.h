/* GENERATED FILE - DO NOT EDIT BY HAND. */
/* MCOtxt v1 static TOP-4 model (START + AFTER_PUNCT): EN (wire id 0). */
#ifndef MCOTXT_MODEL_EN_H
#define MCOTXT_MODEL_EN_H

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

static const uint8_t mcotxt_en_available = 1u;
static const char mcotxt_en_wire_hash[] = "55988b3bb2a000adf6e768a8541df5a25fec1628b4ec661a10b577e3af8b3770";
static const uint8_t mcotxt_en_language_id = 0u;
static const uint8_t mcotxt_en_primary_count = 32u;
static const uint8_t mcotxt_en_extension_count = 5u;
static const uint8_t mcotxt_en_symbol_count = 37u;
static const uint8_t mcotxt_en_uppercase_count = 26u;

static const uint16_t mcotxt_en_primary_symbols[32] = {
  0x0020u, 0x006Fu, 0x0064u, 0x0061u, 0x0069u, 0x006Cu, 0x0072u, 0x006Du, 0x0079u, 0x0074u,
  0x0063u, 0x0070u, 0x0077u, 0x0073u, 0x0066u, 0x0067u, 0x0068u, 0x0062u, 0x006Eu, 0x0065u,
  0x006Bu, 0x0075u, 0x0076u, 0x0078u, 0x0031u, 0x006Au, 0x0033u, 0x0032u, 0x0038u, 0x0034u,
  0x007Au, 0x0035u,
};

static const uint16_t mcotxt_en_extension_symbols[5] = {
  0x0071u, 0x0036u, 0x0037u, 0x0039u, 0x0030u,
};

static const uint8_t mcotxt_en_start_top4[4] = {
  16u, 7u, 9u, 4u,
};

static const uint8_t mcotxt_en_punct_start_top4[4] = {
  0u, 13u, 9u, 7u,
};

static const uint8_t mcotxt_en_top4[148] = {
  9u, 3u, 4u, 13u, 18u, 6u, 0u, 21u, 0u, 1u, 19u, 3u, 18u, 9u, 5u, 0u, 18u, 9u, 13u, 0u, 5u, 1u,
  0u, 19u, 19u, 0u, 18u, 1u, 19u, 1u, 0u, 3u, 0u, 1u, 19u, 13u, 0u, 16u, 1u, 19u, 1u, 16u, 3u, 19u,
  19u, 13u, 0u, 1u, 19u, 4u, 3u, 0u, 0u, 9u, 19u, 16u, 0u, 1u, 6u, 4u, 0u, 1u, 16u, 19u, 19u, 3u,
  1u, 4u, 19u, 21u, 1u, 3u, 15u, 0u, 4u, 19u, 0u, 6u, 13u, 18u, 19u, 0u, 13u, 4u, 9u, 13u, 0u, 6u,
  19u, 4u, 29u, 3u, 0u, 9u, 11u, 4u, 36u, 0u, 31u, 27u, 21u, 1u, 3u, 19u, 0u, 36u, 33u, 2u, 0u,
  27u, 31u, 36u, 0u, 7u, 33u, 36u, 0u, 36u, 29u, 33u, 19u, 30u, 8u, 4u, 0u, 36u, 33u, 27u, 21u, 0u,
  7u, 9u, 0u, 36u, 35u, 28u, 0u, 5u, 10u, 6u, 0u, 33u, 2u, 35u, 0u, 36u, 7u, 35u,
};

static const mcotxt_uppercase_pair_t mcotxt_en_uppercase_map[26] = {
  { 0x0041u, 3u }, /* A -> a */
  { 0x0042u, 17u }, /* B -> b */
  { 0x0043u, 10u }, /* C -> c */
  { 0x0044u, 2u }, /* D -> d */
  { 0x0045u, 19u }, /* E -> e */
  { 0x0046u, 14u }, /* F -> f */
  { 0x0047u, 15u }, /* G -> g */
  { 0x0048u, 16u }, /* H -> h */
  { 0x0049u, 4u }, /* I -> i */
  { 0x004Au, 25u }, /* J -> j */
  { 0x004Bu, 20u }, /* K -> k */
  { 0x004Cu, 5u }, /* L -> l */
  { 0x004Du, 7u }, /* M -> m */
  { 0x004Eu, 18u }, /* N -> n */
  { 0x004Fu, 1u }, /* O -> o */
  { 0x0050u, 11u }, /* P -> p */
  { 0x0051u, 32u }, /* Q -> q */
  { 0x0052u, 6u }, /* R -> r */
  { 0x0053u, 13u }, /* S -> s */
  { 0x0054u, 9u }, /* T -> t */
  { 0x0055u, 21u }, /* U -> u */
  { 0x0056u, 22u }, /* V -> v */
  { 0x0057u, 12u }, /* W -> w */
  { 0x0058u, 23u }, /* X -> x */
  { 0x0059u, 8u }, /* Y -> y */
  { 0x005Au, 30u }, /* Z -> z */
};

#endif /* MCOTXT_MODEL_EN_H */
