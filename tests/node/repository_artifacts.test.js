import test from "node:test";
import assert from "node:assert/strict";
import {
  verifyArtifacts,
  verifyPrivateBoundary,
  verifyPublicNarrative,
} from "../../scripts/verify-repository.mjs";

test("repository exposes the exact production artifact set", () => {
  const result = verifyArtifacts();
  assert.equal(result.docs, 7);
  assert.equal(result.diagrams >= 10, true);
  assert.equal(result.bannerBytes >= 100_000, true);
});

test("public narrative stays inside the product boundary", () => {
  const result = verifyPublicNarrative();
  assert.equal(result.files > 20, true);
});

test("private material is absent from tracked files", () => {
  assert.deepEqual(verifyPrivateBoundary(), { trackedPrivateFiles: 0 });
});
