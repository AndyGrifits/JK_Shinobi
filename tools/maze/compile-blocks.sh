#!/usr/bin/env bash
# Compile every maps/mz/*.map under <base> (blocks of the procedural maze) in parallel: bsp (no vis: blocks are
# open at their doorways) + light.   Q3MAP2=... tools/maze/compile-blocks.sh <build dir containing base/>
set -euo pipefail
B="$1"; Q="${Q3MAP2:-q3map2}"
ls "$B"/base/maps/mz/*.map | xargs -P "$(nproc)" -I{} sh -c \
  "'$Q' -game ja -fs_basepath '$B' -fs_homepath '$B' -bsp -meta {} > {}.bsp.log 2>&1 && \
   '$Q' -game ja -fs_basepath '$B' -fs_homepath '$B' -light -fast -samples 2 -bounce 2 -filter {} > {}.light.log 2>&1 || echo FAIL {}"
