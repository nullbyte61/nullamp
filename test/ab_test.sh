#!/usr/bin/env bash
# Bit-exactness gate: render every example model through our NamModel wrapper and
# assert the output is bit-identical to the upstream `render` tool.
#
#   Usage: test/ab_test.sh [build_dir]
set -euo pipefail

BUILD="${1:-build}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MODELS="$ROOT/deps/NeuralAmpModelerCore/example_models"
INPUT="$ROOT/deps/NeuralAmpModelerCore/example_audio/input.wav"
REF=/tmp/nam_ab_ref.wav

RENDER="$BUILD/nam_render"
OURS="$BUILD/render_cli"
[ -x "$RENDER" ] && [ -x "$OURS" ] || { echo "Build first: cmake --build $BUILD"; exit 2; }

fail=0
for model in "$MODELS"/*.nam; do
    name="$(basename "$model")"
    if ! "$RENDER" "$model" "$INPUT" "$REF" >/dev/null 2>&1; then
        printf "  %-28s SKIP (rate mismatch or unsupported)\n" "$name"
        continue
    fi
    if out="$("$OURS" "$model" "$INPUT" "$REF" 2>&1)" && echo "$out" | grep -q PASS; then
        printf "  %-28s %s\n" "$name" "$(echo "$out" | grep -oE 'Max abs diff = [^ ]+ .*')"
    else
        printf "  %-28s FAIL\n%s\n" "$name" "$out"
        fail=1
    fi
done

[ "$fail" -eq 0 ] && echo "bit-exactness gate: PASS" || { echo "bit-exactness gate: FAIL"; exit 1; }
