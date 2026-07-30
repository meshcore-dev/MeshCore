#include "serial_ble_interface.h"
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/settings/settings.h>
#include <string.h>

SerialBLEInterface ble;

#ifndef BLE_DBG
#define BLE_DBG(...) printk("BLE: " __VA_ARGS__)
#endif

#ifndef MC_BLE_VERBOSE
#define MC_BLE_VERBOSE 1     /* dump every companion frame in/out */
#endif

#if MC_BLE_VERBOSE
static void mc_dump(const char *dir, const uint8_t *b, uint16_t len)
{
	printk("[%8lld] %s code=%-3u len=%-3u :", (long long)k_uptime_get(), dir, len ? b[0] : 0, len);
	for (uint16_t i = 0; i < len && i < 28; i++) printk(" %02x", b[i]);
	printk("%s\n", len > 28 ? " ..." : "");
}
#else
#define mc_dump(d, b, l)
#endif

/* advertising payload: name + NUS 128-bit UUID (so the MeshCore app finds us).
 * g_ad[1] (the scan-list name) is rewritten at begin() from the runtime node name;
 * CONFIG_BT_DEVICE_NAME is only a placeholder. bt_set_name() sets the GAP Device Name
 * *characteristic* but NOT this advertising payload, so without the rewrite the node
 * always advertised the static "MeshCore" regardless of the configured node name. */
static char g_adv_name[27];   /* 31B adv - 3B flags - 2B AD header => 26 name chars + NUL */
static struct bt_data g_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};
static const struct bt_data g_sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL,
		BT_UUID_128_ENCODE(0x6E400001, 0xB5A3, 0xF393, 0xE0A9, 0xE50E24DCCA9E)),
};

/* custom Nordic-UART service, ENCRYPTION-GATED so the *central* drives pairing
 * Zephyr's stock bt_nus characteristics are PERM_NONE, which forced us to send a peripheral
 * SMP Security Request on connect; iOS/Windows respond to that by pairing then dropping &
 * reconnecting. Gating the RX-write and the TX CCC on *authenticated* encryption makes the
 * central initiate MITM (passkey) pairing on first access and stay on the same link. */
static struct bt_uuid_128 nus_srv_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x6E400001, 0xB5A3, 0xF393, 0xE0A9, 0xE50E24DCCA9E));
static struct bt_uuid_128 nus_tx_uuid = BT_UUID_INIT_128(   /* notify: NODE->APP */
	BT_UUID_128_ENCODE(0x6E400003, 0xB5A3, 0xF393, 0xE0A9, 0xE50E24DCCA9E));
static struct bt_uuid_128 nus_rx_uuid = BT_UUID_INIT_128(   /* write:  APP->NODE */
	BT_UUID_128_ENCODE(0x6E400002, 0xB5A3, 0xF393, 0xE0A9, 0xE50E24DCCA9E));

static ssize_t nus_rx_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			    const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	(void)conn; (void)attr; (void)offset; (void)flags;
	ble._onRx(buf, len);
	return len;
}
static void nus_tx_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) { (void)attr; (void)value; }

/* attrs[0]=svc [1]=TX chrc [2]=TX value [3]=CCC [4]=RX chrc [5]=RX value;
 * notify on attrs[1] (mirrors Zephyr's bt_nus_send). */
BT_GATT_SERVICE_DEFINE(nus_svc,
	BT_GATT_PRIMARY_SERVICE(&nus_srv_uuid),
	BT_GATT_CHARACTERISTIC(&nus_tx_uuid.uuid, BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE, NULL, NULL, NULL),
	BT_GATT_CCC(nus_tx_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_AUTHEN),
	BT_GATT_CHARACTERISTIC(&nus_rx_uuid.uuid,
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE_AUTHEN, NULL, nus_rx_write, NULL),
);

/* C BLE callbacks bridging to the single `ble` instance */
static void cb_connected(struct bt_conn *conn, uint8_t err)
{
	if (err) { BLE_DBG("connect failed 0x%02x\n", err); return; }
	BLE_DBG("connected\n");
	ble._onConnect(conn);
	/* Do NOT initiate security here. The NUS characteristics are gated on authenticated
	 * encryption, so the CENTRAL triggers MITM pairing on first access and keeps the link.
	 * A peripheral-initiated Security Request is what made iOS/Windows pair-then-reconnect. */
}
static void cb_disconnected(struct bt_conn *conn, uint8_t reason)
{
	BLE_DBG("disconnected 0x%02x\n", reason);
	ble._onDisconnect(conn);
}
static void cb_security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	BLE_DBG("security level %d err %d\n", level, err);
	ble._onSecured(conn, err == BT_SECURITY_ERR_SUCCESS && level >= BT_SECURITY_L2);
}
BT_CONN_CB_DEFINE(conn_cbs) = {
	.connected = cb_connected,
	.disconnected = cb_disconnected,
	.security_changed = cb_security_changed,
};

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	BLE_DBG(">>> enter PASSKEY on phone: %06u <<<\n", passkey);
}
static void auth_cancel(struct bt_conn *conn) { BLE_DBG("pairing cancelled\n"); }
static struct bt_conn_auth_cb auth_cb = {
	.passkey_display = auth_passkey_display,
	.cancel = auth_cancel,
};
static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	BLE_DBG("*** PAIRING COMPLETE bonded=%d ***\n", bonded);
}
static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	BLE_DBG("!!! PAIRING FAILED reason=%d !!!\n", reason);
}
static struct bt_conn_auth_info_cb auth_info_cb = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

/* --- SerialBLEInterface impl --- */
void SerialBLEInterface::begin(const char *device_name, uint32_t pin_code)
{
	k_mutex_init(&_lock);
	int err = bt_enable(NULL);
	if (err) { BLE_DBG("bt_enable %d\n", err); return; }

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();   /* restore bonds saved by CONFIG_BT_SETTINGS */
	}
	bt_passkey_set(pin_code);
	bt_conn_auth_cb_register(&auth_cb);
	bt_conn_auth_info_cb_register(&auth_info_cb);
	setDeviceName(device_name);
	BLE_DBG("init done; advertising as '%s' pin %06u\n", bt_get_name(), pin_code);
}

void SerialBLEInterface::setDeviceName(const char *device_name)
{
	if (!device_name || !*device_name) return;
	bt_set_name(device_name);   /* GAP Device Name characteristic (read after connect) */
	/* Mirror the name into the advertising payload (the scan-list name the phone shows),
	 * truncating to the 26-char adv budget. Without this the scan list always read
	 * "MeshCore", and a runtime rename never reached the advertisement at all. The new
	 * g_ad[] is picked up by the next mc_start_adv(); if idle, refresh the live advertisement now. */
	size_t n = strlen(device_name);
	bool shortened = n > sizeof(g_adv_name) - 1;
	if (shortened) n = sizeof(g_adv_name) - 1;
	memcpy(g_adv_name, device_name, n);
	g_adv_name[n] = '\0';
	g_ad[1].type = shortened ? BT_DATA_NAME_SHORTENED : BT_DATA_NAME_COMPLETE;
	g_ad[1].data = (const uint8_t *)g_adv_name;
	g_ad[1].data_len = n;
	BLE_DBG("device name set to '%s'\n", g_adv_name);
	if (_enabled && _conn == nullptr && _advertising) {
		bt_le_adv_update_data(g_ad, ARRAY_SIZE(g_ad), g_sd, ARRAY_SIZE(g_sd));
	}
}

static int mc_start_adv(void)
{
	return bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN,
					       BT_GAP_ADV_FAST_INT_MIN_2,
					       BT_GAP_ADV_FAST_INT_MAX_2, NULL),
			       g_ad, ARRAY_SIZE(g_ad), g_sd, ARRAY_SIZE(g_sd));
}

void SerialBLEInterface::enable()
{
	if (_enabled) return;
	_enabled = true;
	int err = mc_start_adv();
	_advertising = (err == 0);
	BLE_DBG("adv start -> %d\n", err);
}

void SerialBLEInterface::disable()
{
	_enabled = false;
	bt_le_adv_stop();
	if (_conn) bt_conn_disconnect((struct bt_conn *)_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

bool SerialBLEInterface::isConnected() const { return _conn != nullptr && _secured; }
bool SerialBLEInterface::isWriteBusy() const { return _send_len >= (QSZ * 2 / 3); }

void SerialBLEInterface::_onConnect(void *conn)
{
	_conn = bt_conn_ref((struct bt_conn *)conn);
	_secured = false;
	_advertising = false;   /* Zephyr stops connectable adv once a connection forms */
	_send_len = 0;
	k_mutex_lock(&_lock, K_FOREVER); _recv_len = 0; k_mutex_unlock(&_lock);
}
void SerialBLEInterface::_onDisconnect(void *conn)
{
	if (_conn) { bt_conn_unref((struct bt_conn *)_conn); _conn = nullptr; }
	_secured = false;
	/* re-advertise is handled in checkRecvFrame() (main thread) avoid BT calls here */
}
void SerialBLEInterface::_onSecured(void *conn, bool ok)
{
	(void)conn;
	_secured = ok;
	/* NOTE: previously requested a conn interval here for Android latency, but an
	 * unsolicited param update right after pairing stalls iOS .
	 * Let the central manage connection parameters. */
}

void SerialBLEInterface::_onRx(const void *data, uint16_t len)
{
	if (len == 0 || len > MAX_FRAME_SIZE) return;
	mc_dump("APP->NODE", (const uint8_t *)data, len);
	k_mutex_lock(&_lock, K_FOREVER);
	if (_recv_len < QSZ) {
		_recv[_recv_len].len = len;
		memcpy(_recv[_recv_len].buf, data, len);
		_recv_len++;
	} else {
		printk("BLE: RX queue FULL, dropping frame\n");
	}
	k_mutex_unlock(&_lock);
}

void SerialBLEInterface::shiftSend()
{
	if (_send_len > 0) {
		_send_len--;
		for (uint8_t i = 0; i < _send_len; i++) _send[i] = _send[i + 1];
	}
}

size_t SerialBLEInterface::writeFrame(const uint8_t src[], size_t len)
{
	if (len == 0 || len > MAX_FRAME_SIZE) return 0;
	if (!isConnected() || _send_len >= QSZ) return 0;
	_send[_send_len].len = len;
	memcpy(_send[_send_len].buf, src, len);
	_send_len++;
	return len;
}

size_t SerialBLEInterface::checkRecvFrame(uint8_t dest[])
{
	/* drain queued TX frames over NUS. Push as many as the controller will take this
	 * call (multiple notifications per connection event) instead of one per loop tick;
	 * stop on -ENOMEM (buffers full, retry next call). Speeds up multi-frame answers
	 * (settings/contacts) without touching connection parameters. */
	if (_send_len > 0) {
		if (!isConnected()) {
			_send_len = 0;
		} else {
			while (_send_len > 0) {
				int rc = bt_gatt_notify((struct bt_conn *)_conn, &nus_svc.attrs[1], _send[0].buf, _send[0].len);
				if (rc == 0) { mc_dump("NODE->APP", _send[0].buf, _send[0].len); shiftSend(); }
				else if (rc == -ENOTCONN) { shiftSend(); break; }   /* gone */
				else { printk("BLE: bt_nus_send rc=%d (retry)\n", rc); break; }  /* -ENOMEM: buffers full */
			}
		}
	}
	/* return one received frame */
	size_t out = 0;
	k_mutex_lock(&_lock, K_FOREVER);
	if (_recv_len > 0) {
		out = _recv[0].len;
		memcpy(dest, _recv[0].buf, out);
		_recv_len--;
		for (uint8_t i = 0; i < _recv_len; i++) _recv[i] = _recv[i + 1];
	}
	k_mutex_unlock(&_lock);

	/* re-advertise after a disconnect so the app can reconnect without a board reset */
	if (_enabled && _conn == nullptr && !_advertising) {
		unsigned long now = k_uptime_get();
		if (now - _last_adv_try > 500) {        /* throttle retries */
			_last_adv_try = now;
			int rc = mc_start_adv();
			if (rc == 0) {
				_advertising = true;
				BLE_DBG("re-advertising after disconnect\n");
			} else {
				BLE_DBG("re-advertise FAILED rc=%d (will retry)\n", rc);
			}
		}
	}
	return out;
}
