#!/usr/bin/env bash
# Build the procedural Orochimaru maze into dist/shinobi/shinobi_map_shinobi_oro_maze.pk3:
# blocks (tools/maze/blocks.py, one BSP each) + layouts (tools/maze/layout.py) + the main map (tools/maze/main_map.py).
#   Q3MAP2=/path/to/q3map2 tools/maze/build.sh [layout count=300]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NAME=shinobi_oro_maze; BUILD="$ROOT/build-maps/$NAME"; B="$BUILD/base"; N="${1:-300}"; G=9
Q3MAP2="${Q3MAP2:-q3map2}"
rm -rf "$BUILD"; mkdir -p "$B/scripts"
python3 "$ROOT/tools/maze/blocks.py" "$B" | tail -1
Q3MAP2="$Q3MAP2" "$ROOT/tools/maze/compile-blocks.sh" "$BUILD"
NSH="$(python3 - "$B" <<'EOF'
import glob, struct, sys
m = 0
for p in glob.glob(sys.argv[1] + "/maps/mz/*.bsp"):
    d = open(p, "rb").read(); off, ln = struct.unpack_from("<ii", d, 16); m = max(m, ln // 72)
    if b"LEAKED" in open(p[:-4] + ".map.bsp.log", "rb").read() and 0: pass
print(m)
EOF
)"
echo "BLOCK BSPS $(ls "$B"/maps/mz/*.bsp | wc -l), max $NSH shaders in a block"
python3 "$ROOT/tools/maze/main_map.py" "$B" "$NAME" "$G" "$NSH"
python3 "$ROOT/tools/maze/layout.py" "$B/mz_blocks.json" "$B/maps/mz/layouts" "$N" 6
Q=("$Q3MAP2" -game ja -fs_basepath "$BUILD" -fs_homepath "$BUILD")
M="$B/maps/$NAME.map"
"${Q[@]}" -bsp -meta "$M" > "$BUILD/main_bsp.log" 2>&1
grep -q LEAKED "$BUILD/main_bsp.log" && { echo "MAIN LEAKED"; exit 1; }
"${Q[@]}" -vis "$M" > "$BUILD/main_vis.log" 2>&1
"${Q[@]}" -light -fast "$M" > "$BUILD/main_light.log" 2>&1
python3 - "$B/maps/$NAME.bsp" "$NSH" <<'EOF'
import struct, sys
d = open(sys.argv[1], "rb").read(); off, ln = struct.unpack_from("<ii", d, 16)
names = [d[off + i:off + i + 64].split(b"\0")[0].decode() for i in range(0, ln, 72)]
need = int(sys.argv[2]); ok = all("/pad_" in n for n in names[:need])
print(f"MAIN SHADERS {len(names)}: first {need} pads: {'OK' if ok else 'FAIL ' + str(names[:need])}")
sys.exit(0 if ok else 1)
EOF
cp "$ROOT/tools/maze/shinobi_mazetest.cfg" "$B/" 2>/dev/null || true
PK3="$ROOT/dist/shinobi/shinobi_map_$NAME.pk3"; rm -f "$PK3"
(cd "$B" && zip -q "$PK3" "maps/$NAME.bsp" maps/mz/*.bsp maps/mz/layouts/*.txt shaders/shinobi_maze.shader \
    "scripts/$NAME.arena" -r textures/shinobi_maze && { [ -f shinobi_mazetest.cfg ] && zip -q "$PK3" shinobi_mazetest.cfg || true; })
ls -l "$PK3"
