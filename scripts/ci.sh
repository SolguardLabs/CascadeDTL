#!/usr/bin/env bash
set -euo pipefail

node scripts/build.mjs --warnings --clean
node --test "tests/node/*.test.js"
npm run format:check
node scripts/verify-repository.mjs
