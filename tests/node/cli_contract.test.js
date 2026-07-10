import test from "node:test";
import assert from "node:assert/strict";
import { runCli, runFixture, validateFixture } from "../helpers/runner.mjs";

test("validate accepts fixture schema", () => {
  assert.equal(validateFixture("balanced_settlement.json"), "ok");
});

test("help output exposes supported commands", () => {
  const stdout = runCli(["--help"]);
  assert.match(stdout, /CascadeDTL batch settlement simulator/);
  assert.match(stdout, /validate <fixture\.json>/);
});

test("run emits stable json contract", () => {
  const state = runFixture("balanced_settlement.json");
  assert.equal(state.ok, true);
  assert.equal(state.scenario, "balanced-settlement");
  assert.equal(state.metrics.batchesProcessed, 1);
  assert.equal(Array.isArray(state.participants), true);
  assert.equal(Array.isArray(state.reserves), true);
  assert.equal(Array.isArray(state.packets), true);
  assert.equal(Array.isArray(state.receipts), true);
});
