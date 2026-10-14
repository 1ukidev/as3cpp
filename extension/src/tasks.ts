import { dirname, relative, sep } from 'node:path';
import * as vscode from 'vscode';
import { compilerSdkPath, configuration, expandHome, serverPath } from './settings';

const TASK_TYPE = 'as3cpp';
// The same source and names as the vscode-as3mxml tasks, so existing "preLaunchTask" values keep working.
const TASK_SOURCE = 'ActionScript';
const PROBLEM_MATCHERS = ['$as3cpp-error', '$as3cpp-warning'];
const PROJECT_GLOB = '**/asconfig*.json';

interface CompileTaskDefinition extends vscode.TaskDefinition {
  type: typeof TASK_TYPE;
  // Path of the asconfig.json file, relative to its workspace folder. In a task of a .code-workspace file,
  // the path starts with the name of the workspace folder.
  asconfig: string;
  debug?: boolean;
  clean?: boolean;
}

interface TaskAction {
  name: string;
  debug?: boolean;
  clean?: boolean;
}

const ACTIONS: TaskAction[] = [
  { name: 'compile debug', debug: true },
  { name: 'compile release', debug: false },
  { name: 'clean', clean: true },
];

interface Project {
  uri: vscode.Uri;
  folder: vscode.WorkspaceFolder;
}

export function registerCompileTasks(context: vscode.ExtensionContext): vscode.Disposable[] {
  const provider = new CompileTaskProvider(context);
  return [
    vscode.tasks.registerTaskProvider(TASK_TYPE, provider),
    vscode.commands.registerCommand('as3cpp.compileModule', () => provider.compileModule()),
  ];
}

class CompileTaskProvider implements vscode.TaskProvider {
  constructor(private readonly context: vscode.ExtensionContext) {}

  async provideTasks(): Promise<vscode.Task[]> {
    const tasks: vscode.Task[] = [];
    for (const project of await findProjects()) {
      for (const action of ACTIONS) {
        const definition: CompileTaskDefinition = {
          type: TASK_TYPE,
          asconfig: relativePath(project),
          ...(action.clean ? { clean: true } : { debug: action.debug }),
        };
        tasks.push(this.createTask(definition, project, `${action.name} - ${projectName(project)}`));
      }
    }
    return tasks;
  }

  resolveTask(task: vscode.Task): vscode.Task | undefined {
    const definition = task.definition as CompileTaskDefinition;
    if (definition.type !== TASK_TYPE || !definition.asconfig) {
      return undefined;
    }
    const project = resolveProject(definition.asconfig, task.scope);
    return project ? this.createTask(definition, project, task.name) : undefined;
  }

  async compileModule(): Promise<void> {
    const projects = await findProjects();
    if (projects.length === 0) {
      void vscode.window.showInformationMessage('No asconfig.json file was found in the workspace.');
      return;
    }
    if (!compilerSdkPath()) {
      void vscode.window.showErrorMessage(
        'No SDK with the Flex compiler (lib/mxmlc.jar) was found. Set "as3cpp.compiler.sdkPath".',
      );
      return;
    }
    const modules = projects
      .map((project) => ({ label: projectName(project), project }))
      .sort((a, b) => a.label.localeCompare(b.label));
    const module =
      modules.length === 1
        ? modules[0]
        : await vscode.window.showQuickPick(modules, { placeHolder: 'Select the module to compile' });
    if (!module) {
      return;
    }
    const mode = await vscode.window.showQuickPick(
      [
        { label: 'Debug', debug: true },
        { label: 'Release', debug: false },
      ],
      { placeHolder: `Compile ${module.label}` },
    );
    if (!mode) {
      return;
    }
    const definition: CompileTaskDefinition = {
      type: TASK_TYPE,
      asconfig: relativePath(module.project),
      debug: mode.debug,
    };
    const name = `compile ${mode.debug ? 'debug' : 'release'} - ${module.label}`;
    await vscode.tasks.executeTask(this.createTask(definition, module.project, name));
  }

  private createTask(definition: CompileTaskDefinition, project: Project, name: string): vscode.Task {
    const args = ['compile', '--project', project.uri.fsPath];
    if (definition.clean) {
      args.push('--clean');
    } else {
      const sdk = compilerSdkPath();
      if (sdk) {
        args.push('--sdk', sdk);
      }
      args.push(`--debug=${definition.debug !== false}`);
      const config = configuration();
      const java = expandHome(config.get<string>('compiler.javaPath', ''));
      if (java) {
        args.push('--java', java);
      }
      for (const argument of config.get<string[]>('compiler.jvmArgs', [])) {
        args.push(`--jvmarg=${argument}`);
      }
    }
    const command = serverPath(this.context) ?? 'as3lsp';
    const execution = new vscode.ProcessExecution(command, args, { cwd: dirname(project.uri.fsPath) });
    const task = new vscode.Task(definition, project.folder, name, TASK_SOURCE, execution, PROBLEM_MATCHERS);
    task.group = definition.clean ? vscode.TaskGroup.Clean : vscode.TaskGroup.Build;
    return task;
  }
}

async function findProjects(): Promise<Project[]> {
  const uris = await vscode.workspace.findFiles(PROJECT_GLOB, '**/node_modules/**');
  const projects: Project[] = [];
  for (const uri of uris) {
    const folder = vscode.workspace.getWorkspaceFolder(uri);
    if (folder) {
      projects.push({ uri, folder });
    }
  }
  return projects;
}

function relativePath(project: Project): string {
  return relative(project.folder.uri.fsPath, project.uri.fsPath).split(sep).join('/');
}

// "asconfig.json" in a single folder workspace, "smt_caixa/asconfig.json" in a multi-root workspace.
function projectName(project: Project): string {
  const path = relativePath(project);
  return (vscode.workspace.workspaceFolders?.length ?? 0) > 1 ? `${project.folder.name}/${path}` : path;
}

function resolveProject(asconfig: string, scope: vscode.Task['scope']): Project | undefined {
  if (typeof scope === 'object') {
    return { uri: vscode.Uri.joinPath(scope.uri, ...asconfig.split('/')), folder: scope };
  }
  if (scope !== vscode.TaskScope.Workspace) {
    return undefined;
  }
  const [folderName, ...rest] = asconfig.split(/[\\/]/);
  const folder = vscode.workspace.workspaceFolders?.find((candidate) => candidate.name === folderName);
  if (!folder || rest.length === 0) {
    return undefined;
  }
  return { uri: vscode.Uri.joinPath(folder.uri, ...rest), folder };
}
