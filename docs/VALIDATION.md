# Validation scope

Completed on the spare CM3+ beta card:

- The 97-object engine archived and linked successfully. This was a resumed build:
  94 verified objects from a fresh partial run, one verified binding object, and
  two new compiles. It is not a clean all-unit run.
- The binding used original O3/ABI flags, removed `-pipe`, and added GCC collector
  parameters `ggc-min-expand=10` and `ggc-min-heapsize=4096`. With a temporary 1 GiB
  swap file and 1 GiB process address-space cap it compiled in 776.253 seconds,
  peaked at 483.53 MiB child RSS, and matched three historical objects byte for byte.
- Startup and Markov → Architectural Ghosts → Markov passed a brief log-based
  check, including PERSIST and injected OSC knobs. Helpers, FBO allocation and
  shader compile/link succeeded. Stock restoration was verified afterward.
- A separate guarded deployment installed all 16 captured live modes and required
  assets; Markov startup passed. Boot service enablement was checked, not a reboot.
- The user physically confirmed the Shift+knob recorder fix on 2026-10-18.

Completed locally before publication: 97 focused recipe/resource/installer checks,
then eight recorder integration checks. Native source and mode assets retain their
recorded hashes. Final packaging checks cover contents, required assets, licensing
notices and sensitive-file exclusion. No new build or extended suite was run for
publication. The beta launcher now stages/prefers the two bundled modules using
the convention already used by the guarded deployment; that packaging change was
reviewed statically.

Remaining limits: the packaged install, temporary-swap supervisor, activation and
rollback paths have not been exercised together end-to-end on hardware. The
controller transaction in this installer is locally tested, while its exact PD
patch was installed and physically confirmed separately. All 16 modes are present;
this is not a claim of new visual/audio/performance qualification for every mode.

Recorded executable SHA256:
`47f1d7b85e04b9b66e53541235da51f890c934c7cd4e4b014343fed497cfb4e1`

The repository distributes source, not that executable or an OS image.
