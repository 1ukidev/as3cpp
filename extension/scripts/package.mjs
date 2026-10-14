// Builds the language server for one platform and packages a .vsix that contains only that executable.
// Usage: node package.mjs [--target <platform>-<arch>]
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const extensionDir = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const targetIndex = process.argv.indexOf('--target');
const target = targetIndex >= 0 ? process.argv[targetIndex + 1] : `${process.platform}-${process.arch}`;

function run(command, args) {
  const result = spawnSync(command, args, { cwd: extensionDir, stdio: 'inherit', shell: process.platform === 'win32' });
  if (result.error) {
    throw result.error;
  }
  if (result.status !== 0) {
    process.exit(result.status ?? 1);
  }
}

run('node', [join('scripts', 'build-server.mjs'), '--target', target]);

// The .vscodeignore keeps every executable in server/; this copy keeps only the target's executable.
const ignoreRules = readFileSync(join(extensionDir, '.vscodeignore'), 'utf8').replace('!server/**', `!server/${target}/**`);
const directory = mkdtempSync(join(tmpdir(), 'as3cpp-package-'));
const ignoreFile = join(directory, '.vscodeignore');
writeFileSync(ignoreFile, ignoreRules);
try {
  run('pnpm', [
    'exec', 'vsce', 'package', '--target', target, '--ignoreFile', ignoreFile,
    '--no-dependencies', '--skip-license', '--allow-missing-repository',
  ]);
} finally {
  rmSync(directory, { recursive: true, force: true });
}
