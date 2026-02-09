#pragma once

#include "Mesh.h"

class StatsFormatHelper {
public:
  static void formatCoreStats(char* reply, 
                             mesh::MainBoard& board, 
                             mesh::MillisecondClock& ms, 
                             uint16_t err_flags,
                             mesh::PacketManager* mgr) {
    sprintf(reply, 
      "{\"battery_mv\":%u,\"uptime_secs\":%u,\"errors\":%u,\"queue_len\":%u}",
      board.getBattMilliVolts(),
      ms.getMillis() / 1000,
      err_flags,
      mgr->getOutboundCount(0xFFFFFFFF)
    );
  }

  template<typename RadioDriverType>
  static void formatRadioStats(char* reply,
                              mesh::Radio* radio,
                              RadioDriverType& driver,
                              uint32_t total_air_time_ms,
                              uint32_t total_rx_air_time_ms) {
    sprintf(reply, 
      "{\"noise_floor\":%d,\"last_rssi\":%d,\"last_snr\":%.2f,\"tx_air_secs\":%u,\"rx_air_secs\":%u}",
      (int16_t)radio->getNoiseFloor(),
      (int16_t)driver.getLastRSSI(),
      driver.getLastSNR(),
      total_air_time_ms / 1000,
      total_rx_air_time_ms / 1000
    );
  }

  template<typename RadioDriverType>
  static void formatPacketStats(char* reply,
                               RadioDriverType& driver,
                               uint32_t n_sent_flood,
                               uint32_t n_sent_direct,
                               uint32_t n_recv_flood,
                               uint32_t n_recv_direct) {
    sprintf(reply, 
      "{\"recv\":%u,\"sent\":%u,\"flood_tx\":%u,\"direct_tx\":%u,\"flood_rx\":%u,\"direct_rx\":%u,\"recv_errors\":%u}",
      driver.getPacketsRecv(),
      driver.getPacketsSent(),
      n_sent_flood,
      n_sent_direct,
      n_recv_flood,
      n_recv_direct,
      driver.getPacketsRecvErrors()
    );
  }
};
