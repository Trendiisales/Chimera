#!/usr/bin/env bash
set -e

ROOT_MARKER=".chimera_root"
GUARD='[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }'

echo "[CONTROL] Enforcing canonical root"

# Create root marker
if [ ! -f "$ROOT_MARKER" ]; then
  echo "CHIMERA_CANONICAL_ROOT=1" > "$ROOT_MARKER"
  echo "[ROOT] Marker created"
else
  echo "[ROOT] Marker exists"
fi

# Process all shell scripts
find . -type f -name "*.sh" | while read -r file; do
  if ! grep -F "$GUARD" "$file" >/dev/null; then
    echo "[PATCH] $file"

    tmpfile="$(mktemp)"

    {
      read -r first || true
      echo "$first"
      echo "$GUARD"
      cat
    } < "$file" > "$tmpfile"

    chmod --reference="$file" "$tmpfile"
    mv "$tmpfile" "$file"
  else
    echo "[OK] $file already guarded"
  fi
done

echo "[CONTROL] Root enforcement complete"
