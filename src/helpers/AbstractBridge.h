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
};
