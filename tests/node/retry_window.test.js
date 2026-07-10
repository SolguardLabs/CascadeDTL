import test from "node:test";
import assert from "node:assert/strict";
import { byId, runFixture } from "../helpers/runner.mjs";

test("deferred packet resumes after its retry window", () => {
  const state = runFixture("retained_retry.json");

  const retained = byId(state.packets, "pkt-retained-001");
  const gap = byId(state.packets, "pkt-gap-001");
  assert.equal(retained.status, "settled");
  assert.equal(gap.status, "settled");
  assert.equal(retained.attempts, 2);
  assert.equal(state.metrics.packetsDeferred, 1);
  assert.equal(state.metrics.packetsSettled, 2);

  const reserve = byId(state.reserves, "cov-main");
  assert.equal(reserve.available, 58000);
  assert.equal(reserve.locked, 0);
  assert.equal(reserve.settled, 42000);
  assert.equal(reserve.exposure, 0);
});

test("retry output remains deterministic with events enabled", () => {
  const state = runFixture("retained_retry.json", ["--events"]);
  assert.equal(state.events.some((line) => line.includes("packet_deferred")), true);
  assert.equal(state.events.some((line) => line.includes("packet_settled")), true);
});
