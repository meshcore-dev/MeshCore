/* GENERATED FILE - DO NOT EDIT BY HAND. */
/* MCOtxt v1 static TOP-4 model (START + AFTER_PUNCT): UK (wire id 5). */
#ifndef MCOTXT_MODEL_UK_H
#define MCOTXT_MODEL_UK_H

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

static const uint8_t mcotxt_uk_available = 1u;
static const char mcotxt_uk_wire_hash[] = "949992c57156526843cdb9634c5c14bee23218878f116ef90be2a8497274c131";
static const uint8_t mcotxt_uk_language_id = 5u;
static const uint8_t mcotxt_uk_primary_count = 32u;
static const uint8_t mcotxt_uk_extension_count = 12u;
static const uint8_t mcotxt_uk_symbol_count = 44u;
static const uint8_t mcotxt_uk_uppercase_count = 33u;

static const uint16_t mcotxt_uk_primary_symbols[32] = {
  0x0020u, 0x043Au, 0x0441u, 0x0440u, 0x0456u, 0x043Cu, 0x0443u, 0x043Du, 0x0437u, 0x0431u,
  0x043Bu, 0x0434u, 0x043Eu, 0x0438u, 0x044Fu, 0x0430u, 0x0435u, 0x0442u, 0x0432u, 0x0448u,
  0x0447u, 0x0439u, 0x0433u, 0x043Fu, 0x0436u, 0x044Eu, 0x0446u, 0x0445u, 0x0454u, 0x0449u,
  0x0444u, 0x0457u,
};

static const uint16_t mcotxt_uk_extension_symbols[12] = {
  0x0032u, 0x0033u, 0x044Cu, 0x0031u, 0x0034u, 0x0035u, 0x0038u, 0x0036u, 0x0037u, 0x0030u,
  0x0039u, 0x0491u,
};

static const uint8_t mcotxt_uk_start_top4[4] = {
  0u, 17u, 7u, 11u,
};

static const uint8_t mcotxt_uk_punct_start_top4[4] = {
  0u, 37u, 41u, 35u,
};

static const uint8_t mcotxt_uk_top4[176] = {
  23u, 18u, 7u, 17u, 12u, 15u, 13u, 6u, 17u, 34u, 14u, 23u, 12u, 15u, 16u, 13u, 0u, 11u, 7u, 10u,
  12u, 15u, 0u, 16u, 0u, 18u, 17u, 10u, 15u, 12u, 16u, 4u, 15u, 0u, 7u, 4u, 6u, 15u, 12u, 4u, 16u,
  34u, 13u, 14u, 12u, 15u, 16u, 6u, 0u, 5u, 18u, 11u, 0u, 18u, 17u, 2u, 0u, 1u, 17u, 18u, 0u, 7u,
  17u, 10u, 0u, 7u, 3u, 5u, 15u, 13u, 12u, 34u, 0u, 15u, 13u, 4u, 13u, 12u, 16u, 6u, 13u, 15u, 16u,
  12u, 0u, 7u, 17u, 12u, 12u, 15u, 4u, 3u, 3u, 12u, 4u, 16u, 16u, 7u, 0u, 13u, 0u, 28u, 17u, 18u,
  16u, 4u, 25u, 14u, 0u, 12u, 13u, 17u, 0u, 17u, 5u, 11u, 12u, 16u, 15u, 6u, 4u, 12u, 15u, 17u, 0u,
  27u, 31u, 7u, 0u, 32u, 39u, 41u, 0u, 32u, 33u, 41u, 0u, 7u, 1u, 2u, 32u, 41u, 0u, 37u, 0u, 33u,
  40u, 41u, 0u, 18u, 41u, 32u, 41u, 0u, 39u, 32u, 38u, 0u, 32u, 33u, 0u, 41u, 1u, 5u, 0u, 41u, 1u,
  5u, 39u, 0u, 42u, 22u, 0u, 12u, 15u, 7u,
};

static const mcotxt_uppercase_pair_t mcotxt_uk_uppercase_map[33] = {
  { 0x0404u, 28u }, /* Є -> є */
  { 0x0406u, 4u }, /* І -> і */
  { 0x0407u, 31u }, /* Ї -> ї */
  { 0x0410u, 15u }, /* А -> а */
  { 0x0411u, 9u }, /* Б -> б */
  { 0x0412u, 18u }, /* В -> в */
  { 0x0413u, 22u }, /* Г -> г */
  { 0x0414u, 11u }, /* Д -> д */
  { 0x0415u, 16u }, /* Е -> е */
  { 0x0416u, 24u }, /* Ж -> ж */
  { 0x0417u, 8u }, /* З -> з */
  { 0x0418u, 13u }, /* И -> и */
  { 0x0419u, 21u }, /* Й -> й */
  { 0x041Au, 1u }, /* К -> к */
  { 0x041Bu, 10u }, /* Л -> л */
  { 0x041Cu, 5u }, /* М -> м */
  { 0x041Du, 7u }, /* Н -> н */
  { 0x041Eu, 12u }, /* О -> о */
  { 0x041Fu, 23u }, /* П -> п */
  { 0x0420u, 3u }, /* Р -> р */
  { 0x0421u, 2u }, /* С -> с */
  { 0x0422u, 17u }, /* Т -> т */
  { 0x0423u, 6u }, /* У -> у */
  { 0x0424u, 30u }, /* Ф -> ф */
  { 0x0425u, 27u }, /* Х -> х */
  { 0x0426u, 26u }, /* Ц -> ц */
  { 0x0427u, 20u }, /* Ч -> ч */
  { 0x0428u, 19u }, /* Ш -> ш */
  { 0x0429u, 29u }, /* Щ -> щ */
  { 0x042Cu, 34u }, /* Ь -> ь */
  { 0x042Eu, 25u }, /* Ю -> ю */
  { 0x042Fu, 14u }, /* Я -> я */
  { 0x0490u, 43u }, /* Ґ -> ґ */
};

#endif /* MCOTXT_MODEL_UK_H */
