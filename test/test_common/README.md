# Common tests

This directory holds tests that are expected to pass on all platforms,
including native and on-device tests.

Tests that exercise device-specific features should should not go here,
and should be capable of passing with hardware features mocked out
(e.g. SPI or Wire are present but return fake responses.)
