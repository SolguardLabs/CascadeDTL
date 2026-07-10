import test from "node:test";
import assert from "node:assert/strict";
import crypto from "node:crypto";
import { runCli } from "../helpers/runner.mjs";

function digest(text) {
  return crypto.createHash("sha256").update(text).digest("hex");
}

test("same fixture produces byte-stable report", () => {
  const args = ["run", "tests/fixtures/balanced_settlement.json", "--json", "--events"];
  const first = runCli(args);
  const second = runCli(args);
  assert.equal(digest(first), digest(second));
});
