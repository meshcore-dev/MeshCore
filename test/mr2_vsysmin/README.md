# MR2 minimum-system-voltage regression

Run with Python 3 and a C++17 compiler:

```powershell
.venv/Scripts/python.exe test/mr2_vsysmin/run.py --cxx C:/Tools/mingw64/bin/g++.exe --check-mutations
```

The runner extracts the production `configureBaseBQ()`, `configureChemistry()` and
`getBatteryProperties()` implementations and the real battery property table and
constants. It compiles them against a BQ register-state model and executes ten
scenarios. Generated sources and binaries use a temporary directory. The fixture
has an `.inc` extension so PlatformIO does not pick it up as an Arduino/native test
translation unit.

The model implements successful CELL writes resetting VSYSMIN, VREG and ICHG;
MPPT rejection below VSYSMIN or without a source; and the final BATFET linear-mode
condition. Checks cover Na-ion at 2.63 V, LTO 2S at 4.9 V, repeated chemistry changes,
MPPT disabled, unknown chemistry, voltage below 2.5 V, an absent source, an
uninitialized driver, LiFePO4 and Li-ion. They also check CE, register restoration
order, preservation of the MPPT preference and the existing 60 C thermal setting.

`--check-mutations` proves the suite rejects the old 2.75 V setting, removal of the
post-CELL VSYSMIN restoration, and removal of the final MPPT enable request.

These are host sequencing tests, not electrical measurements. The model does not
exercise the I2C bus or driver register encoding, simulate I2C write failures, prove
transient thermal behavior, or simulate MPPT tracking under clouds. Flashing and
reading hardware telemetry is still necessary to confirm the observed behavior.
