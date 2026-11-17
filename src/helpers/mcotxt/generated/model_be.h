/* GENERATED FILE - DO NOT EDIT BY HAND. */
/* MCOtxt v1 static TOP-4 model (START + AFTER_PUNCT): BE (wire id 6). */
#ifndef MCOTXT_MODEL_BE_H
#define MCOTXT_MODEL_BE_H

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

static const uint8_t mcotxt_be_available = 1u;
static const char mcotxt_be_wire_hash[] = "dd23c2b82288e0a4452f237ccd905055974779e3cc5cc6f3d03be1b24e5bb1fd";
static const uint8_t mcotxt_be_language_id = 6u;
static const uint8_t mcotxt_be_primary_count = 32u;
static const uint8_t mcotxt_be_extension_count = 11u;
static const uint8_t mcotxt_be_symbol_count = 43u;
static const uint8_t mcotxt_be_uppercase_count = 32u;

static const uint16_t mcotxt_be_primary_symbols[32] = {
  0x0020u, 0x043Au, 0x043Bu, 0x043Cu, 0x0441u, 0x0435u, 0x0440u, 0x0432u, 0x043Du, 0x044Fu,
  0x0434u, 0x0443u, 0x0442u, 0x0447u, 0x043Eu, 0x043Fu, 0x0437u, 0x044Bu, 0x0439u, 0x0433u,
  0x0431u, 0x0436u, 0x0430u, 0x0448u, 0x044Eu, 0x0445u, 0x044Cu, 0x044Du, 0x0032u, 0x0444u,
  0x0446u, 0x0031u,
};

static const uint16_t mcotxt_be_extension_symbols[11] = {
  0x0034u, 0x0451u, 0x0033u, 0x0036u, 0x0038u, 0x0035u, 0x0037u, 0x0030u, 0x0039u, 0x0456u,
  0x045Eu,
};

static const uint8_t mcotxt_be_start_top4[4] = {
  0u, 12u, 8u, 5u,
};

static const uint8_t mcotxt_be_punct_start_top4[4] = {
  0u, 12u, 28u, 38u,
};

static const uint8_t mcotxt_be_top4[172] = {
  8u, 15u, 7u, 4u, 22u, 14u, 0u, 2u, 14u, 26u, 0u, 22u, 14u, 0u, 5u, 22u, 12u, 0u, 5u, 14u, 0u,
  12u, 8u, 6u, 14u, 22u, 5u, 11u, 0u, 22u, 5u, 14u, 22u, 14u, 5u, 11u, 0u, 12u, 16u, 8u, 5u, 14u,
  22u, 11u, 0u, 12u, 21u, 4u, 14u, 22u, 0u, 26u, 5u, 22u, 12u, 8u, 0u, 10u, 20u, 12u, 14u, 6u, 22u,
  5u, 22u, 0u, 8u, 14u, 0u, 18u, 2u, 12u, 0u, 12u, 1u, 4u, 14u, 6u, 10u, 22u, 14u, 17u, 11u, 6u,
  5u, 8u, 11u, 22u, 0u, 2u, 4u, 12u, 5u, 26u, 22u, 12u, 13u, 0u, 12u, 10u, 14u, 0u, 22u, 16u, 0u,
  8u, 23u, 12u, 12u, 1u, 29u, 7u, 0u, 28u, 39u, 32u, 14u, 22u, 29u, 11u, 5u, 22u, 7u, 17u, 39u,
  37u, 0u, 31u, 0u, 37u, 36u, 39u, 0u, 12u, 3u, 14u, 0u, 39u, 28u, 32u, 0u, 36u, 38u, 40u, 35u, 0u,
  37u, 14u, 0u, 39u, 35u, 4u, 0u, 35u, 28u, 37u, 39u, 0u, 28u, 37u, 39u, 0u, 3u, 34u, 0u, 14u, 5u,
  22u, 0u, 14u, 5u, 22u,
};

static const mcotxt_uppercase_pair_t mcotxt_be_uppercase_map[32] = {
  { 0x0401u, 33u }, /* Ё -> ё */
  { 0x0406u, 41u }, /* І -> і */
  { 0x040Eu, 42u }, /* Ў -> ў */
  { 0x0410u, 22u }, /* А -> а */
  { 0x0411u, 20u }, /* Б -> б */
  { 0x0412u, 7u }, /* В -> в */
  { 0x0413u, 19u }, /* Г -> г */
  { 0x0414u, 10u }, /* Д -> д */
  { 0x0415u, 5u }, /* Е -> е */
  { 0x0416u, 21u }, /* Ж -> ж */
  { 0x0417u, 16u }, /* З -> з */
  { 0x0419u, 18u }, /* Й -> й */
  { 0x041Au, 1u }, /* К -> к */
  { 0x041Bu, 2u }, /* Л -> л */
  { 0x041Cu, 3u }, /* М -> м */
  { 0x041Du, 8u }, /* Н -> н */
  { 0x041Eu, 14u }, /* О -> о */
  { 0x041Fu, 15u }, /* П -> п */
  { 0x0420u, 6u }, /* Р -> р */
  { 0x0421u, 4u }, /* С -> с */
  { 0x0422u, 12u }, /* Т -> т */
  { 0x0423u, 11u }, /* У -> у */
  { 0x0424u, 29u }, /* Ф -> ф */
  { 0x0425u, 25u }, /* Х -> х */
  { 0x0426u, 30u }, /* Ц -> ц */
  { 0x0427u, 13u }, /* Ч -> ч */
  { 0x0428u, 23u }, /* Ш -> ш */
  { 0x042Bu, 17u }, /* Ы -> ы */
  { 0x042Cu, 26u }, /* Ь -> ь */
  { 0x042Du, 27u }, /* Э -> э */
  { 0x042Eu, 24u }, /* Ю -> ю */
  { 0x042Fu, 9u }, /* Я -> я */
};

#endif /* MCOTXT_MODEL_BE_H */
