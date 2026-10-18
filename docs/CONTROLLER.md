# Shift+knob recording on beta

The user physically confirmed this fix on the spare card on 2026-10-18.
Hold Shift, move a knob, then release Shift to replay the recorded movement.
Normal PERSIST keeps its mode-defined retain/replace behavior; it is not a global
freeze command. The old Shift+PERSIST shortcut is retired.

The recorder option reserves Shift+knobs instead of beta shifted gain, MIDI and
trigger-settings editing. Stored settings, startup/external recall, `/shift`
notifications and physical key mappings remain. The original stock controller
is still available through the separate rollback.

```sh
python3 install.py controller-check
sudo python3 install.py controller-apply
```

Only the exact inspected `/home/music/EYESY_OS/pd/eyesy.pd` preimage is accepted.
The installer saves `/var/lib/hautlle-eyesy/controller/eyesy-stock.pd`, applies the
three routing changes, and restarts only `eyesy-pd.service`. It preserves the video
service identity and attempts recovery if PD startup fails. It resolves
`/usr/bin/pd` correctly when that is a link to `puredata`.

```sh
sudo python3 install.py controller-rollback
```

Engine rollback and controller rollback are separate. An already-patched controller
is left unchanged, using its existing backup/rollback route. This packaged controller
transaction has focused local tests; the physical confirmation concerns the same
patch installed by the separately guarded spare-card procedure.
