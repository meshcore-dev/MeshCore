/* GENERATED FILE - DO NOT EDIT BY HAND. */
/* MCOtxt v1 static TOP-4 model (START + AFTER_PUNCT): IT (wire id 4). */
#ifndef MCOTXT_MODEL_IT_H
#define MCOTXT_MODEL_IT_H

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

static const uint8_t mcotxt_it_available = 1u;
static const char mcotxt_it_wire_hash[] = "f6d073a1d6ae6344eaac445b86fb8a454b276a740fd40651956b1ff28f5af270";
static const uint8_t mcotxt_it_language_id = 4u;
static const uint8_t mcotxt_it_primary_count = 32u;
static const uint8_t mcotxt_it_extension_count = 14u;
static const uint8_t mcotxt_it_symbol_count = 46u;
static const uint8_t mcotxt_it_uppercase_count = 35u;

static const uint16_t mcotxt_it_primary_symbols[32] = {
  0x0020u, 0x0074u, 0x006Cu, 0x006Du, 0x0070u, 0x0075u, 0x0069u, 0x0072u, 0x0063u, 0x0073u,
  0x0067u, 0x0076u, 0x0065u, 0x006Fu, 0x0061u, 0x006Eu, 0x0064u, 0x0062u, 0x0068u, 0x007Au,
  0x0066u, 0x0071u, 0x00E8u, 0x0031u, 0x0033u, 0x0032u, 0x0034u, 0x0077u, 0x006Bu, 0x0037u,
  0x0036u, 0x0038u,
};

static const uint16_t mcotxt_it_extension_symbols[14] = {
  0x0035u, 0x0079u, 0x00E0u, 0x00ECu, 0x0039u, 0x00F2u, 0x0030u, 0x00F9u, 0x006Au, 0x00E9u,
  0x0078u, 0x00EDu, 0x00F3u, 0x00FAu,
};

static const uint8_t mcotxt_it_start_top4[4] = {
  17u, 8u, 9u, 6u,
};

static const uint8_t mcotxt_it_punct_start_top4[4] = {
  0u, 6u, 14u, 7u,
};

static const uint8_t mcotxt_it_top4[184] = {
  14u, 9u, 16u, 8u, 12u, 6u, 13u, 1u, 14u, 0u, 12u, 6u, 12u, 6u, 14u, 13u, 12u, 13u, 9u, 7u, 13u,
  15u, 1u, 14u, 0u, 13u, 14u, 15u, 12u, 14u, 6u, 13u, 13u, 6u, 18u, 12u, 12u, 14u, 1u, 6u, 6u, 10u,
  7u, 14u, 12u, 14u, 13u, 6u, 0u, 7u, 15u, 9u, 0u, 15u, 7u, 4u, 0u, 15u, 2u, 7u, 13u, 0u, 14u, 1u,
  14u, 6u, 13u, 12u, 5u, 12u, 6u, 14u, 13u, 12u, 6u, 14u, 6u, 14u, 19u, 13u, 14u, 6u, 12u, 13u, 5u,
  1u, 0u, 7u, 0u, 13u, 14u, 6u, 0u, 38u, 25u, 14u, 0u, 17u, 24u, 18u, 0u, 38u, 31u, 36u, 0u, 36u,
  32u, 18u, 6u, 14u, 0u, 12u, 0u, 6u, 7u, 5u, 24u, 0u, 20u, 32u, 0u, 23u, 31u, 32u, 0u, 30u, 17u,
  20u, 0u, 38u, 23u, 30u, 1u, 0u, 10u, 9u, 0u, 13u, 14u, 6u, 0u, 14u, 15u, 13u, 0u, 13u, 23u, 25u,
  0u, 12u, 13u, 14u, 0u, 38u, 25u, 30u, 0u, 13u, 14u, 6u, 15u, 13u, 0u, 14u, 0u, 13u, 14u, 6u, 0u,
  8u, 5u, 16u, 0u, 13u, 14u, 6u, 0u, 13u, 14u, 6u, 0u, 13u, 14u, 6u,
};

static const mcotxt_uppercase_pair_t mcotxt_it_uppercase_map[35] = {
  { 0x0041u, 14u }, /* A -> a */
  { 0x0042u, 17u }, /* B -> b */
  { 0x0043u, 8u }, /* C -> c */
  { 0x0044u, 16u }, /* D -> d */
  { 0x0045u, 12u }, /* E -> e */
  { 0x0046u, 20u }, /* F -> f */
  { 0x0047u, 10u }, /* G -> g */
  { 0x0048u, 18u }, /* H -> h */
  { 0x0049u, 6u }, /* I -> i */
  { 0x004Au, 40u }, /* J -> j */
  { 0x004Bu, 28u }, /* K -> k */
  { 0x004Cu, 2u }, /* L -> l */
  { 0x004Du, 3u }, /* M -> m */
  { 0x004Eu, 15u }, /* N -> n */
  { 0x004Fu, 13u }, /* O -> o */
  { 0x0050u, 4u }, /* P -> p */
  { 0x0051u, 21u }, /* Q -> q */
  { 0x0052u, 7u }, /* R -> r */
  { 0x0053u, 9u }, /* S -> s */
  { 0x0054u, 1u }, /* T -> t */
  { 0x0055u, 5u }, /* U -> u */
  { 0x0056u, 11u }, /* V -> v */
  { 0x0057u, 27u }, /* W -> w */
  { 0x0058u, 42u }, /* X -> x */
  { 0x0059u, 33u }, /* Y -> y */
  { 0x005Au, 19u }, /* Z -> z */
  { 0x00C0u, 34u }, /* À -> à */
  { 0x00C8u, 22u }, /* È -> è */
  { 0x00C9u, 41u }, /* É -> é */
  { 0x00CCu, 35u }, /* Ì -> ì */
  { 0x00CDu, 43u }, /* Í -> í */
  { 0x00D2u, 37u }, /* Ò -> ò */
  { 0x00D3u, 44u }, /* Ó -> ó */
  { 0x00D9u, 39u }, /* Ù -> ù */
  { 0x00DAu, 45u }, /* Ú -> ú */
};

#endif /* MCOTXT_MODEL_IT_H */
