#!/usr/bin/env bash
# Build a model-based JK_Shinobi map from an FBX + textures into dist/shinobi/shinobi_map_<name>.pk3.
#
#   tools/build-map.sh <name> <source.fbx> <texture dir>
#
# Map settings live in maps/<name>/map.json. Needs: python3 with bpy + Pillow (pip install bpy pillow),
# zip, and q3map2 from netradiant-custom (set Q3MAP2=/path/to/q3map2 if it is not on PATH).
# Extra q3map2 switches: BSP_ARGS, VIS_ARGS, LIGHT_ARGS (e.g. BSP_ARGS=-debugclip to see the collision).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NAME="$1"; FBX="$2"; TEX="$3"
CFG="$ROOT/maps/$NAME/map.json"
BUILD="$ROOT/build-maps/$NAME"
DIST="$ROOT/dist/shinobi"
Q3MAP2="${Q3MAP2:-q3map2}"
PY="python3 -I"

mkdir -p "$BUILD" "$DIST"
$PY "$ROOT/tools/maps/prep_model.py" "$CFG" "$FBX" "$TEX" "$BUILD" | grep -E "PREP|RESCALE|DROP|PROPS|FACE_UP|RING|SPLIT|STRETCH|PLAZA|UV_|FAR|CUT|DELETE|rror"
$PY "$ROOT/tools/maps/propclip.py" "$CFG" "$BUILD" | grep -E "PROPCLIP|rror"
$PY "$ROOT/tools/maps/stairs.py" "$CFG" "$BUILD" | grep -E "STAIRS|ramp|rror"
$PY "$ROOT/tools/maps/waterfill.py" "$CFG" "$BUILD" | grep -E "WATERFILL|rror"
python3 "$ROOT/tools/maps/gen_map.py" "$CFG" "$BUILD"
SKYENV="$(python3 -c "import json;print(json.load(open('$CFG')).get('sky',{}).get('env',''))")"
[ -n "$SKYENV" ] && python3 "$ROOT/tools/maps/gen_sky.py" "$CFG" "$BUILD"
$PY "$ROOT/tools/maps/levelshot.py" "$CFG" "$BUILD" | grep -E "LEVELSHOT|rror"

MAP="$BUILD/base/maps/$NAME.map"
Q=("$Q3MAP2" -game ja -fs_basepath "$BUILD" -fs_homepath "$BUILD")
"${Q[@]}" -bsp -meta -patchmeta ${BSP_ARGS:-} "$MAP"             2>&1 | tee "$BUILD/bsp.log"
grep -q "LEAKED" "$BUILD/bsp.log" && { echo "LEAK - see $BUILD/base/maps/$NAME.lin"; exit 1; }
"${Q[@]}" -vis -saveprt ${VIS_ARGS:-} "$MAP"                       2>&1 | tee "$BUILD/vis.log"
"${Q[@]}" -light -fast -samples 2 -bounce 2 -patchshadows -filter ${LIGHT_ARGS:-} "$MAP" 2>&1 | tee "$BUILD/light.log"

PK3="$DIST/shinobi_map_$NAME.pk3"
rm -f "$PK3"
(cd "$BUILD/base" && zip -q "$PK3" "maps/$NAME.bsp" "shaders/$NAME.shader" "scripts/$NAME.arena" \
    "levelshots/$NAME.jpg" ${SKYENV:+$SKYENV\_*.jpg} -r "textures/$(python3 -c "import json;print(json.load(open('$CFG'))['shader_dir'])")")
ls -l "$PK3"
