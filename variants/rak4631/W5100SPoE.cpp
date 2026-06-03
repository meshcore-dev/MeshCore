#ifdef WITH_W5100S_POE

#include <Arduino.h>
#include <nrf.h>
#include "W5100SPoE.h"

// ── Early power-rail + RST release (constructor priority 200) ────────────────
// Runs before setup(), right after SystemInit. Two jobs, as early as possible
// so the W5100S draws its full operating current before the RAK19018
// (Silvertel) converter folds back during PoE cold-start:
//
//   1. Drive PIN_3V3_EN (P1.02 / WB_IO2 / Arduino 34) HIGH — this is the
//      RAK19007 3.3 V PERIPHERAL POWER ENABLE that feeds the RAK13800/W5100S.
//      Meshtastic does this in initVariant(); stock MeshCore never did, so our
//      W5100S was only weakly powered via a default path (responds on USB but
//      can't pull its full ~130 mA on the marginal PoE rail).
//   2. Drive W5100S RST (P0.21 / WB_IO3 / Arduino 21) HIGH — out of reset.
//
// Raw registers because the Arduino GPIO layer isn't up this early.
static void __attribute__((constructor(200))) w5100s_early_power_init() {
    // P1.02 = 3V3_EN → HIGH (power the peripheral rail FIRST)
    NRF_P1->PIN_CNF[2] =
        (GPIO_PIN_CNF_DIR_Output       << GPIO_PIN_CNF_DIR_Pos) |
        (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos) |
        (GPIO_PIN_CNF_PULL_Disabled    << GPIO_PIN_CNF_PULL_Pos) |
        (GPIO_PIN_CNF_DRIVE_S0S1       << GPIO_PIN_CNF_DRIVE_Pos) |
        (GPIO_PIN_CNF_SENSE_Disabled   << GPIO_PIN_CNF_SENSE_Pos);
    NRF_P1->OUTSET = (1UL << 2);

    // P0.21 = W5100S RST → HIGH (release from reset)
    NRF_P0->PIN_CNF[21] =
        (GPIO_PIN_CNF_DIR_Output       << GPIO_PIN_CNF_DIR_Pos) |
        (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos) |
        (GPIO_PIN_CNF_PULL_Disabled    << GPIO_PIN_CNF_PULL_Pos) |
        (GPIO_PIN_CNF_DRIVE_S0S1       << GPIO_PIN_CNF_DRIVE_Pos) |
        (GPIO_PIN_CNF_SENSE_Disabled   << GPIO_PIN_CNF_SENSE_Pos);
    NRF_P0->OUTSET = (1UL << 21);
}

// ── Bit-banged SPI to the W5100S ────────────────────────────────────────────
// Shares the SPI bus (SCK=3 / MOSI=30 / MISO=29) with the SX1262 radio; only
// the chip-select differs (W5100S CS=26, SX1262 NSS=42). Bit-banged so it
// works regardless of which SPIClass owns the bus and runs before RadioLib.
// W5100S frame: write [0xF0][hi][lo][data], read [0x0F][hi][lo]->[data].
#define ETH_SCK_PIN   3
#define ETH_MOSI_PIN  30
#define ETH_MISO_PIN  29
#define LORA_NSS_PIN  42

static void bb_write_reg(uint16_t addr, uint8_t data) {
    const uint8_t frame[4] = { 0xF0, (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF), data };
    digitalWrite(W5100S_CS_PIN, LOW);
    for (uint8_t b = 0; b < 4; b++) {
        uint8_t v = frame[b];
        for (int8_t i = 7; i >= 0; i--) {
            digitalWrite(ETH_MOSI_PIN, (v >> i) & 1);
            digitalWrite(ETH_SCK_PIN, HIGH);
            digitalWrite(ETH_SCK_PIN, LOW);
        }
    }
    digitalWrite(W5100S_CS_PIN, HIGH);
}

static uint8_t bb_read_reg(uint16_t addr) {
    const uint8_t frame[3] = { 0x0F, (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF) };
    digitalWrite(W5100S_CS_PIN, LOW);
    for (uint8_t b = 0; b < 3; b++) {
        uint8_t v = frame[b];
        for (int8_t i = 7; i >= 0; i--) {
            digitalWrite(ETH_MOSI_PIN, (v >> i) & 1);
            digitalWrite(ETH_SCK_PIN, HIGH);
            digitalWrite(ETH_SCK_PIN, LOW);
        }
    }
    uint8_t out = 0;
    for (int8_t i = 7; i >= 0; i--) {
        digitalWrite(ETH_SCK_PIN, HIGH);
        out = (out << 1) | (digitalRead(ETH_MISO_PIN) & 1);
        digitalWrite(ETH_SCK_PIN, LOW);
    }
    digitalWrite(W5100S_CS_PIN, HIGH);
    return out;
}

// ── Full W5100S bring-up ────────────────────────────────────────────────────
// Called from RAK4631Board::begin(). Confirms the 3V3 rail + RST are driven
// (Arduino API, in case the core re-init touched them), then soft-resets and
// reads VERSIONR. Returns VERSIONR (0x51 = healthy W5100S).
uint8_t w5100s_poe_init() {
    // Make sure the peripheral power rail stays driven HIGH.
    pinMode(W5100S_3V3_EN_PIN, OUTPUT); digitalWrite(W5100S_3V3_EN_PIN, HIGH);
    delay(20);  // let the rail/W5100S settle after enable

    // Park both chip-selects HIGH on the shared bus, RST released.
    pinMode(LORA_NSS_PIN,   OUTPUT); digitalWrite(LORA_NSS_PIN,   HIGH);
    pinMode(W5100S_CS_PIN,  OUTPUT); digitalWrite(W5100S_CS_PIN,  HIGH);
    pinMode(W5100S_RST_PIN, OUTPUT); digitalWrite(W5100S_RST_PIN, HIGH);

    pinMode(ETH_SCK_PIN,  OUTPUT); digitalWrite(ETH_SCK_PIN, LOW);
    pinMode(ETH_MOSI_PIN, OUTPUT); digitalWrite(ETH_MOSI_PIN, LOW);
    pinMode(ETH_MISO_PIN, INPUT);

    bb_write_reg(0x0000, 0x80);   // MR: software reset
    delay(2);

    uint8_t ver = bb_read_reg(0x0080);   // VERSIONR (0x51 on W5100S)

    // Chip stays powered and out of reset; PHY auto-negotiates with the switch
    // and the W5100S draws its full operating current.
    return ver;
}

#endif // WITH_W5100S_POE
