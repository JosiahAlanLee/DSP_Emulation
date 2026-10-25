# Clocked Register

One byte of state for the [byte bus](../../docs/BYTE_BUS_SPEC.md), in a 12 HP
VCV Rack module. BUS IN carries eight bits, LSB first, optionally with a ninth
clock channel. BUS OUT publishes the held word and its word clock. Choose the
clock source and RESET word; patch a transform around it to build a feedback
sequence. Eight LEDs and a hex display show the held bits.

Plugin **ByteBusLab**, module **Clocked Register**, version **2.0.0**.
[Manual and timing contract](docs/MANUAL.md).

Build and check from the repository root:

```sh
make -C vcv/bytebus RACK_DIR=/absolute/path/to/Rack-SDK -j2
make -C vcv/bytebus/tests check RACK_DIR=/absolute/path/to/Rack-SDK
make -C vcv/bytebus/tests sanitize RACK_DIR=/absolute/path/to/Rack-SDK
make -C vcv/bytebus dist RACK_DIR=/absolute/path/to/Rack-SDK
```

The portable test requires no Rack SDK: `make -C vcv/bytebus/tests test`.
The module test runs the actual Register and existing Vastish, with cables
delayed one engine sample and randomized module order, and verifies a FUN XOR
feedback sequence and RESET replay at 44.1, 48 and 96 kHz.

Prototype: not in the three-instrument release bundles or the VCV Library.
Hardware clock paths are outside these software tests.
