/*
 * Zephyr SerialBLEInterface: MeshCore's BaseSerialInterface over the Zephyr NUS
 * (bt_nus) + the step-2 bonded pairing (LE SC + MITM + fixed PIN). This is the
 * serial seam the companion role will use; the companion-protocol bytes ride
 * the encrypted, bonded NUS link that the bare-metal core could never establish.
 */
#pragma once
#include <Arduino.h>
#include <helpers/BaseSerialInterface.h>
#include <zephyr/kernel.h>

class SerialBLEInterface : public BaseSerialInterface {
  public:
	/* prefix+name -> advertised name; pin_code = fixed BLE passkey. */
	void begin(const char *device_name, uint32_t pin_code);

	/* Apply (or re-apply at runtime) the BLE name: sets the GAP Device Name characteristic
	 * AND the advertising payload, so a rename takes effect on the next advertise without a
	 * reboot. Safe to call while connected (the new name is used on the next re-advertise). */
	void setDeviceName(const char *device_name);

	void enable() override;
	void disable() override;
	bool isEnabled() const override { return _enabled; }
	bool isConnected() const override;
	bool isWriteBusy() const override;
	size_t writeFrame(const uint8_t src[], size_t len) override;
	size_t checkRecvFrame(uint8_t dest[]) override;

	/* invoked from the C BLE callbacks (see .cpp) */
	void _onConnect(void *conn);
	void _onDisconnect(void *conn);
	void _onSecured(void *conn, bool ok);
	void _onRx(const void *data, uint16_t len);

  private:
	struct Frame {
		uint16_t len;
		uint8_t buf[MAX_FRAME_SIZE];
	};
	static const uint8_t QSZ = 12;

	Frame _send[QSZ];
	uint8_t _send_len = 0;          /* main-thread only */
	Frame _recv[QSZ];
	uint8_t _recv_len = 0;          /* filled in BT ctx, read in main -> guarded by _lock */
	struct k_mutex _lock;

	void *_conn = nullptr;          /* struct bt_conn* */
	bool _enabled = false;
	bool _secured = false;
	bool _advertising = false;
	unsigned long _last_adv_try = 0;

	void shiftSend();
};

extern SerialBLEInterface ble;
