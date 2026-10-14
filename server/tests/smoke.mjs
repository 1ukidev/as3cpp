// End-to-end check of the language server over JSON-RPC.
// Usage: node smoke.mjs <server executable> <project folder> [sdk folder]
import { spawn } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const [serverPath, projectArg, sdkPath] = process.argv.slice(2);
if (!serverPath || !projectArg) {
  console.error('Usage: node smoke.mjs <server executable> <project folder> [sdk folder]');
  process.exit(2);
}
const project = resolve(projectArg);
const server = spawn(serverPath, [], { stdio: ['pipe', 'pipe', 'inherit'] });

let buffer = Buffer.alloc(0);
let nextId = 1;
const pending = new Map();
const logs = [];
let onLog = () => {};
let onDiagnostics = () => {};

server.stdout.on('data', (chunk) => {
  buffer = Buffer.concat([buffer, chunk]);
  while (true) {
    const headerEnd = buffer.indexOf('\r\n\r\n');
    if (headerEnd < 0) return;
    const length = Number(/Content-Length: (\d+)/i.exec(buffer.subarray(0, headerEnd).toString())[1]);
    if (buffer.length < headerEnd + 4 + length) return;
    const message = JSON.parse(buffer.subarray(headerEnd + 4, headerEnd + 4 + length).toString());
    buffer = buffer.subarray(headerEnd + 4 + length);
    if (message.id !== undefined && pending.has(message.id)) {
      pending.get(message.id)(message);
      pending.delete(message.id);
    } else if (message.method === 'textDocument/publishDiagnostics') {
      onDiagnostics(message.params);
    } else if (message.method === 'window/logMessage') {
      logs.push(message.params.message);
      onLog(message.params.message);
    }
  }
});

function send(message) {
  const body = JSON.stringify({ jsonrpc: '2.0', ...message });
  server.stdin.write(`Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`);
}

function request(method, params) {
  const id = nextId++;
  send({ id, method, params });
  return new Promise((resolveRequest) => pending.set(id, resolveRequest));
}

function positionOf(text, needle, offset = 0) {
  const index = text.indexOf(needle);
  if (index < 0) throw new Error(`"${needle}" not found`);
  const before = text.slice(0, index + offset).split('\n');
  return { line: before.length - 1, character: before.at(-1).length };
}

let failures = 0;
function check(name, condition, detail) {
  console.log(`${condition ? 'ok  ' : 'FAIL'} ${name}${condition ? '' : `\n     ${JSON.stringify(detail)?.slice(0, 400)}`}`);
  if (!condition) failures++;
}

const started = performance.now();
const indexed = new Promise((resolveIndexed) => {
  onLog = (message) => message.startsWith('Indexed') && resolveIndexed(message);
});
const init = await request('initialize', {
  processId: process.pid,
  rootUri: pathToFileURL(project).href,
  capabilities: {},
  initializationOptions: { sdkPath },
});
check('initialize', init.result?.capabilities?.completionProvider, init);
send({ method: 'initialized', params: {} });
console.log(`     ${await indexed} (${Math.round(performance.now() - started)} ms total)`);

function nextDiagnostics(uri) {
  return new Promise((resolveDiagnostics) => {
    onDiagnostics = (params) => params.uri === uri && resolveDiagnostics(params.diagnostics);
  });
}

const mainPath = join(project, 'src', 'Main.as');
const uri = pathToFileURL(mainPath).href;
const original = readFileSync(mainPath, 'utf8');
const text = original.replace('addChild(player);', 'addChild(player);\n            player.');
const opened = nextDiagnostics(uri);
send({ method: 'textDocument/didOpen', params: { textDocument: { uri, languageId: 'actionscript', version: 1, text } } });
const textDocument = { uri };
check('no diagnostics for valid code', (await opened).length === 0, await opened);

const completion = await request('textDocument/completion', {
  textDocument,
  position: positionOf(text, 'player.\n', 7),
  context: { triggerKind: 2, triggerCharacter: '.' },
});
const labels = completion.result?.items?.map((item) => item.label) ?? [];
check('completion lists Player members', labels.includes('moveTo') && labels.includes('lives'), labels);
if (sdkPath) {
  check('completion lists inherited SDK members', labels.includes('graphics') && labels.includes('addChild'), labels);
}

const definition = await request('textDocument/definition', { textDocument, position: positionOf(text, 'moveTo(', 2) });
check('definition of moveTo', definition.result?.[0]?.uri?.endsWith('game/Player.as'), definition.result);

const typeDefinition = await request('textDocument/definition', { textDocument, position: positionOf(text, 'Player;', 1) });
check('definition of imported class', typeDefinition.result?.[0]?.range?.start?.line === 7, typeDefinition.result);

const hover = await request('textDocument/hover', { textDocument, position: positionOf(text, 'moveTo(', 2) });
check('hover shows docs', hover.result?.contents?.value?.includes('Moves the player'), hover.result);

if (sdkPath) {
  const sdkHover = await request('textDocument/hover', { textDocument, position: positionOf(text, 'addChild(', 2) });
  check('hover on SDK method', sdkHover.result?.contents?.value?.includes('addChild(child:DisplayObject)'), sdkHover.result);
}

const signature = await request('textDocument/signatureHelp', { textDocument, position: positionOf(text, 'stage.stageWidth', 0) });
check('signature help', signature.result?.signatures?.[0]?.label === 'moveTo(x:Number, y:Number):void', signature.result);

const symbols = await request('textDocument/documentSymbol', { textDocument });
check('document symbols', symbols.result?.[0]?.name === 'Main' && symbols.result[0].children.length === 3, symbols.result);

const workspace = await request('workspace/symbol', { query: 'Player' });
check('workspace symbols', workspace.result?.some((symbol) => symbol.name === 'Player' && symbol.kind === 5), workspace.result);

const started2 = performance.now();
for (let i = 0; i < 200; i++) {
  await request('textDocument/completion', { textDocument, position: positionOf(text, 'player.\n', 7) });
}
console.log(`     200 completions in ${Math.round(performance.now() - started2)} ms`);

const broken = text.replace('addChild(player);', 'addChild(player;');
const changed = nextDiagnostics(uri);
send({ method: 'textDocument/didChange', params: { textDocument: { uri, version: 2 }, contentChanges: [{ text: broken }] } });
const errors = await changed;
check('diagnostics after an edit', errors.length === 1 && errors[0].message.includes("'(' is not closed"), errors);
const closed = nextDiagnostics(uri);
send({ method: 'textDocument/didClose', params: { textDocument: { uri } } });
check('diagnostics cleared on close', (await closed).length === 0, await closed);

await request('shutdown', null);
send({ method: 'exit' });
server.on('exit', (code) => {
  check('exit code', code === 0, code);
  process.exit(failures === 0 ? 0 : 1);
});
