// Builds the native language server with CMake and copies it next to the extension.
// Usage: node build-server.mjs [--debug] [--target <platform>-<arch>]
// The only cross compilation target is win32-x64, which uses MinGW-w64 (cmake/toolchains).
import { spawnSync } from 'node:child_process';
import { copyFileSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const extensionDir = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const repositoryDir = resolve(extensionDir, '..');
const hostTarget = `${process.platform}-${process.arch}`;

const targetIndex = process.argv.indexOf('--target');
const target = targetIndex >= 0 ? process.argv[targetIndex + 1] : hostTarget;
const debug = process.argv.includes('--debug');

let preset;
if (target === hostTarget) {
  preset = debug ? 'debug' : 'release';
} else if (target === 'win32-x64') {
  preset = 'windows-x64-release';
} else {
  console.error(`Cannot build for ${target} on ${hostTarget}. Supported targets: ${hostTarget}, win32-x64.`);
  process.exit(2);
}
const executable = target.startsWith('win32') ? 'as3lsp.exe' : 'as3lsp';

function run(...args) {
  const result = spawnSync('cmake', args, { cwd: repositoryDir, stdio: 'inherit' });
  if (result.error) {
    throw result.error;
  }
  if (result.status !== 0) {
    process.exit(result.status ?? 1);
  }
}

run('--preset', preset);
run('--build', '--preset', preset);

const destination = join(extensionDir, 'server', target);
mkdirSync(destination, { recursive: true });
copyFileSync(join(repositoryDir, 'build', preset, 'server', executable), join(destination, executable));
console.log(`Copied ${executable} to ${destination}`);
