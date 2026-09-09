#!/usr/bin/env bash
set -euo pipefail
if command -v idf.py >/dev/null 2>&1; then
  exec "$@"
fi
# Pinned Nix environment, isolated from the system configuration.
exec nix develop github:mirrexagon/nixpkgs-esp-dev/5287d6e1ca9e15ebd5113c41b9590c468e1e001b#esp32s3-idf -c "$@"
