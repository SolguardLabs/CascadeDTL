import test from "node:test";
import assert from "node:assert/strict";
import { byId, runFixture } from "../helpers/runner.mjs";

test("congestion weighting can outrank base priority", () => {
  const state = runFixture("priority_congestion.json", ["--events"]);
  const attempts = state.events
    .filter((line) => line.includes("packet_attempt"))
    .map((line) => line.match(/packet=([^ ]+)/)?.[1]);

  assert.deepEqual(attempts, ["pkt-heavy-001", "pkt-top-001", "pkt-low-001"]);
  assert.equal(
    byId(state.packets, "pkt-heavy-001").lastScore > byId(state.packets, "pkt-top-001").lastScore,
    true,
  );
  assert.equal(state.metrics.grossSettled, 33000);
});
