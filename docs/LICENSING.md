# Licensing and credits

Project: [HAUTLLE](https://hautlle.com). New HAUTLLE additions, tools, Lua modes,
helpers and documentation are offered under BSD-3-Clause where identified by
`provenance/hautlle-license-scope.json`. This is a mixed-license distribution.
The top-level license does not relicense upstream code or map data.

The engine retains the Owen Osborn / Critter & Guitari notice. openFrameworks,
ofxLua (Dan Wilcox), ofxOsc/oscpack, GLM, JSON, utf8, KissFFT and libtess2 retain
their original notices under `notices/` and in source. The embedded OF bitmap
glyph notice remains applicable. No separate TTF files are redistributed.

KissFFT and libtess2 source retain their original license files. The imported
Apothecary reference scripts/patch and license under
`build-support/dependency-provenance/` retain GPL-3.0-or-later; they are not BSD
additions and are not executed by the custom Python build recipe. The OF-patched
libtess2 source retains that provenance and its original notices.

MassSt map data is © OpenStreetMap contributors under [ODbL 1.0](../notices/ODbL-1.0.txt).
Its [notice](../modes/HAUTLLE_MASST_WALK/NOTICE.txt) and complete supplied derived
data are bundled. This data is excluded from the HAUTLLE BSD scope.
The KCSC mesh identifies itself as the original monogram geometry and remains
part of the HAUTLLE mode payload. No unrelated third-party image/font assets are
included. Existing authorship and source notices in all mode files are retained.

EYESY_OS and installed system libraries remain separate dependencies. This
repository includes only the scoped beta-controller patch, not the OS or full PD
file. Unchanged patch context retains the underlying project's terms.
