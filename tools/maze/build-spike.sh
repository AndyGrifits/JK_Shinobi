#!/usr/bin/env bash
# Build the misc_bsp maze spike into dist/shinobi/shinobi_map_shinobi_maze_spike.pk3 (see tools/maze/spike.py).
#   Q3MAP2=/path/to/q3map2 tools/maze/build-spike.sh <texture dir with oro_wall.jpg, oro_ground.jpg, oro_rock.jpg>
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEX="$1"; BUILD="$ROOT/build-maps/maze_spike"; B="$BUILD/base"
Q3MAP2="${Q3MAP2:-q3map2}"
rm -rf "$BUILD"; mkdir -p "$B"
python3 "$ROOT/tools/maze/spike.py" "$B" "$TEX"
Q=("$Q3MAP2" -game ja -fs_basepath "$BUILD" -fs_homepath "$BUILD")
for m in mz/spk_a mz/spk_b mz/spk_c; do         # blocks are open at the doorways: they leak, no vis
    "${Q[@]}" -bsp -meta "$B/maps/$m.map" > "$BUILD/$(basename $m)_bsp.log" 2>&1
    "${Q[@]}" -light -fast -samples 2 -bounce 2 -filter "$B/maps/$m.map" > "$BUILD/$(basename $m)_light.log" 2>&1
    echo "BLOCK $m: $(grep -c LEAKED "$BUILD/$(basename $m)_bsp.log") leak lines, $(grep -E 'BSP lightmaps' "$BUILD/$(basename $m)_light.log" | xargs)"
done
M="$B/maps/shinobi_maze_spike.map"
"${Q[@]}" -bsp -meta "$M" > "$BUILD/main_bsp.log" 2>&1
grep -q LEAKED "$BUILD/main_bsp.log" && { echo "MAIN LEAKED"; exit 1; }
"${Q[@]}" -vis "$M" > "$BUILD/main_vis.log" 2>&1
"${Q[@]}" -light -fast "$M" > "$BUILD/main_light.log" 2>&1
cp "$ROOT/tools/maze/shinobi_mazespike.cfg" "$B/"
PK3="$ROOT/dist/shinobi/shinobi_map_shinobi_maze_spike.pk3"; rm -f "$PK3"
(cd "$B" && zip -q "$PK3" maps/shinobi_maze_spike.bsp maps/mz/*.bsp shaders/shinobi_maze_spike.shader \
    scripts/shinobi_maze_spike.arena shinobi_mazespike.cfg -r textures/shinobi_maze)
ls -l "$PK3"
