import test from "node:test";
import assert from "node:assert/strict";
import { byId, runFixture, validateFixture } from "../helpers/runner.mjs";

test("lane policy fixture validates", () => {
  assert.equal(validateFixture("lane_policy_controls.json"), "ok");
});

test("lane policy rejects packets before reserve accounting", () => {
  const state = runFixture("lane_policy_controls.json", ["--events"]);

  assert.equal(state.metrics.policyRejections, 3);
  assert.equal(state.metrics.packetsFailed, 3);
  assert.equal(state.metrics.packetsSettled, 2);
  assert.equal(state.metrics.grossSettled, 35000);
  assert.equal(state.events.filter((line) => line.includes("packet_policy_rejected")).length, 3);

  assert.equal(byId(state.packets, "pkt-policy-ok").status, "settled");
  assert.equal(byId(state.packets, "pkt-policy-fee").status, "failed");
  assert.equal(byId(state.packets, "pkt-policy-amount").status, "failed");
  assert.equal(byId(state.packets, "pkt-policy-priority").status, "failed");

  const rtgs = byId(state.reserves, "cov-rtgs");
  assert.equal(rtgs.available, 80000);
  assert.equal(rtgs.locked, 0);
  assert.equal(rtgs.settled, 20000);
});

test("lane reconciliation reports packet and reserve totals", () => {
  const state = runFixture("lane_policy_controls.json");
  const rtgs = byId(state.laneStats, "rtgs");
  const batch = byId(state.laneStats, "batch");

  assert.equal(rtgs.reserves, 1);
  assert.equal(rtgs.packets, 4);
  assert.equal(rtgs.settledPackets, 1);
  assert.equal(rtgs.failedPackets, 3);
  assert.equal(rtgs.available, 80000);
  assert.equal(rtgs.settled, 20000);
  assert.equal(rtgs.grossPackets, 65000);
  assert.equal(rtgs.grossFees, 265);

  assert.equal(batch.reserves, 1);
  assert.equal(batch.packets, 1);
  assert.equal(batch.settledPackets, 1);
  assert.equal(batch.available, 35000);
  assert.equal(batch.settled, 15000);
});
