#!/usr/bin/env bash
set -euo pipefail

node scripts/build.mjs --warnings
node --test "tests/node/*.test.js"
