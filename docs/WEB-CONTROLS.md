# Beta web controls

The stock beta editor gains **Modes** and **Load this mode**, styled to match its
existing buttons. Settings, Save and Reload Mode remain available.

**Modes** opens `/modes`, which lists the live OF/Lua folders on your card.
**Load this mode** loads the saved `main.lua` currently displayed in the editor.
If it has unsaved edits, you can cancel and save first, or load the saved version
while keeping your edits in the editor. Loading does not save automatically.
Loading the already active mode resets that mode's state.

## Install on the inspected 20211213 beta image

From a clean extracted source release, run these separately from engine setup:

```sh
python3 install.py web-check
sudo python3 install.py web-apply
```

The check identifies the exact stock or final versions of the three web files.
Apply backs up the stock files, stages all replacements, preserves their owner and
permissions, then restarts only `cherrypy.service`. It verifies the served editor,
JavaScript and selector through localhost. It does not select a mode during this
check. Refresh the browser with **Ctrl+F5** afterward.

These actions support `/home/music/EYESY_OS` on the inspected beta image. They
require the existing web service to be running and serving the expected files on
port 8080. They do not install dependencies or change Settings, the renderer, PD,
service enablement, modes or networking. The existing trusted-LAN access model
remains; this is not a web security audit. There is no Flask migration or Palette UI.

The source patch is `patches/beta-web-controls.patch`; the exact preimages and
transformations are in `build-support/beta-web.json`. Unknown or mixed file versions
are refused, including intermediate versions of these controls. Restore or review
your earlier patch first. All three final files already present is a no-op.

## Undo and recovery

```sh
sudo python3 install.py web-rollback
```

Rollback requires the original stock backups created by this installer under
`/var/lib/hautlle-eyesy/web/stock/`. It restores all three files and restarts only
the web service. Engine and recorder rollback are separate. If these exact controls
were installed by another workflow, retain and use that workflow's rollback copies.

Caught write or health-check failures restore the previous known files. An
independent edit is never overwritten during recovery. Power loss or forced kill
can leave mixed files: retain the backups, avoid rerunning patches blindly, and
review the three files before restoring them. Keep rollback copies during cleanup.

The exact web patch and styling were verified on the spare beta card. The packaged
installer transaction has local fixture coverage but has not been run end-to-end
on hardware. See [validation](VALIDATION.md).
