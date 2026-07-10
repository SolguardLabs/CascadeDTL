import { spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

export const root = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..");
export const binary = join(root, "out", process.platform === "win32" ? "cascadedtl.exe" : "cascadedtl");

export function ensureBuilt() {
  if (existsSync(binary)) {
    return;
  }
  const result = spawnSync(process.execPath, ["scripts/build.mjs"], {
    cwd: root,
    encoding: "utf8",
  });
  if (result.status !== 0) {
    throw new Error(result.stderr || result.stdout || "build failed");
  }
}

export function runCli(args) {
  ensureBuilt();
  const result = spawnSync(binary, args, {
    cwd: root,
    encoding: "utf8",
  });
  if (result.status !== 0) {
    throw new Error(`command failed: ${binary} ${args.join(" ")}\n${result.stderr}`);
  }
  return result.stdout;
}

export function runFixture(name, options = []) {
  const fixture = join("tests", "fixtures", name);
  const stdout = runCli(["run", fixture, "--json", ...options]);
  return JSON.parse(stdout);
}

export function validateFixture(name) {
  const fixture = join("tests", "fixtures", name);
  return runCli(["validate", fixture]).trim();
}

export function byId(collection, id) {
  const found = collection.find((item) => item.id === id);
  if (!found) {
    throw new Error(`missing id ${id}`);
  }
  return found;
}
