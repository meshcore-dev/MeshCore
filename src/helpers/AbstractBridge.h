#pragma once

#include <Mesh.h>

class AbstractBridge {
public:
  virtual ~AbstractBridge() {}

  /**
   * @brief Initializes the bridge.
   */
  virtual void begin() = 0;

  /**
   * @brief Stops the bridge.
   */
  virtual void end() = 0;

  /**
   * @brief Gets the current state of the bridge.
   *
   * @return true if the bridge is initialized and running, false otherwise.
   */
  virtual bool isRunning() const = 0;

  /**
   * @brief A method to be called on every main loop iteration.
   *        Used for tasks like checking for incoming data.
   */
  virtual void loop() = 0;

  /**
   * @brief A callback that is triggered when the mesh transmits a packet.
   *        The bridge can use this to forward the packet.
   *
   * @param packet The packet that was transmitted.
   */
  virtual void sendPacket(mesh::Packet* packet) = 0;

  /**
   * @brief Processes a received packet from the bridge's medium.
   *
   * @param packet The packet that was received.
   */
  virtual void onPacketReceived(mesh::Packet* packet) = 0;

  /**
   * @brief Offers an outbound packet to the bridge, immediately before the radio
   *        would transmit it. A bridge that can reach the packet's destination
   *        over its own medium sends it there and answers true, so the radio
   *        never pays airtime for a packet the local lane already carried.
   *
   *        A bridge that answers false may still mirror the packet (so a nearby
   *        peer gets a fast copy) - the radio remains the transport of record.
   *
   * @param packet The packet about to be handed to the radio.
   * @returns true if the bridge has sent the packet and the radio must not.
   */
  virtual bool claimOutboundPacket(mesh::Packet* packet) { return false; }

  /**
   * @brief Called when a packet this bridge injected has been processed by the
   *        mesh. Lets the bridge report the injected path's real latency.
   *
   * @param packet The packet that was processed.
   */
  virtual void onInboundPacketProcessed(mesh::Packet* packet) { }

  /**
   * @brief Writes this bridge's counters in the host's bridge-stats frame.
   *
   * @param dest Destination buffer, after the stats response header.
   * @param max_len Number of bytes available at dest.
   * @returns Number of bytes written, or 0 if this bridge keeps no counters.
   */
  virtual size_t writeStats(uint8_t* dest, size_t max_len) { return 0; }
};
