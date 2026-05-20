#!/bin/sh
# verify_vendor.sh - prove the vendored yaAGC is upstream's exact code.
#
# apollo-64 runs the Virtual AGC engine; the whole "this is a real AGC"
# claim rests on that engine being unmodified. This script fetches the
# pinned upstream commit and checks the vendored copy against it:
#
#   - agc_engine.c, NullAPI.c, EmbeddedDemo.c must be BYTE-IDENTICAL.
#     agc_engine.c is the CPU / executive / instruction simulator - the
#     actual AGC. Byte-identical means apollo-64's AGC *is* upstream's.
#   - agc_engine.h, yaAGC.h must differ from upstream by EXACTLY the
#     committed expected diff (the -DN64 build branches documented in
#     vendor/yaAGC/PATCHES.md) - no more, no less.
#
# Exit 0 if the vendored tree is faithful, non-zero otherwise.

set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
vendor="$here/vendor/yaAGC"
pin=$(tr -d ' \t\n' < "$vendor/UPSTREAM")
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "verify_vendor: pinned upstream commit $pin"

# Fetch just the pinned commit.
git -C "$tmp" init -q
git -C "$tmp" remote add origin https://github.com/virtualagc/virtualagc.git
git -C "$tmp" fetch -q --depth 1 origin "$pin"
git -C "$tmp" checkout -q FETCH_HEAD

up="$tmp/yaAGC"
rc=0

# Files that must be byte-identical to upstream.
for f in agc_engine.c NullAPI.c EmbeddedDemo.c; do
  if cmp -s "$vendor/$f" "$up/$f"; then
    echo "  OK   $f  byte-identical"
  else
    echo "  FAIL $f  differs from upstream"
    rc=1
  fi
done

# Headers must differ from upstream by exactly the committed expected
# diff - the -DN64 build branches and nothing else.
for f in agc_engine.h yaAGC.h; do
  if diff "$up/$f" "$vendor/$f" | diff -q - "$vendor/$f.expected-diff" >/dev/null 2>&1; then
    echo "  OK   $f  matches the committed -DN64 patch exactly"
  else
    echo "  FAIL $f  diff vs upstream is not the documented patch:"
    diff "$up/$f" "$vendor/$f" | sed 's/^/        /'
    rc=1
  fi
done

if [ "$rc" -eq 0 ]; then
  echo "verify_vendor: PASS - vendored engine is upstream yaAGC, unmodified."
else
  echo "verify_vendor: FAIL - vendored tree diverges from upstream."
fi
exit $rc
