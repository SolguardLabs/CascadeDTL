import { existsSync, mkdirSync, rmSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const outDir = join(root, "out");
const exeName = process.platform === "win32" ? "cascadedtl.exe" : "cascadedtl";
const output = join(outDir, exeName);
const args = new Set(process.argv.slice(2));
const warnings = args.has("--warnings");
const clean = args.has("--clean");

const sources = [
  "src/common.c",
  "src/capital.c",
  "src/governance.c",
  "src/json.c",
  "src/model.c",
  "src/risk.c",
  "src/ledger.c",
  "src/scheduler.c",
  "src/settlement.c",
  "src/reconcile.c",
  "src/report.c",
  "src/main.c",
].map((file) => join(root, file));

function tryRun(command, args, options = {}) {
  return spawnSync(command, args, {
    cwd: root,
    encoding: "utf8",
    stdio: options.stdio ?? "pipe",
    shell: false,
  });
}

function commandExists(command) {
  const result =
    process.platform === "win32"
      ? tryRun("where.exe", [command])
      : spawnSync("sh", ["-c", `command -v "${command.replaceAll('"', '\\"')}"`], {
          cwd: root,
          encoding: "utf8",
          stdio: "pipe",
        });
  return result.status === 0;
}

function compilerCandidates() {
  const explicit = process.env.CC ? [process.env.CC] : [];
  const native =
    process.platform === "win32" ? ["clang", "gcc", "cc", "cl"] : ["cc", "gcc", "clang"];
  const discovered =
    process.platform === "win32" ? findMsvcVcvars().map((path) => `vcvars:${path}`) : [];
  return [...explicit, ...native, ...discovered].filter(
    (value, index, array) => value && array.indexOf(value) === index,
  );
}

function findMsvcVcvars() {
  const candidates = [
    "C:\\Program Files\\Microsoft Visual Studio\\18\\Insiders\\VC\\Auxiliary\\Build\\vcvars64.bat",
    "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat",
    "C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\VC\\Auxiliary\\Build\\vcvars64.bat",
    "C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\VC\\Auxiliary\\Build\\vcvars64.bat",
    "C:\\Program Files\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat",
  ];
  return candidates.filter((path) => existsSync(path));
}

function cmdQuote(value) {
  return `"${String(value).replaceAll('"', '\\"')}"`;
}

function msvcObjectDir() {
  return outDir.replaceAll("\\", "/") + "/";
}

function buildWithMsvc(command) {
  const flags = [
    "/std:c11",
    "/O2",
    "/D_CRT_SECURE_NO_WARNINGS",
    "/Fo" + msvcObjectDir(),
    "/Fe:" + output,
    ...sources,
  ];
  if (warnings) {
    flags.unshift("/WX", "/W4");
  }
  return tryRun(command, flags, { stdio: "inherit" });
}

function buildWithMsvcVcvars(vcvarsPath) {
  const flags = [
    "/std:c11",
    "/O2",
    "/D_CRT_SECURE_NO_WARNINGS",
    "/Fo" + msvcObjectDir(),
    "/Fe:" + output,
    ...sources,
  ];
  if (warnings) {
    flags.unshift("/WX", "/W4");
  }
  const command = `${cmdQuote(vcvarsPath)} >nul && cl ${flags.map(cmdQuote).join(" ")}`;
  return spawnSync(command, {
    cwd: root,
    encoding: "utf8",
    shell: true,
    stdio: "inherit",
  });
}

function buildWithUnixCompiler(command) {
  const flags = ["-std=c11", "-O2", "-I", join(root, "src"), "-o", output, ...sources];
  if (warnings) {
    flags.splice(2, 0, "-Werror", "-Wall", "-Wextra", "-Wpedantic");
  }
  return tryRun(command, flags, { stdio: "inherit" });
}

if (clean) {
  rmSync(outDir, { recursive: true, force: true });
}
mkdirSync(outDir, { recursive: true });

let attempted = [];
let lastStatus = 1;
for (const compiler of compilerCandidates()) {
  if (compiler.startsWith("vcvars:")) {
    attempted.push("msvc-vcvars");
    const result = buildWithMsvcVcvars(compiler.slice("vcvars:".length));
    lastStatus = result.status ?? 1;
    if (lastStatus === 0) {
      console.log(output);
      process.exit(0);
    }
    continue;
  }
  if (!commandExists(compiler)) {
    continue;
  }
  attempted.push(compiler);
  const result = compiler === "cl" ? buildWithMsvc(compiler) : buildWithUnixCompiler(compiler);
  lastStatus = result.status ?? 1;
  if (lastStatus === 0) {
    console.log(output);
    process.exit(0);
  }
}

if (attempted.length === 0) {
  console.error(
    "No C compiler found. Install gcc, clang, cc, or run from a Visual Studio Developer Prompt.",
  );
} else {
  console.error(`Compilation failed with: ${attempted.join(", ")}`);
}
process.exit(lastStatus || 1);
