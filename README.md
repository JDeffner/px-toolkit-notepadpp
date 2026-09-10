# Paradox Modding Toolkit for Notepad++

Language tools for Crusader Kings III, Victoria 3 and Europa Universalis V mods, powered by [px-lsp](https://github.com/JDeffner/paradox-modding-toolkit). Version 0.2.0 adds docked panels, references, rename previews, quick fixes, folding, signature help, semantic highlighting and native options. The release includes LSP 0.3.4 and Node.

## Install

Use 64-bit Notepad++ on Windows 10 or later. This release was tested with Notepad++ 8.9.8; older versions are not covered by the native test run.

1. Download the zip from [Releases](https://github.com/JDeffner/px-toolkit-notepadpp/releases).
2. Save your files and close Notepad++.
3. Extract the whole `PxToolkit` folder into its `plugins` folder, then reopen the editor.

Keep the server folder beside the DLL. A separate Node installation is not needed. Existing settings are retained.

## Editor tools

Open **Plugins > Paradox Modding Toolkit > Problems and outline** to show the docked panel. Drag its title to move it. Double-click a result, or select it and press Enter, to open its location.

| Tool | How to use it |
|---|---|
| Problems | Shows diagnostics reported by the server. Filter by severity or current file. This is not a scan of every unopened file. |
| Outline | Lists symbols in the active file, including nested symbols. |
| References | Place the caret on a symbol and use **Find references** (Shift+F12). Results include declarations. |
| Workspace symbols | Enter part of a name in the Symbols tab and choose **Find symbols**, or press Enter in the search field. |
| Rename | Place the caret on a symbol, press F2, enter its new name and choose **Preview rename**. Check the before/after rows, then choose **Apply preview**. |
| Quick fixes | Place the caret on a diagnostic and choose **Quick fixes**. Open a fix to preview it, then apply it. The server currently offers localization creation for missing required keys. |
| Folding | Use the editor's fold margin. Fold ranges come from the server; refreshes preserve collapsed blocks. |
| Signature help | Appears at supported trigger characters, or with Ctrl+Shift+Space. Parameterized scripted effects and triggers show their arguments; the current argument is emphasized. |
| Semantic highlighting | Adds server token colors to lexical colors. Lexical highlighting works while the server starts or is unavailable. |

Rename and quick fixes preflight their targets before writing. Existing files change in editor buffers and remain unsaved. Each file has its own undo step. A localization fix can create a new file on disk. Changes to a previewed document invalidate the preview. Read-only targets and edits outside the active mod are refused. File deletion and file renaming operations are not supported.

If an unversioned localization edit targets an already modified buffer, save that target and request the fix again.

Completion (Ctrl+Space), hover, go to definition (F12), formatting, scope at caret and plain-text snippet insertion remain available. Change conflicting shortcuts in Notepad++'s Shortcut Mapper. The plugin does not implement snippet tabstops.

Files must belong to a mod: an ancestor must contain `descriptor.mod` or a `.metadata` folder. Recognized files are `.gui`, localization `.yml`, and `.txt` under script folders such as `common` and `events`. Use UTF-8 for non-ASCII text. Ordinary text files outside mods are unaffected. One server session serves the active mod; changing to another mod restarts it.

Coverage depends on the game, file type and available server data. See the server's [feature table](https://github.com/JDeffner/paradox-modding-toolkit/blob/main/packages/server/README.md#what-works-where). An empty result can mean that the server has no result for that location.

## Options

Choose **Plugins > Paradox Modding Toolkit > Options**. **Save and apply** saves your choices and restarts the server.

| Option | Meaning |
|---|---|
| Game | CK3, Victoria 3 or EU5. |
| Game data folder | The game's `game` folder, used for vanilla definitions. |
| Script docs folder | Your game-generated `script_docs` dumps. Empty uses bundled data. |
| Localization language | Language for localization operations, default `english`. |
| Custom server launcher | Path to an executable or `.cmd` launcher. Empty uses the managed server. |
| Automatic completion / signature help | Control requests while typing. Manual commands remain available. |
| Syntax / semantic highlighting | Control lexical colors and additional server colors. Semantic colors require syntax highlighting. |
| Code folding | Enable or disable server fold ranges. |
| Completion inserts / hover detail | Choose the server's completion mode and hover detail level. |
| Automatically update the LSP | Enable or disable background server checks. |

**Open settings** still opens `px-toolkit.ini` in Notepad++'s plugin config folder. Existing values are retained; new feature switches default to enabled.

## Updates

**The LSP updates automatically by default. The plugin DLL does not.**

At the first managed server start in each Notepad++ session, the plugin starts a background check, at most once per 24 hours. **Check LSP now** bypasses that interval. Downloads go into `%LOCALAPPDATA%\PxToolkit\servers` without administrator access.

The updater verifies the release asset's SHA-256 digest, checks required files and runs the downloaded server's version command before making it available. A new server starts after **Restart server** or the next Notepad++ launch. A running server is never replaced. Offline checks and invalid downloads leave the existing server available. The cache's `update.log` records details.

A custom launcher bypasses managed updates. Disabling checks still permits use of a version already downloaded. **Plugin releases** opens this plugin's release page. Install a release and restart Notepad++ to update the DLL. Publishing a GitHub release does not put a plugin into Notepad++'s Plugins Admin catalog.

## Build and test

Install Visual Studio 2022 Build Tools with the Desktop development with C++ workload.

```powershell
.\build.cmd
.\build\x64\Release\PxToolkitTests.exe
.\package.cmd
.\test\updater-tests.ps1
```

`package.cmd` builds the production DLL, runs unit tests and packages the pinned server after checksum verification. The result is `build/PxToolkit-0.2.0-win-x64.zip`.

For live editor tests, put the official `npp.8.9.8.portable.x64.zip` archive in `build`, run `package.cmd`, then run:

```powershell
.\test\native-smoke.ps1
.\test\native-smoke.ps1 -Dark
```

The runner builds a separate test DLL, launches an isolated portable Notepad++, exercises the real LSP and Scintilla controls, and writes assertions and screenshots under `build/native-smoke-*`. It exits with an error on failure and closes its test process. Add `-KeepOpen` to inspect fixture tabs. Never install the DLL from `build/smoke-dll`; releases use `build/x64/Release`.

See [integration decisions and validation](docs/notepadpp-integration.md) for sources and test limits.

## Source layout

`plugin.cpp` connects Notepad++ notifications to LSP requests and editor changes. `panel.cpp` and `plugin.rc` implement the native panel and options. `lspfeatures.cpp` validates workspace edits, fold ranges and semantic tokens. `lspclient.cpp` owns the server process and pipes. Its reader posts responses to the UI thread; editor calls run there. Document revisions change immediately on edits, while synchronization is debounced by 150 ms.

GPL-3.0-or-later. Copyright 2026 Joel Deffner. See [third-party notices](THIRD-PARTY-NOTICES.md).
