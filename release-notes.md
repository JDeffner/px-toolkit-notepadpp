# Paradox Modding Toolkit for Notepad++ 0.2.1

**Draft: waiting for the new px-lsp release.** The current draft assets contain px-lsp 0.3.4. Replace them, update the server version below, and rerun all architecture checks before publishing.

This release updates the **Notepad++ plugin to 0.2.1** and bundles **px-lsp 0.3.4** with a Windows Node runtime. These are separate version numbers; 0.2.1 is not the version of the main PX Toolkit or its language server.

## Download

Choose the ZIP matching your **Notepad++ architecture**:

| Notepad++ | Download |
|---|---|
| 64-bit x64 | [PxToolkit-NotepadPlusPlus-0.2.1-win-x64.zip](https://github.com/JDeffner/px-toolkit-notepadpp/releases/download/v0.2.1/PxToolkit-NotepadPlusPlus-0.2.1-win-x64.zip) |
| 32-bit x86 | [PxToolkit-NotepadPlusPlus-0.2.1-win-x86.zip](https://github.com/JDeffner/px-toolkit-notepadpp/releases/download/v0.2.1/PxToolkit-NotepadPlusPlus-0.2.1-win-x86.zip) |
| ARM64 | [PxToolkit-NotepadPlusPlus-0.2.1-win-arm64.zip](https://github.com/JDeffner/px-toolkit-notepadpp/releases/download/v0.2.1/PxToolkit-NotepadPlusPlus-0.2.1-win-arm64.zip) |

Use these complete plugin packages. The main toolkit's `px-lsp` ZIP contains only the language server, and GitHub's **Source code** downloads require building the plugin. No separate Node installation is needed.

Save your files and close Notepad++, then extract the entire `PxToolkit` folder into its `plugins` folder and reopen the editor. The DLL should be at `plugins/PxToolkit/PxToolkit.dll`, with `px-lsp` beside it. Existing settings are retained.

## Settings

Options uses full game names, folder browse buttons, and explanations for game data, script documentation, completion, and hover settings. The localization dropdown includes the server's standard languages and an **Other (custom language)** option for custom keys, with a filename and header preview. Existing settings remain valid.

The game data folder must point to the game's `game` subfolder. Empty disables vanilla file indexing; no game installation is detected automatically. An empty script docs folder uses bundled documentation for the selected game.

## Reliability fixes

- Folding uses Notepad++'s folding margin and preserves the change-history gutter, fixing orange lines after edits.
- Unchanged semantic tokens retain their colors during edits and undo while a new server response is pending.
- Valid JSON-RPC array parameters no longer cause false language-server errors from `paradox/indexChanged`.
- Completion checks its document, revision, caret, request, and server session before display and insertion. Checked server edit ranges cannot consume text between an old response and a new caret position.
- Indexed UTF-16 position conversion removes repeated document-prefix scans for large semantic responses.
- Pipe writes run off the editor thread through a bounded queue. Shutdown cancels blocked I/O and stops the server process tree.
- Restart resends tracked buffers for the active mod, including inactive tabs with unsaved edits. Switching mods keeps their server sessions separate.
- Message validation contains malformed envelopes and payload errors. Pending requests fail when a session ends.
- Framing rejects invalid lengths and enforces message and queue limits.

## Platforms and installation

Windows packages cover x86, x64, and ARM64 Notepad++. Choose the package matching the editor's architecture. Each includes px-lsp 0.3.4 and a Windows Node runtime. Wine support on Linux and macOS is experimental and untested; the wiki documents its setup requirements.

The x86 launcher uses a 1 GiB JavaScript heap limit. Applying the 4 GiB limit used by the other architectures caused 32-bit Node to fail before server initialization. Use x64 or ARM64 for large indexes.

Automatic LSP updates accept only server archives matching the plugin architecture. When upstream has no matching archive, the bundled server remains available. Under Wine, managed updates require working Windows PowerShell; manual plugin packages provide the update route when it is unavailable.

Plugin DLL updates require a plugin package; LSP updates alone do not install these fixes. A GitHub release does not add the plugin to Plugins Admin.

## Documentation and maintenance

The README and wiki cover installation, settings, editor tools, updates, and troubleshooting. Contribution, support, security, and conduct policies are included with issue and pull request templates. The project uses GPL-3.0-or-later, the same license as PX Toolkit.

Joel does not use Notepad++ himself. He will look into reported issues, but PX Toolkit for VS Code has priority. Anyone interested can open an issue to discuss taking over maintenance.

See [integration validation](docs/notepadpp-integration.md) for the recorded checks and their limits. Build coverage is separate from testing in a live editor; no Linux or macOS Wine run is claimed.
