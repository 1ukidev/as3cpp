// Builds the native language server with CMake and copies it next to the extension.
import { spawnSync } from 'node:child_process';
import { copyFileSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const extensionDir = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const repositoryDir = resolve(extensionDir, '..');
const preset = process.argv.includes('--debug') ? 'debug' : 'release';
const executable = process.platform === 'win32' ? 'as3lsp.exe' : 'as3lsp';

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

const target = join(extensionDir, 'server', `${process.platform}-${process.arch}`);
mkdirSync(target, { recursive: true });
copyFileSync(join(repositoryDir, 'build', preset, 'server', executable), join(target, executable));
console.log(`Copied ${executable} to ${target}`);
