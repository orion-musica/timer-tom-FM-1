#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Timer Tom host test: compile the real app + screen code natively with the
# browser simulator platform, run every script in tools/host/tests and save
# PNG screenshots to build/host/. Needs clang (or gcc) and Python 3 with Pillow
# for the pictures. Usage, from the repository root:  sh tools/host/build.sh
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
out="$root/build/host"
cc=${CC:-clang}
mkdir -p "$out/obj"
inc="-I$root/web/engine -I$root/web/synth -I$root/third_party/synth -I$root/sdk/include -I$root/app/synth -I$root/app"
flags="-std=c11 -O1 -fwrapv -fno-fast-math -ffp-contract=off -DFM1_HOST_TEST -DFM1_OPEN_WEB -Wall -Wextra -Werror -Wno-unused-function -Wno-array-parameter"
objs=""
for src in "$root"/app/*.c "$root"/web/engine/*.c "$root"/web/synth/*.c "$root"/third_party/synth/*.c "$root"/tools/host/harness.c; do
    [ -f "$src" ] || continue
    obj="$out/obj/$(basename "$src").o"
    $cc $flags $inc -c "$src" -o "$obj"
    objs="$objs $obj"
done
$cc -o "$out/harness" $objs -lm
status=0
for script in "$root"/tools/host/tests/*.txt; do
    name=$(basename "$script" .txt)
    mkdir -p "$out/$name"
    echo "== $name"
    if ! (cd "$out/$name" && "$out/harness" < "$script"); then status=1; fi
done
if python3 -c "import PIL" 2>/dev/null; then
    python3 "$root/tools/host/topng.py" "$out"
fi
exit $status
