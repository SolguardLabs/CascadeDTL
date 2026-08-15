import test from "node:test";
import assert from "node:assert/strict";
import { binary, root } from "../helpers/runner.mjs";
import {
  CascadeClient,
  atomic,
  computeCapital,
  parseIntegerJson,
  portfolioStress,
} from "../../sdk/cascade-client.mjs";

const input = {
  reserveAvailable: 300_000n,
  reserveLocked: 50_000n,
  pendingGross: 180_000n,
  expectedFees: 900n,
  largestCounterparty: 90_000n,
};

const policy = {
  reserveHaircutBps: 800,
  volatilityBps: 650,
  liquidityBps: 40,
  concentrationBps: 1_200,
  horizonEpochs: 3,
  targetCoverageBps: 12_500,
  operationalFloor: 25_000n,
};

test("capital model applies haircuts and stress addons deterministically", () => {
  const result = computeCapital(input, policy);
  assert.deepEqual(result.report, {
    eligibleAvailable: 276_000n,
    eligibleLocked: 46_000n,
    eligibleResources: 322_000n,
    volatilityAddon: 11_700n,
    liquidityAddon: 2_160n,
    concentrationAddon: 10_800n,
    stressedObligation: 205_560n,
    requiredCoverage: 281_950n,
    surplus: 40_050n,
    shortfall: 0n,
    coverageBps: 11_420n,
    largestShareBps: 5_000n,
    policySatisfied: true,
  });
});

test("CLI capital report matches the offline model", () => {
  const client = new CascadeClient({ binaryPath: binary, root });
  const remote = client.capital(input, policy);
  const offline = computeCapital(input, policy);
  assert.deepEqual(remote.report, offline.report);
});

test("governance evaluation binds the complete operation domain", () => {
  const client = new CascadeClient({ binaryPath: binary, root });
  const operation = {
    protocol: "CascadeDTL",
    network: "testnet",
    chainId: 84_532,
    target: "reserve-registry",
    selector: "set-policy",
    payloadDigest: "8a36f1",
    predecessor: "none",
    salt: "2026q3",
    eta: 1_700_000_000n,
    expiresAt: 1_700_003_600n,
    quorum: 3,
    approvals: 3,
  };
  const ready = client.governance(operation, { now: 1_700_000_100n, predecessorSatisfied: true });
  assert.equal(ready.operationId, "365426de23338105");
  assert.equal(ready.lifecycle, "ready");
  assert.equal(ready.executable, true);

  const changed = client.governance(
    { ...operation, payloadDigest: "8a36f2" },
    { now: 1_700_000_100n, predecessorSatisfied: true },
  );
  assert.notEqual(changed.operationId, ready.operationId);
});

test("governance evaluation enforces quorum, timelock, predecessor, and expiry", () => {
  const client = new CascadeClient({ binaryPath: binary, root });
  const base = {
    protocol: "CascadeDTL",
    network: "testnet",
    chainId: 84_532,
    target: "reserve-registry",
    selector: "set-policy",
    payloadDigest: "8a36f1",
    predecessor: "op-previous",
    salt: "2026q3",
    eta: 1_700_000_000n,
    expiresAt: 1_700_003_600n,
    quorum: 3,
    approvals: 2,
  };
  assert.equal(
    client.governance(base, { now: 1_700_000_100n, predecessorSatisfied: true }).lifecycle,
    "pending-approvals",
  );
  assert.equal(
    client.governance(
      { ...base, approvals: 3 },
      { now: 1_699_999_999n, predecessorSatisfied: true },
    ).lifecycle,
    "timelocked",
  );
  assert.equal(
    client.governance(
      { ...base, approvals: 3 },
      { now: 1_700_000_100n, predecessorSatisfied: false },
    ).lifecycle,
    "blocked-predecessor",
  );
  assert.equal(
    client.governance(
      { ...base, approvals: 3 },
      { now: 1_700_003_600n, predecessorSatisfied: true },
    ).lifecycle,
    "expired",
  );
});

test("portfolio aggregation reports concentration and total shortfall", () => {
  const stressed = portfolioStress([
    { id: "rtgs", input, policy },
    {
      id: "instant",
      input: {
        reserveAvailable: 50_000n,
        reserveLocked: 0n,
        pendingGross: 90_000n,
        expectedFees: 200n,
        largestCounterparty: 60_000n,
      },
      policy,
    },
  ]);
  assert.equal(stressed.lanes.length, 2);
  assert.equal(stressed.aggregate.totalPending, 270_000n);
  assert.equal(stressed.aggregate.shortfall > 0n, true);
  assert.equal(stressed.aggregate.policySatisfied, false);
  assert.equal(stressed.aggregate.digest.length, 64);
  assert.equal(stressed.aggregate.hhiBps > 5_000n, true);
});

test("integer JSON parser preserves signed 64-bit values", () => {
  const decoded = parseIntegerJson('{"max":9223372036854775807,"min":-9223372036854775808}');
  assert.equal(decoded.max, 9_223_372_036_854_775_807n);
  assert.equal(decoded.min, -9_223_372_036_854_775_808n);
  assert.throws(() => atomic("1.25"), /must be an integer/);
});
