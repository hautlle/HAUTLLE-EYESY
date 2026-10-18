Qualified Markov720 helper migration

main.lua SHA256:
00697edcb61195528d6846563960189cd169f7fb352d571b1b82f1ce7c62fe0e

The only change from the accepted baseline is resolving the shared helper through
of.toDataPath('lua/hautlle/v1/frozen_layer_pair.lua', true). Install the exact
qualified API 1.2 helper in the matching executable's bin/data tree first:
05d1c9e7f24de7f9092c06ac16a8f5e71c0ae4f4d556e31dd23552a9c362b11d

The mode retains its original 640x480 virtual geometry mapped to 1280x720,
normalized knob controls, trigger handling and 30 Hz mutation scheduler. It does
not analyze audio. PERSIST off produces a clean foreground; PERSIST on retains
and transforms prior layer content. It continues evolving while on. Host clear
pulses take precedence. See docs/PERSIST-BEHAVIOR.txt in the source package.

This directory is a reviewable example. Adding it to an engine checkout does not
install it into the active mode menu. The separate guarded mode patch targets the
existing HAUTLLE_MARKOV_LIVE_720_V1 directory. No Delta source is included.
