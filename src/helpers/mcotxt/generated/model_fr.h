/* GENERATED FILE - DO NOT EDIT BY HAND. */
/* MCOtxt v1 static TOP-4 model (START + AFTER_PUNCT): FR (wire id 2). */
#ifndef MCOTXT_MODEL_FR_H
#define MCOTXT_MODEL_FR_H

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

static const uint8_t mcotxt_fr_available = 1u;
static const char mcotxt_fr_wire_hash[] = "910c5821a202a9b35313a9b939355f09d1c4293fe2ed246fed0123639d379ccb";
static const uint8_t mcotxt_fr_language_id = 2u;
static const uint8_t mcotxt_fr_primary_count = 32u;
static const uint8_t mcotxt_fr_extension_count = 21u;
static const uint8_t mcotxt_fr_symbol_count = 53u;
static const uint8_t mcotxt_fr_uppercase_count = 42u;

static const uint16_t mcotxt_fr_primary_symbols[32] = {
  0x0020u, 0x0075u, 0x0074u, 0x0063u, 0x006Du, 0x0072u, 0x006Cu, 0x006Fu, 0x0069u, 0x0065u,
  0x0061u, 0x0073u, 0x006Eu, 0x0076u, 0x0070u, 0x00E9u, 0x006Au, 0x0062u, 0x0064u, 0x0067u,
  0x0068u, 0x0066u, 0x0071u, 0x0079u, 0x00E7u, 0x00E0u, 0x006Bu, 0x0032u, 0x0078u, 0x0031u,
  0x0035u, 0x007Au,
};

static const uint16_t mcotxt_fr_extension_symbols[21] = {
  0x0034u, 0x0077u, 0x0033u, 0x0037u, 0x0036u, 0x00E8u, 0x0030u, 0x0038u, 0x00EAu, 0x0039u,
  0x00F4u, 0x00EEu, 0x00F9u, 0x00FBu, 0x00FCu, 0x00EBu, 0x0153u, 0x00E2u, 0x00E6u, 0x00EFu,
  0x00FFu,
};

static const uint8_t mcotxt_fr_start_top4[4] = {
  17u, 11u, 2u, 3u,
};

static const uint8_t mcotxt_fr_punct_start_top4[4] = {
  0u, 10u, 9u, 12u,
};

static const uint8_t mcotxt_fr_top4[212] = {
  18u, 14u, 6u, 11u, 5u, 0u, 2u, 8u, 0u, 9u, 8u, 7u, 7u, 20u, 9u, 10u, 9u, 10u, 7u, 8u, 0u, 9u,
  10u, 7u, 9u, 10u, 6u, 0u, 12u, 1u, 8u, 5u, 0u, 11u, 12u, 5u, 0u, 11u, 12u, 5u, 8u, 0u, 12u, 11u,
  0u, 2u, 9u, 10u, 0u, 2u, 9u, 11u, 9u, 7u, 8u, 10u, 10u, 7u, 9u, 5u, 2u, 0u, 9u, 14u, 9u, 7u, 1u,
  10u, 7u, 8u, 6u, 9u, 9u, 8u, 0u, 1u, 9u, 5u, 10u, 12u, 9u, 7u, 10u, 0u, 10u, 5u, 8u, 7u, 1u, 0u,
  22u, 5u, 0u, 9u, 7u, 2u, 10u, 1u, 7u, 0u, 0u, 9u, 11u, 10u, 0u, 9u, 10u, 8u, 0u, 38u, 29u, 41u,
  0u, 9u, 2u, 7u, 0u, 34u, 29u, 35u, 0u, 38u, 41u, 27u, 0u, 10u, 9u, 8u, 0u, 41u, 32u, 34u, 8u, 9u,
  10u, 7u, 0u, 34u, 32u, 39u, 34u, 29u, 38u, 0u, 0u, 39u, 27u, 32u, 11u, 5u, 2u, 4u, 0u, 38u, 36u,
  35u, 0u, 36u, 4u, 21u, 2u, 4u, 13u, 0u, 0u, 10u, 21u, 9u, 2u, 6u, 14u, 0u, 12u, 2u, 6u, 3u, 0u,
  9u, 11u, 10u, 5u, 0u, 9u, 11u, 5u, 0u, 9u, 11u, 6u, 0u, 9u, 11u, 8u, 0u, 9u, 11u, 0u, 9u, 11u,
  10u, 0u, 9u, 11u, 10u, 0u, 9u, 11u, 10u, 0u, 9u, 11u, 10u,
};

static const mcotxt_uppercase_pair_t mcotxt_fr_uppercase_map[42] = {
  { 0x0041u, 10u }, /* A -> a */
  { 0x0042u, 17u }, /* B -> b */
  { 0x0043u, 3u }, /* C -> c */
  { 0x0044u, 18u }, /* D -> d */
  { 0x0045u, 9u }, /* E -> e */
  { 0x0046u, 21u }, /* F -> f */
  { 0x0047u, 19u }, /* G -> g */
  { 0x0048u, 20u }, /* H -> h */
  { 0x0049u, 8u }, /* I -> i */
  { 0x004Au, 16u }, /* J -> j */
  { 0x004Bu, 26u }, /* K -> k */
  { 0x004Cu, 6u }, /* L -> l */
  { 0x004Du, 4u }, /* M -> m */
  { 0x004Eu, 12u }, /* N -> n */
  { 0x004Fu, 7u }, /* O -> o */
  { 0x0050u, 14u }, /* P -> p */
  { 0x0051u, 22u }, /* Q -> q */
  { 0x0052u, 5u }, /* R -> r */
  { 0x0053u, 11u }, /* S -> s */
  { 0x0054u, 2u }, /* T -> t */
  { 0x0055u, 1u }, /* U -> u */
  { 0x0056u, 13u }, /* V -> v */
  { 0x0057u, 33u }, /* W -> w */
  { 0x0058u, 28u }, /* X -> x */
  { 0x0059u, 23u }, /* Y -> y */
  { 0x005Au, 31u }, /* Z -> z */
  { 0x00C0u, 25u }, /* À -> à */
  { 0x00C2u, 49u }, /* Â -> â */
  { 0x00C6u, 50u }, /* Æ -> æ */
  { 0x00C7u, 24u }, /* Ç -> ç */
  { 0x00C8u, 37u }, /* È -> è */
  { 0x00C9u, 15u }, /* É -> é */
  { 0x00CAu, 40u }, /* Ê -> ê */
  { 0x00CBu, 47u }, /* Ë -> ë */
  { 0x00CEu, 43u }, /* Î -> î */
  { 0x00CFu, 51u }, /* Ï -> ï */
  { 0x00D4u, 42u }, /* Ô -> ô */
  { 0x00D9u, 44u }, /* Ù -> ù */
  { 0x00DBu, 45u }, /* Û -> û */
  { 0x00DCu, 46u }, /* Ü -> ü */
  { 0x0152u, 48u }, /* Œ -> œ */
  { 0x0178u, 52u }, /* Ÿ -> ÿ */
};

#endif /* MCOTXT_MODEL_FR_H */
