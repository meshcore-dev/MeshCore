# MR2 UVLO control-flow regression

Run with Python 3 and a C++17 compiler:

```powershell
python test/mr2_uvlo_flow/run.py --cxx C:/Tools/mingw64/bin/g++.exe
```

The runner extracts the production board boot, shutdown, RTC wake wrapper, board
loop and periodic dispatch functions, then compiles them against host hardware
fakes. It exercises validation failure, an asserted RTC INT pin, bounded recovery,
successful sleep, both early boot paths, continued watchdog/periodic work after a
failed shutdown, retry throttling, recovered or invalid voltage, sticky INA ALERT
and the 32-bit millisecond rollover. No production decision logic is copied into
the fixture. Temporary generated sources/binaries live outside the repository.

These checks cover board sequencing and decisions; the RTC configuration helper
is a fake at this layer. They do not verify register writes, electrical wake-up,
I2C timing, ADC averaging or battery behavior on physical hardware.
