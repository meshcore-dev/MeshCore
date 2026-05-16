# Convenience targets. Firmware builds use PlatformIO directly: `pio run -e <env>`.

.PHONY: test
test:
	pio test -e native
