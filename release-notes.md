# Paradox Modding Toolkit for Notepad++ 0.2.1 (unreleased)

Version 0.2.1 is being prepared. The existing 0.2.0 release does not contain these changes.

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

Prepared Windows packages cover x86, x64, and ARM64 Notepad++. Choose the package matching the editor's architecture. Each includes px-lsp 0.3.4 and a Windows Node runtime. Wine support on Linux and macOS is experimental and untested; the wiki documents its setup requirements.

The x86 launcher uses a 1 GiB JavaScript heap limit. Applying the 4 GiB limit used by the other architectures caused 32-bit Node to fail before server initialization. Use x64 or ARM64 for large indexes.

Automatic LSP updates accept only server archives matching the plugin architecture. When upstream has no matching archive, the bundled server remains available. Under Wine, managed updates require working Windows PowerShell; manual plugin packages provide the update route when it is unavailable.

Save your files and close Notepad++, then extract the entire `PxToolkit` folder into its `plugins` folder and reopen the editor. Existing settings are retained. Plugin DLL updates require a plugin package; LSP updates alone do not install these fixes. A GitHub release does not add the plugin to Plugins Admin.

## Documentation and maintenance

The README and wiki cover installation, settings, editor tools, updates, and troubleshooting. Contribution, support, security, and conduct policies are included with issue and pull request templates. The project uses GPL-3.0-or-later, the same license as PX Toolkit.

Joel does not use Notepad++ himself. He will look into reported issues, but PX Toolkit for VS Code has priority. Anyone interested can open an issue to discuss taking over maintenance.

See [integration validation](docs/notepadpp-integration.md) for the recorded checks and their limits. Build coverage is separate from testing in a live editor; no Linux or macOS Wine run is claimed.
