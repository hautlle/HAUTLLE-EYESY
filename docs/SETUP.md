# Setup

Use the original CM3/CM3+ EYESY with the inspected 20211213 beta image, or the
reviewed custom legacy OF/Lua setup. Python 3.7+, GCC 8.3.0 and the recorded system
development libraries must already be installed. Missing prerequisites stop the
installer; it does not download packages, reflash the card or configure networking.

## Build and start

Use GitHub's **Code → Download ZIP**, extract it to `/sdcard/HAUTLLE-EYESY`, then open that folder over your
usual SSH connection as `music` on the beta image (`pi` on the custom setup).
`/sdcard` must already be a writable executable ext4 mount owned by that account.

```sh
python3 install.py check --workspace sdcard --temporary-swap
python3 install.py prepare --workspace sdcard --temporary-swap
sudo python3 install.py activate --workspace sdcard
```

`check` is read-only. `prepare` explicitly permits a temporary 1 GiB swap file and
builds with resource limits; it requires noninteractive sudo for that swap file.
Existing swap is left alone and causes this profile to stop. Temporary swap is
removed after the build; no persistent swap configuration changes are made.
Preparation keeps the existing renderer running. Activation switches the selected
OF service to the staged engine and adds a `HAUTLLE_START_HERE` Markov mode.

The beta launcher uses the bundled `modules/` before the beta fallback, so the
included modes do not depend on the stock beta's font-loading helper.

## Add the live modes

Copy the mode folders you want from `modes/` into `/sdcard/Modes/oFLua/` using SFTP
or your existing mode-management workflow. Keep each folder name and all its files.
Preserve existing user modes instead of overwriting a same-named folder. Restart
the renderer through the normal EYESY interface to refresh its mode list.
The installer adds only the starter; it does not automatically replace your menu.

On beta, [enable the recorder controller option](CONTROLLER.md) if you want
Shift+knobs reserved for recording. The engine installer does not change PD.

## Undo or recover

```sh
sudo python3 install.py rollback
```

The activation journal selects the matching workspace. Controller rollback is
separate. If a build was interrupted and reports leftover temporary swap, keep its
files and run `sudo python3 install.py cleanup-build --workspace sdcard` after the
build stops. It checks the process group and file identity before cleanup; never
delete an active swap file manually.

This is an early source release. The spare-card engine and recorder are working;
the packaged install/rollback path is not yet tested end-to-end. Preserve installer
output if a prerequisite or recovery check stops the operation.
