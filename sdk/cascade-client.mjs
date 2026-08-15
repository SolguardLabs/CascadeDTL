import { createHash } from "node:crypto";
import { spawnSync } from "node:child_process";
import { isAbsolute, relative, resolve } from "node:path";

const BPS = 10_000n;
const MAX_I64 = 9_223_372_036_854_775_807n;
const MIN_I64 = -9_223_372_036_854_775_808n;

function invariant(condition, message) {
  if (!condition) {
    throw new TypeError(message);
  }
}

export function atomic(value, field = "amount") {
  let parsed;
  if (typeof value === "bigint") {
    parsed = value;
  } else if (typeof value === "number" && Number.isSafeInteger(value)) {
    parsed = BigInt(value);
  } else if (typeof value === "string" && /^-?(0|[1-9][0-9]*)$/.test(value)) {
    parsed = BigInt(value);
  } else {
    throw new TypeError(
      `${field} must be an integer encoded as bigint, safe number, or decimal string`,
    );
  }
  invariant(parsed >= MIN_I64 && parsed <= MAX_I64, `${field} is outside the signed 64-bit domain`);
  return parsed;
}

function integer(value, field, minimum, maximum) {
  const parsed = atomic(value, field);
  invariant(
    parsed >= BigInt(minimum) && parsed <= BigInt(maximum),
    `${field} is outside its policy range`,
  );
  return Number(parsed);
}

function ceilDiv(numerator, denominator) {
  invariant(
    numerator >= 0n && denominator > 0n,
    "ceilDiv requires a non-negative numerator and positive denominator",
  );
  return numerator / denominator + (numerator % denominator === 0n ? 0n : 1n);
}

function canonicalId(value, field) {
  invariant(
    typeof value === "string" && value.length > 0 && value.length < 64,
    `${field} must contain 1..63 characters`,
  );
  invariant(!/[|\r\n\0]/u.test(value), `${field} contains a reserved delimiter`);
  return value;
}

export function normalizeCapitalInput(input) {
  invariant(input && typeof input === "object", "capital input is required");
  const normalized = {
    reserveAvailable: atomic(input.reserveAvailable, "reserveAvailable"),
    reserveLocked: atomic(input.reserveLocked, "reserveLocked"),
    pendingGross: atomic(input.pendingGross, "pendingGross"),
    expectedFees: atomic(input.expectedFees, "expectedFees"),
    largestCounterparty: atomic(input.largestCounterparty, "largestCounterparty"),
  };
  for (const [field, value] of Object.entries(normalized)) {
    invariant(value >= 0n, `${field} cannot be negative`);
  }
  invariant(
    normalized.expectedFees <= normalized.pendingGross,
    "expectedFees cannot exceed pendingGross",
  );
  invariant(
    normalized.largestCounterparty <= normalized.pendingGross,
    "largestCounterparty cannot exceed pendingGross",
  );
  return normalized;
}

export function normalizeCapitalPolicy(policy) {
  invariant(policy && typeof policy === "object", "capital policy is required");
  return {
    reserveHaircutBps: integer(policy.reserveHaircutBps, "reserveHaircutBps", 0, 10_000),
    volatilityBps: integer(policy.volatilityBps, "volatilityBps", 0, 10_000),
    liquidityBps: integer(policy.liquidityBps, "liquidityBps", 0, 10_000),
    concentrationBps: integer(policy.concentrationBps, "concentrationBps", 0, 10_000),
    horizonEpochs: integer(policy.horizonEpochs, "horizonEpochs", 1, 365),
    targetCoverageBps: integer(policy.targetCoverageBps, "targetCoverageBps", 10_000, 30_000),
    operationalFloor: atomic(policy.operationalFloor, "operationalFloor"),
  };
}

export function computeCapital(inputValue, policyValue) {
  const input = normalizeCapitalInput(inputValue);
  const policy = normalizeCapitalPolicy(policyValue);
  invariant(policy.operationalFloor >= 0n, "operationalFloor cannot be negative");

  const eligibleRate = BPS - BigInt(policy.reserveHaircutBps);
  const eligibleAvailable = (input.reserveAvailable * eligibleRate) / BPS;
  const eligibleLocked = (input.reserveLocked * eligibleRate) / BPS;
  const eligibleResources = eligibleAvailable + eligibleLocked;
  const volatilityAddon = ceilDiv(input.pendingGross * BigInt(policy.volatilityBps), BPS);
  const liquidityAddon = ceilDiv(
    input.pendingGross * BigInt(policy.liquidityBps) * BigInt(policy.horizonEpochs),
    BPS,
  );
  const concentrationAddon = ceilDiv(
    input.largestCounterparty * BigInt(policy.concentrationBps),
    BPS,
  );
  const stressedObligation =
    input.pendingGross + input.expectedFees + volatilityAddon + liquidityAddon + concentrationAddon;
  const requiredCoverage =
    ceilDiv(stressedObligation * BigInt(policy.targetCoverageBps), BPS) + policy.operationalFloor;
  const surplus = eligibleResources >= requiredCoverage ? eligibleResources - requiredCoverage : 0n;
  const shortfall =
    requiredCoverage > eligibleResources ? requiredCoverage - eligibleResources : 0n;
  const coverageBps =
    requiredCoverage === 0n
      ? eligibleResources === 0n
        ? BPS
        : 30_000n
      : (eligibleResources * BPS) / requiredCoverage;
  const largestShareBps =
    input.pendingGross === 0n ? 0n : ceilDiv(input.largestCounterparty * BPS, input.pendingGross);

  return {
    input,
    policy,
    report: {
      eligibleAvailable,
      eligibleLocked,
      eligibleResources,
      volatilityAddon,
      liquidityAddon,
      concentrationAddon,
      stressedObligation,
      requiredCoverage,
      surplus,
      shortfall,
      coverageBps,
      largestShareBps,
      policySatisfied: shortfall === 0n,
    },
  };
}

export function capitalArguments(inputValue, policyValue) {
  const { input, policy } = computeCapital(inputValue, policyValue);
  return [
    input.reserveAvailable,
    input.reserveLocked,
    input.pendingGross,
    input.expectedFees,
    input.largestCounterparty,
    policy.reserveHaircutBps,
    policy.volatilityBps,
    policy.liquidityBps,
    policy.concentrationBps,
    policy.horizonEpochs,
    policy.targetCoverageBps,
    policy.operationalFloor,
  ].map(String);
}

export function normalizeGovernanceOperation(operation) {
  invariant(operation && typeof operation === "object", "governance operation is required");
  const normalized = {
    protocol: canonicalId(operation.protocol, "protocol"),
    network: canonicalId(operation.network, "network"),
    chainId: integer(operation.chainId, "chainId", 1, 2_147_483_647),
    target: canonicalId(operation.target, "target"),
    selector: canonicalId(operation.selector, "selector"),
    payloadDigest: canonicalId(operation.payloadDigest, "payloadDigest"),
    predecessor: canonicalId(operation.predecessor, "predecessor"),
    salt: canonicalId(operation.salt, "salt"),
    eta: atomic(operation.eta, "eta"),
    expiresAt: atomic(operation.expiresAt, "expiresAt"),
    quorum: integer(operation.quorum, "quorum", 1, 32),
    approvals: integer(operation.approvals, "approvals", 0, 32),
  };
  invariant(
    normalized.eta >= 0n && normalized.expiresAt > normalized.eta,
    "execution window is invalid",
  );
  return normalized;
}

export function governanceArguments(operationValue, evaluationValue) {
  const operation = normalizeGovernanceOperation(operationValue);
  invariant(
    evaluationValue && typeof evaluationValue === "object",
    "governance evaluation is required",
  );
  const now = atomic(evaluationValue.now, "now");
  invariant(now >= 0n, "now cannot be negative");
  invariant(
    typeof evaluationValue.predecessorSatisfied === "boolean",
    "predecessorSatisfied must be boolean",
  );
  return [
    operation.protocol,
    operation.network,
    operation.chainId,
    operation.target,
    operation.selector,
    operation.payloadDigest,
    operation.predecessor,
    operation.salt,
    operation.eta,
    operation.expiresAt,
    operation.quorum,
    operation.approvals,
    now,
    evaluationValue.predecessorSatisfied ? 1 : 0,
  ].map(String);
}

export function parseIntegerJson(text) {
  invariant(typeof text === "string", "JSON payload must be text");
  let encoded = "";
  let inString = false;
  let escaped = false;
  for (let index = 0; index < text.length; index += 1) {
    const current = text[index];
    if (inString) {
      encoded += current;
      if (escaped) {
        escaped = false;
      } else if (current === "\\") {
        escaped = true;
      } else if (current === '"') {
        inString = false;
      }
      continue;
    }
    if (current === '"') {
      inString = true;
      encoded += current;
      continue;
    }
    if (current === "-" || /[0-9]/u.test(current)) {
      const match = text.slice(index).match(/^-?(0|[1-9][0-9]*)/u);
      invariant(match, "unsupported numeric token in JSON payload");
      encoded += `"__cascade_integer__${match[0]}"`;
      index += match[0].length - 1;
      continue;
    }
    encoded += current;
  }
  return JSON.parse(encoded, (_key, value) =>
    typeof value === "string" && value.startsWith("__cascade_integer__")
      ? BigInt(value.slice("__cascade_integer__".length))
      : value,
  );
}

function safeFixturePath(root, fixture) {
  invariant(
    typeof fixture === "string" && fixture.length > 0 && !fixture.includes("\0"),
    "fixture path is invalid",
  );
  const absolute = resolve(root, fixture);
  const rel = relative(root, absolute);
  invariant(
    rel !== "" && !rel.startsWith("..") && !isAbsolute(rel),
    "fixture must stay inside the configured root",
  );
  return absolute;
}

export class CascadeClient {
  constructor({ binaryPath, root = process.cwd(), timeoutMs = 10_000 } = {}) {
    invariant(typeof binaryPath === "string" && binaryPath.length > 0, "binaryPath is required");
    invariant(
      Number.isInteger(timeoutMs) && timeoutMs >= 100 && timeoutMs <= 120_000,
      "timeoutMs is outside 100..120000",
    );
    this.binaryPath = resolve(binaryPath);
    this.root = resolve(root);
    this.timeoutMs = timeoutMs;
  }

  execute(args) {
    invariant(
      Array.isArray(args) && args.every((arg) => typeof arg === "string" && !arg.includes("\0")),
      "arguments must be safe strings",
    );
    const result = spawnSync(this.binaryPath, args, {
      cwd: this.root,
      encoding: "utf8",
      timeout: this.timeoutMs,
      maxBuffer: 8 * 1024 * 1024,
      windowsHide: true,
      shell: false,
    });
    if (result.error) {
      throw result.error;
    }
    if (result.status !== 0) {
      const detail = String(result.stderr || result.stdout || `exit ${result.status}`).trim();
      throw new Error(`CascadeDTL command failed: ${detail}`);
    }
    return String(result.stdout);
  }

  validate(fixture) {
    return this.execute(["validate", safeFixturePath(this.root, fixture)]).trim() === "ok";
  }

  run(fixture, { events = false, strict = false } = {}) {
    const args = ["run", safeFixturePath(this.root, fixture), "--json"];
    if (events) args.push("--events");
    if (strict) args.push("--strict");
    return parseIntegerJson(this.execute(args));
  }

  capital(input, policy) {
    return parseIntegerJson(this.execute(["capital", ...capitalArguments(input, policy)]));
  }

  governance(operation, evaluation) {
    return parseIntegerJson(
      this.execute(["governance", ...governanceArguments(operation, evaluation)]),
    );
  }
}

export function portfolioStress(positions) {
  invariant(
    Array.isArray(positions) && positions.length > 0 && positions.length <= 64,
    "positions must contain 1..64 lanes",
  );
  const lanes = positions.map((position, index) => {
    invariant(position && typeof position === "object", `position ${index} is invalid`);
    const id = canonicalId(position.id, `position ${index} id`);
    return { id, ...computeCapital(position.input, position.policy) };
  });
  const totalPending = lanes.reduce((sum, lane) => sum + lane.input.pendingGross, 0n);
  const eligibleResources = lanes.reduce((sum, lane) => sum + lane.report.eligibleResources, 0n);
  const requiredCoverage = lanes.reduce((sum, lane) => sum + lane.report.requiredCoverage, 0n);
  const shortfall =
    requiredCoverage > eligibleResources ? requiredCoverage - eligibleResources : 0n;
  const shares = lanes.map((lane) =>
    totalPending === 0n ? 0n : (lane.input.pendingGross * BPS) / totalPending,
  );
  const hhiBps = shares.reduce((sum, share) => sum + (share * share) / BPS, 0n);
  const largestLaneShareBps = shares.reduce(
    (largest, share) => (share > largest ? share : largest),
    0n,
  );
  const canonical = lanes
    .map(
      (lane) =>
        `${lane.id}:${lane.input.pendingGross}:${lane.report.requiredCoverage}:${lane.report.shortfall}`,
    )
    .join("|");
  return {
    lanes,
    aggregate: {
      totalPending,
      eligibleResources,
      requiredCoverage,
      surplus: eligibleResources >= requiredCoverage ? eligibleResources - requiredCoverage : 0n,
      shortfall,
      coverageBps: requiredCoverage === 0n ? BPS : (eligibleResources * BPS) / requiredCoverage,
      hhiBps,
      largestLaneShareBps,
      policySatisfied: shortfall === 0n && lanes.every((lane) => lane.report.policySatisfied),
      digest: createHash("sha256").update(`CASCADE_PORTFOLIO_V1|${canonical}`).digest("hex"),
    },
  };
}
