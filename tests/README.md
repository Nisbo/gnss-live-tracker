# Host-test fixtures

The supported entry point is `python scripts/test-v2.py` from the repository root.

These tests use fabricated application state, standard NMEA examples and public satellite-catalogue samples. Test keys and device/network identifiers are examples. No live tracker credentials, exported personal tracks or private network configuration are required.

The host suite does not connect to a tracker, a broker or a LoRaWAN network. Hardware coverage and remaining limitations are described in [TESTS.md](../docs/TESTS.md).
