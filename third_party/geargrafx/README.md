# Geargrafx core (vendored)

PC Engine hardware emulation from [Geargrafx](https://github.com/drhelius/Geargrafx) by Ignacio Sanchez, GPL-3.0 (see LICENSE).

- Upstream commit: `60c7208832f5003b1551bbebf69fae772d7e927d` (1.8.5)
- Only the core (`src/`) and the libraries it needs are included. The desktop front end is not.
- Local changes, also in `tools/tracer/geargrafx-rondo-trace.patch`:
  - `rondo_trace.*` coverage tracer (off unless initialized)
  - `HuC6280::XlatInstruction` and operand injection in `Fetch8/Fetch16` for translated code
  - `GeargrafxCore::SetXlat/XlatPre/XlatPost` dispatch hook in the frame loop
