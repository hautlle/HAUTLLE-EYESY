# HAUTLLE EYESY

Live visual instruments for the original CM3/CM3+ EYESY OF/Lua environment,
by [HAUTLLE](https://hautlle.com).

This source release includes the native engine, 16 live modes with their required
assets, shared Lua helpers, and tools for building, installing and rolling back.
It targets the inspected 20211213 beta image and the reviewed custom legacy
Raspbian 10 armhf setup. It is not an OS image or a modern Raspberry Pi/KMS port.

Start with [Setup](docs/SETUP.md). The source build can take an hour or longer.
The installer prepares a separate engine and leaves activation to an explicit
command. Keep a backup of your card and modes.

The spare-card build linked successfully, Markov and Architectural Ghosts passed
a brief runtime check, and the user confirmed physical Shift+knob recording.
The packaged installer has local checks; its full install/rollback workflow has
not been exercised end-to-end on the device. [Validation details](docs/VALIDATION.md)
state exactly what was checked.

- [Modes and assets](docs/SUPPORTED-MODES.md)
- [Recorder controls](docs/CONTROLLER.md)
- [Developer build notes](docs/DEVELOPMENT.md)
- [Frozen-layer Lua API](FROZEN-LAYER-API.txt)
- [Licensing and credits](docs/LICENSING.md)

New HAUTLLE work uses BSD-3-Clause where identified. Upstream code retains its
original licenses. MassSt's map data is © OpenStreetMap contributors under ODbL;
see its bundled [notice](modes/HAUTLLE_MASST_WALK/NOTICE.txt).
