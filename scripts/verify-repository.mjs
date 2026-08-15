import { execFileSync } from "node:child_process";
import { existsSync, readFileSync, readdirSync, statSync } from "node:fs";
import { dirname, extname, join, relative, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

export const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");

export const expectedDocs = [
  "architecture.md",
  "economic-model.md",
  "governance.md",
  "operations.md",
  "sdk.md",
  "security-model.md",
  "settlement-lifecycle.md",
];

const requiredFiles = [
  ".editorconfig",
  ".gitattributes",
  ".github/CODEOWNERS",
  ".github/dependabot.yml",
  ".github/workflows/ci.yml",
  ".github/workflows/release-integrity.yml",
  ".gitignore",
  ".prettierignore",
  "LICENSE",
  "README.md",
  "SECURITY.md",
  "assets/banner.png",
  "package-lock.json",
  "package.json",
  "scripts/build.mjs",
  "scripts/verify-repository.mjs",
  "sdk/cascade-client.mjs",
  "src/capital.c",
  "src/capital.h",
  "src/governance.c",
  "src/governance.h",
];

const excludedDirectories = new Set([
  ".git",
  "assets",
  "node_modules",
  "out",
  "out-linux",
  "private",
]);
const textExtensions = new Set([
  "",
  ".c",
  ".h",
  ".js",
  ".json",
  ".md",
  ".mjs",
  ".sh",
  ".yaml",
  ".yml",
]);
const disallowedWords = [
  ["c", "tf"].join(""),
  ["la", "boratorio"].join(""),
  ["vulnera", "bilidad"].join(""),
  ["vulnera", "ble"].join(""),
  ["ex", "ploit"].join(""),
  ["bu", "g"].join(""),
];

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

function publicTextFiles(directory = root) {
  const files = [];
  for (const entry of readdirSync(directory)) {
    if (excludedDirectories.has(entry)) continue;
    const absolute = join(directory, entry);
    const stats = statSync(absolute);
    if (stats.isDirectory()) {
      files.push(...publicTextFiles(absolute));
      continue;
    }
    const extension = extname(entry).toLowerCase();
    if (textExtensions.has(extension) || entry === ".gitignore" || entry === ".editorconfig") {
      files.push(absolute);
    }
  }
  return files;
}

export function verifyArtifacts() {
  for (const file of requiredFiles) {
    assert(existsSync(join(root, file)), `missing required artifact: ${file}`);
  }
  const docs = readdirSync(join(root, "docs"))
    .filter((name) => name.endsWith(".md"))
    .sort();
  assert(
    JSON.stringify(docs) === JSON.stringify(expectedDocs),
    `docs set is not exact: ${docs.join(", ")}`,
  );

  const banner = statSync(join(root, "assets", "banner.png"));
  assert(banner.size >= 100_000, "banner asset is unexpectedly small");

  const readme = readFileSync(join(root, "README.md"), "utf8");
  assert(readme.includes("./assets/banner.png"), "README does not use the canonical banner");
  assert(readme.includes("Production%201.0.0"), "README does not expose the production release");
  for (const doc of expectedDocs) {
    assert(readme.includes(`docs/${doc}`), `README does not link docs/${doc}`);
  }

  const diagrams = [
    readme,
    readFileSync(join(root, "SECURITY.md"), "utf8"),
    ...docs.map((doc) => readFileSync(join(root, "docs", doc), "utf8")),
  ]
    .join("\n")
    .match(/```mermaid/gu);
  assert((diagrams?.length ?? 0) >= 10, "documentation requires at least ten Mermaid diagrams");
  return { docs: docs.length, diagrams: diagrams?.length ?? 0, bannerBytes: banner.size };
}

export function verifyPublicNarrative() {
  const findings = [];
  for (const file of publicTextFiles()) {
    const content = readFileSync(file, "utf8");
    for (const word of disallowedWords) {
      const expression = new RegExp(`\\b${word}\\b`, "giu");
      if (expression.test(content)) {
        findings.push(`${relative(root, file)}:${word}`);
      }
    }
  }
  assert(findings.length === 0, `public narrative contains reserved terms: ${findings.join(", ")}`);
  return { files: publicTextFiles().length };
}

export function verifyPrivateBoundary() {
  const tracked = execFileSync("git", ["ls-files", "tests/private", "private-notes.md"], {
    cwd: root,
    encoding: "utf8",
  }).trim();
  assert(tracked === "", `private material is tracked: ${tracked}`);
  return { trackedPrivateFiles: 0 };
}

export function verifyRepository() {
  const result = {
    artifacts: verifyArtifacts(),
    narrative: verifyPublicNarrative(),
    boundary: verifyPrivateBoundary(),
  };
  return result;
}

const invokedPath = process.argv[1] ? pathToFileURL(resolve(process.argv[1])).href : "";
if (import.meta.url === invokedPath) {
  console.log(JSON.stringify(verifyRepository()));
}
