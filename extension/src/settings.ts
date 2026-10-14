import { existsSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';
import * as vscode from 'vscode';

export const SECTION = 'as3cpp';

const EXECUTABLE = process.platform === 'win32' ? 'as3lsp.exe' : 'as3lsp';

export function configuration(): vscode.WorkspaceConfiguration {
  return vscode.workspace.getConfiguration(SECTION);
}

export function expandHome(path: string): string {
  return path === '~' || path.startsWith('~/') ? join(homedir(), path.slice(1)) : path;
}

// The as3lsp executable: the configured one, or the one bundled with the extension.
export function serverPath(context: vscode.ExtensionContext): string | undefined {
  const candidates = [
    expandHome(configuration().get<string>('server.path', '')),
    context.asAbsolutePath(join('server', `${process.platform}-${process.arch}`, EXECUTABLE)),
    context.asAbsolutePath(join('server', EXECUTABLE)),
  ];
  return candidates.find((candidate) => candidate && existsSync(candidate));
}

function sdkCandidates(): string[] {
  const candidates = [
    configuration().get<string>('sdkPath', ''),
    vscode.workspace.getConfiguration('as3mxml').get<string>('sdk.framework', ''),
    process.env.AIR_HOME,
    process.env.FLEX_HOME,
    process.env.ROYALE_HOME,
  ];
  return candidates.map((candidate) => expandHome(candidate ?? '')).filter((candidate) => candidate.length > 0);
}

// The SDK whose libraries the language server reads.
export function sdkPath(): string | undefined {
  return sdkCandidates()[0];
}

// The SDK that compiles projects. It must contain the Flex compiler, which the AIR SDK from Harman does not have.
export function compilerSdkPath(): string | undefined {
  const configured = expandHome(configuration().get<string>('compiler.sdkPath', ''));
  if (configured) {
    return configured;
  }
  return sdkCandidates().find((sdk) => existsSync(join(sdk, 'lib', 'mxmlc.jar')));
}
