New: NRF targets are now set with MAX_CONTACTS=350 and MAX_GROUP_CHANNELS=40. Merged MeshCore-dev [08b49c3ac546f96596bc356d277d0d0c0a01134a](https://github.com/meshcore-dev/MeshCore/commit/08b49c3ac546f96596bc356d277d0d0c0a01134a)
Fixed: Updated library to fix RAK builds. Fixed STM32 builds thanks to @fdlamotte

This branch now uses dual filesystems - _main.id and new_prefs will live on UserData, while adv_blobs, channels and contacts will live on the extra filesystem - either an extra 100kb set aside on the onboard NRF flash, or on external flash if -D QSPIFLASH is set.

If you have already been testing this branch, this new version should migrate your _main.id and prefs file back to UserData (it will remove those files first if they exist in UserData).

If you haven't been testing this branch yet, it will migrate adv_blobs, channels and contacts across to the new filesystem. adv_blobs will be set to 100 records instead of 20 in the process.

All NRF boards have EXTRAFS set in the main platformio.ini - this will cause them to use 100kb at the end of the application flash area for a filesystem (unless you have QSPIFLASH set), leaving 100kb less for application size. This means 696kb for devices using softdevice S140 v6.1.1 or 692kb for S140 v7.3.0. **The board.json / env targets have not been modified to reflect this**.

RescueCLI now supports both filesystems, and it will show which files are on FS1 and FS2.
```
========= CLI Rescue =========
ls
[dir]  /FS1/adafruit
[file] /FS1/_main.id (96 bytes)
[file] /FS1/new_prefs (84 bytes)
[file] /FS2/adv_blobs (18000 bytes)
[file] /FS2/channels2 (544 bytes)
[file] /FS2/contacts3 (3344 bytes)
```
You must use the filesystem path when using the RescueCLI with dual filesytems. ie ```cat FS1/_main.id```


### Instructions:
You should **BACK UP ALL OF YOUR NODE DATA BEFORE FLASHING IN CASE SOMETHING GOES WRONG**, and also so that you can restore your identity and contact list etc to the new empty filesystem.

#### TODO:
- Set board_upload.maximum_size in env targets?  []