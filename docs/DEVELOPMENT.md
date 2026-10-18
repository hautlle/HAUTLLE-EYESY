# Developer build notes

The pinned legacy target uses GCC 8.3.0, GNU binutils 2.31.1, armhf ABI, Broadcom
GLES2 in `/opt/vc`, LuaJIT 5.1, and the system headers/libraries listed by
`build-support/dependencies.json`. The source plan contains 97 engine units and
650 source inputs. Two dependencies are rebuilt from the included sources using
`build-support/rebuild_dependencies.py`; their nine compile units include a CPU
smoke check. Imported Apothecary references are not executed by this recipe.

Use `install.py check` for prerequisite checks and the setup guide for orchestration.
For direct tooling, see `python3 build-support/build.py --help` and
`python3 build-support/rebuild_dependencies.py --help`. Build output must be fresh
and separate from source/dependencies; no cached objects are silently reused.

The default compiler recipe is original. Optional
`--compiler-memory-profile bindings-gc-v1` changes only `ofBindings.cpp`: removes
`-pipe`, adds `--param ggc-min-expand=10 --param ggc-min-heapsize=4096`, and keeps
O3, ABI and vectorization flags. It does not enable swap or limits by itself.
`expanded-plan.json` and `build-result.json` record effective commands and hashes.

The opt-in installer resource profile uses one temporary 1 GiB swap file, a 1 GiB
address-space cap, and guarded process groups. It stops at <32 MiB available RAM,
<64 MiB for 3 seconds, <256 MiB free swap, <384 MiB combined headroom, <640 MiB
data free or <320 MiB root free. Cleanup verifies the exact inode and active-swap
state; interrupted recovery never signals a saved, possibly reused process group.
Full hardware installer qualification is still pending.

Local unit fixtures are available under `tests/`; they mock device service actions.
Existing native CPU and Lua fixture runners are separate explicit tools. No tests
run automatically during a repository checkout.
