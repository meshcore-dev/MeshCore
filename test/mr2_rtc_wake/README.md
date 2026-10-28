# MR2 RTC wake verification

Run with Python 3 and a C++17 compiler:

```powershell
python test/mr2_rtc_wake/run_test.py --cxx C:/Tools/mingw64/bin/g++.exe
```

The runner compiles the complete production `Rv3028Wake.cpp` and its header,
copied verbatim into a temporary directory, against a small Wire register model.
It takes RTC register addresses from the real board header. Generated files and
the executable are temporary; the `.inc` fixture is excluded from PlatformIO's
normal C++ test discovery.

Tests cover valid and clamped durations; restarting an active timer; NACKs on
every write and register selection; failure to queue any address or data byte;
acknowledged but ignored writes; short and inconsistent reads; incorrect
readback, including a timer flag that did not clear; and an absent RTC. Unrelated
registers must remain unchanged. Reserved register bits are ignored correctly.

The helper makes one attempt and returns false on failure. Bus recovery, retry,
the GPIO wake input and deciding whether to enter System OFF belong to the board
caller, outside this test. This model does not prove electrical I2C behavior,
oscillator operation or a physical wake from System OFF. Those require a board
test with low voltage and RTC/I2C fault injection.
