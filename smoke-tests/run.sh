#!/bin/bash
# Stable entry point for Yakkai local render regression checks.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec python3 "$ROOT/smoke-tests/runner.py" "$@"
