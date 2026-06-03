#pragma once

// W5100S activation for PoE operation on RAK10720 (RAK4631 + RAK13800 + RAK19018).
//
// ROOT CAUSE (confirmed via RAK forum + Meshtastic rak4631_eth_gw variant):
// The RAK19018 PoE module (Silvertel Ag9905MT) enters a non-continuous
// "gated pulse" mode when the load is below ~125-200 mA. A bare MeshCore
// repeater draws only a few mA, so the converter never latches → the supply
// pulses and the device never boots (fade-in / brighten / die / repeat LED).
//
// The fix that lets Meshtastic boot on PoE without a battery: bring the
// W5100S PHY into its active state. An active W5100S draws ~120 mA — enough
// to keep the Silvertel converter latched in continuous mode.
//
// The W5100S has no power-enable pin on this board (always powered), but its
// RST must be HIGH for the PHY to run. We release RST as early as possible
// (before setup(), via a constructor) so the PHY draws current and latches
// the converter before its foldback timer expires.
//
// Pin mapping (from Meshtastic rak4631_eth_gw variant.h):
//   RST → PIN_ETHERNET_RESET = 21 (P0.21 / WB_IO3)
//   CS  → PIN_ETHERNET_SS    = 26 (WB_SPI_CS)
//   SPI → SPI1 (SCK=3, MISO=29, MOSI=30)   [not used in Layer 1]

#ifndef W5100S_RST_PIN
  #define W5100S_RST_PIN  21   // P0.21 / WB_IO3
#endif

#ifndef W5100S_CS_PIN
  #define W5100S_CS_PIN   26   // WB_SPI_CS
#endif

// RAK19007 3.3 V peripheral power enable (feeds the RAK13800/W5100S).
// Must be driven HIGH or the W5100S is only weakly powered — exactly what
// Meshtastic's initVariant() does and stock MeshCore omits.
#ifndef W5100S_3V3_EN_PIN
  #define W5100S_3V3_EN_PIN  34   // P1.02 / WB_IO2
#endif

// Called from RAK4631Board::begin(); fully brings up the W5100S (soft reset +
// activation) so it draws full operating current. Returns VERSIONR (0x51 = ok).
uint8_t w5100s_poe_init();
