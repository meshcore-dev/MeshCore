#ifdef WITH_W5100S_POE

#include <Arduino.h>
#include <nrf.h>
#include "W5100SPoE.h"

// ── Early RST release (constructor priority 200) ─────────────────────────────
// Runs before setup(), right after SystemInit — earlier than the ETHERNET_ENABLED
// constructor in RAK4631Board.cpp that powers the peripheral rail (WB_IO2,
// priority 102 runs first) but well before setup()/board.begin(), so the
// W5100S starts drawing its full operating current as early as possible,
// before the RAK19018 (Silvertel) converter's foldback timer expires.
//
// Raw registers because the Arduino GPIO layer isn't up this early.
static void __attribute__((constructor(200))) w5100s_early_rst_release() {
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
// Called from RAK4631Board::begin(). Confirms RST is driven (Arduino API, in
// case the core re-init touched it), then soft-resets and reads VERSIONR.
// Returns VERSIONR (0x51 = healthy W5100S).
uint8_t w5100s_poe_init() {
    delay(20);  // let the rail/W5100S settle after the ETHERNET_ENABLED ctor's WB_IO2 enable

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
