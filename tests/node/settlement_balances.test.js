import test from "node:test";
import assert from "node:assert/strict";
import { byId, runFixture } from "../helpers/runner.mjs";

test("balanced settlement updates reserve and participant balances", () => {
  const state = runFixture("balanced_settlement.json");

  assert.equal(state.metrics.packetsSettled, 2);
  assert.equal(state.metrics.packetsFailed, 0);
  assert.equal(state.metrics.grossSettled, 43000);
  assert.equal(state.metrics.grossFees, 205);

  const alpha = byId(state.reserves, "cov-alpha");
  const beta = byId(state.reserves, "cov-beta");
  assert.equal(alpha.available, 95000);
  assert.equal(alpha.locked, 0);
  assert.equal(alpha.settled, 25000);
  assert.equal(beta.available, 72000);
  assert.equal(beta.locked, 0);
  assert.equal(beta.settled, 18000);

  assert.equal(byId(state.participants, "merchant-a").balance, 24875);
  assert.equal(byId(state.participants, "merchant-b").balance, 17920);
  assert.equal(byId(state.participants, "operator").fees, 205);
});

test("receipt accounting is consumed after a normal settlement", () => {
  const state = runFixture("balanced_settlement.json");
  for (const receipt of state.receipts) {
    assert.equal(receipt.valid, true);
    assert.equal(receipt.consumed, true);
    assert.equal(receipt.retryable, false);
  }
});
