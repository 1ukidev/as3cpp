# ActionScript 3 (as3cpp)

Fast ActionScript 3 support for VS Code, backed by a native language server written in C++20.

## Features

- Syntax highlighting for `.as` and `.mxml` files.
- Go to definition, hover and completion, also inside MXML scripts and event attributes.
- Automatic `import` for completed types.
- Signature help for methods and constructors.
- Syntax errors while you type.
- Outline and workspace symbol search.
- Flash Player, AIR and Apache Flex classes from the SDK.
- Compilation of modules with `asconfig.json` (**ActionScript: Compile Module**).

## Settings

| Setting | Description |
| --- | --- |
| `as3cpp.sdkPath` | AIR, Flex or Royale SDK. Default: `as3mxml.sdk.framework`, then `AIR_HOME`, `FLEX_HOME` or `ROYALE_HOME`. |
| `as3cpp.compiler.sdkPath` | SDK with the Flex compiler, such as Apache Flex. |
| `as3cpp.sourcePaths` | Extra source directories. |
| `as3cpp.libraryPaths` | Extra `.swc` files or directories. |
| `as3cpp.frameworkLibraries` | Index the Flex framework `.swc` files. Automatic when the workspace has MXML files. |

Disable the "ActionScript & MXML" extension (vscode-as3mxml) in the same workspace.
