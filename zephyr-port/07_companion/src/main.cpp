/*
 * Step 4e: the full MeshCore companion on Zephyr. Wires companion_radio's MyMesh +
 * DataStore onto the proven Zephyr seams: radio (target.cpp / RadioLib+ZephyrHal),
 * filesystem (InternalFS / flash_area), and serial = the bonded bt_nus SerialBLEInterface.
 * The phone app pairs (level 4, fixed PIN), then talks the companion protocol over NUS.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include <Arduino.h>
#include <target.h>
#include <helpers/nrf54/InternalFileSystem.h>
#include <helpers/SimpleMeshTables.h>
#include "serial_ble_interface.h"
#include "MyMesh.h"   /* examples/companion_radio (on the include path) */

SerialShim Serial;

StdRNG fast_rng;
SimpleMeshTables tables;
DataStore store(InternalFS, rtc_clock);
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store);

int main(void)
{
	printk("\n=== MeshCore companion on Zephyr (board=%s) ===\n", CONFIG_BOARD);

	board.begin();
	if (!radio_init()) { printk("radio_init FAILED\n"); return 0; }
	fast_rng.begin(radio_get_rng_seed());
	if (!InternalFS.begin()) { printk("InternalFS FAILED\n"); return 0; }
	store.begin();
	the_mesh.begin(false);   /* no display (loads prefs from /new_prefs) */

	/* DEBUG: did the node name survive the last power cycle? On a cold boot this should show
	 * the previously-set name and exists=1; if it shows the default name / exists=0 the prefs
	 * file was lost (reformat or never persisted). Paired with the "FS: mounted OK/FAILED" line. */
	printk("PREFS: cold boot -> /new_prefs exists=%d, node_name='%s'\n",
	       InternalFS.exists("/new_prefs"), the_mesh.getNodePrefs()->node_name);

	char name[48];
	snprintf(name, sizeof(name), "%s%s", BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name);
	ble.begin(name, the_mesh.getBLEPin());
	the_mesh.startInterface(ble);
	printk("companion up: '%s' pin %lu — connect from the MeshCore app\n",
	       name, (unsigned long)the_mesh.getBLEPin());

	while (1) {
		the_mesh.loop();
		rtc_clock.tick();
		sensors.loop();

		/* A rename from the app (CMD_SET_ADVERT_NAME) only updates prefs; push it to BLE so
		 * the advertised/GAP name follows without a reboot. */
		char cur[48];
		snprintf(cur, sizeof(cur), "%s%s", BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name);
		if (strcmp(cur, name) != 0) {
			strcpy(name, cur);
			ble.setDeviceName(name);
			/* DEBUG: confirm the rename reached the FS (savePrefs ran in the CMD handler).
			 * exists=1 means /new_prefs was (re)written; if it's still 0 the save never landed. */
			printk("PREFS: rename -> '%s', /new_prefs exists=%d\n",
			       the_mesh.getNodePrefs()->node_name, InternalFS.exists("/new_prefs"));
		}

		k_msleep(1);   /* snappy serial/radio polling for the app's frame bursts */
	}
	return 0;
}
