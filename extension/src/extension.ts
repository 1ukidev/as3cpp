import * as vscode from 'vscode';
import { LanguageClient, type LanguageClientOptions, type ServerOptions, TransportKind } from 'vscode-languageclient/node';
import { compilerSdkPath, configuration, expandHome, SECTION, sdkPath, serverPath } from './settings';
import { registerCompileTasks } from './tasks';

const CLIENT_NAME = 'ActionScript 3 (as3cpp)';

interface InitializationOptions {
  sdkPath?: string;
  flexSdkPath?: string;
  sourcePaths: string[];
  libraryPaths: string[];
  frameworkLibraries: boolean;
}

let client: LanguageClient | undefined;
let outputChannel: vscode.LogOutputChannel | undefined;
let pending: Promise<void> = Promise.resolve();

export function activate(context: vscode.ExtensionContext): Promise<void> {
  outputChannel = vscode.window.createOutputChannel(CLIENT_NAME, { log: true });
  context.subscriptions.push(
    outputChannel,
    vscode.commands.registerCommand(`${SECTION}.restartServer`, () => schedule(() => restart(context))),
    vscode.workspace.onDidChangeConfiguration((event) => {
      if (event.affectsConfiguration(SECTION) || event.affectsConfiguration('as3mxml.sdk.framework')) {
        void schedule(() => restart(context));
      }
    }),
    ...registerCompileTasks(context),
  );
  return schedule(() => start(context));
}

export function deactivate(): Promise<void> {
  return schedule(stop);
}

// Runs start and stop operations one after another, so a restart never overlaps another one.
function schedule(operation: () => Promise<void>): Promise<void> {
  pending = pending.then(operation, operation);
  return pending;
}

async function start(context: vscode.ExtensionContext): Promise<void> {
  const command = serverPath(context);
  if (!command) {
    void vscode.window.showErrorMessage(
      `${CLIENT_NAME}: the language server executable was not found. ` +
        'Run "pnpm run build:server" in the extension folder, or set "as3cpp.server.path".',
    );
    return;
  }

  const serverOptions: ServerOptions = { command, transport: TransportKind.stdio };
  const clientOptions: LanguageClientOptions = {
    documentSelector: [
      { scheme: 'file', language: 'actionscript' },
      { scheme: 'file', language: 'mxml' },
    ],
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher('**/*.{as,mxml}'),
    },
    initializationOptions: initializationOptions(),
    outputChannel,
  };

  client = new LanguageClient(SECTION, CLIENT_NAME, serverOptions, clientOptions);
  await client.start();
}

async function stop(): Promise<void> {
  const current = client;
  client = undefined;
  await current?.stop();
}

async function restart(context: vscode.ExtensionContext): Promise<void> {
  await stop();
  await start(context);
}

function initializationOptions(): InitializationOptions {
  const config = configuration();
  return {
    sdkPath: sdkPath(),
    flexSdkPath: compilerSdkPath(),
    sourcePaths: config.get<string[]>('sourcePaths', []).map(expandHome),
    libraryPaths: config.get<string[]>('libraryPaths', []).map(expandHome),
    frameworkLibraries: config.get<boolean>('frameworkLibraries', false),
  };
}
