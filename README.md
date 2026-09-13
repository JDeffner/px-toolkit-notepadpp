# Paradox Modding Toolkit for Notepad++

Language tools for Crusader Kings III, Victoria 3 and Europa Universalis V mods, powered by [px-lsp](https://github.com/JDeffner/paradox-modding-toolkit). Completion, diagnostics, navigation, rename previews, quick fixes, folding and semantic highlighting run inside Notepad++.

**Notepad++ plugin version 0.2.1** adds clearer settings, more Windows architectures and reliability fixes. Each ZIP includes **px-lsp 0.3.4** and Node. The plugin, the language server and the main PX Toolkit have separate version numbers. See the [0.2.1 release notes](https://github.com/JDeffner/px-toolkit-notepadpp/releases/tag/v0.2.1).

## Maintainer wanted

I do not use Notepad++ myself. I will look into reported issues, but [PX Toolkit for VS Code](https://github.com/JDeffner/paradox-modding-toolkit) has priority. Anyone interested is welcome to take over maintenance of this plugin. [Open an issue](https://github.com/JDeffner/px-toolkit-notepadpp/issues) to report a problem or discuss the handover.

## Install

The 0.2.1 packages target Windows 10 or later. Choose the package matching the architecture of **Notepad++**, even if Windows has a different architecture. Native editor tests use Notepad++ 8.9.8; see [validation and limits](docs/notepadpp-integration.md#validation).

| Notepad++ architecture | 0.2.1 package |
|---|---|
| 32-bit x86 | [PxToolkit-NotepadPlusPlus-0.2.1-win-x86.zip](https://github.com/JDeffner/px-toolkit-notepadpp/releases/download/v0.2.1/PxToolkit-NotepadPlusPlus-0.2.1-win-x86.zip) |
| 64-bit x64 | [PxToolkit-NotepadPlusPlus-0.2.1-win-x64.zip](https://github.com/JDeffner/px-toolkit-notepadpp/releases/download/v0.2.1/PxToolkit-NotepadPlusPlus-0.2.1-win-x64.zip) |
| ARM64 | [PxToolkit-NotepadPlusPlus-0.2.1-win-arm64.zip](https://github.com/JDeffner/px-toolkit-notepadpp/releases/download/v0.2.1/PxToolkit-NotepadPlusPlus-0.2.1-win-arm64.zip) |

1. Download the matching ZIP above. Use the `PxToolkit-NotepadPlusPlus` package from this repository; the main toolkit's `px-lsp` ZIP contains only the language server. GitHub's **Source code** downloads require building the plugin.
2. Save your files and close Notepad++.
3. Extract the whole `PxToolkit` folder into its `plugins` folder, then reopen the editor.

Keep the server folder beside the DLL. A separate Node installation is not needed. Existing settings are retained.

For large vanilla or mod indexes, use x64 or ARM64 Notepad++. The x86 server has a 1 GiB JavaScript heap limit because of its smaller address space.

Wine on Linux or macOS uses the matching Windows package inside the same prefix as Notepad++. **Wine support is experimental and untested.** Your Wine distribution must run the editor and its bundled Windows Node runtime. See [Wine setup](https://github.com/JDeffner/px-toolkit-notepadpp/wiki/Installation#wine-on-linux-or-macos), including manual updates when Windows PowerShell is unavailable.

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

The folder browse buttons, friendly labels and localization dropdown below were added in plugin version 0.2.1.

| Option | Meaning |
|---|---|
| Game | Crusader Kings III, Victoria 3 or Europa Universalis V. |
| Game data folder | The game's `game` subfolder, used for vanilla definitions. Browse to it or enter its path. Empty means no vanilla file indexing; game installations are not detected automatically. |
| Script docs folder | Your game-generated `script_docs` dumps. Empty uses bundled documentation for the selected game, independently of the game data folder. |
| Localization language | Select English, French, German, Spanish, Russian, Korean, Simplified Chinese, Japanese or Polish. **Other (custom language)** enables a custom key such as `braz_por`. Default: English. |
| Server override | Advanced: path to an executable or `.cmd` launcher. Empty uses the managed server. |
| Show completion / argument help while typing | Control requests while typing. Manual commands remain available. |
| Syntax colors / Extra colors from language server | Control lexical colors and additional server colors. Server colors require syntax colors. |
| Code folding | Enable or disable server fold ranges. |
| Completion inserts / Hover detail | Choose **Minimal structure**, **Example blocks** or **Names only** for completion, and **Compact**, **Standard** or **Full** hover documentation. |
| Check for language server updates | Enable or disable background server checks. |

**Open settings** still opens `px-toolkit.ini` in Notepad++'s plugin config folder. Existing values are retained; new feature switches default to enabled.

## Updates

**The LSP updates automatically by default. The plugin DLL does not.**

At the first managed server start in each Notepad++ session, the plugin starts a background check, at most once per 24 hours. **Check server updates** bypasses that interval. Downloads go into `%LOCALAPPDATA%\PxToolkit\servers\<arch>`, where `<arch>` is `x86`, `x64` or `arm64`, without administrator access.

The updater verifies the release asset's SHA-256 digest, checks required files and runs the downloaded server's version command before making it available. A new server starts after **Restart server** or the next Notepad++ launch. A running server is never replaced. Offline checks and invalid downloads leave the existing server available. The cache's `update.log` records details.

A custom launcher bypasses managed updates. Disabling checks still permits use of a version already downloaded. **Plugin releases** opens this plugin's release page. Install a release and restart Notepad++ to update the DLL. Publishing a GitHub release does not put a plugin into Notepad++'s Plugins Admin catalog.

Managed updates require an upstream server archive for the plugin's architecture. The pinned upstream release provides x64; x86 and ARM64 packages use their bundled server until a matching archive is available. Under Wine, managed downloads also need working Windows PowerShell. If it is unavailable, disable automatic checks and install plugin packages manually.

## Build and test

Install Visual Studio 2022 Build Tools with the Desktop development with C++ workload. ARM64 builds also need the MSVC ARM64 build tools.

```powershell
.\build.cmd
.\build\x64\Release\PxToolkitTests.exe
.\package.cmd
.\test\updater-tests.ps1
```

`build.cmd` and `package.cmd` accept `x86`, `x64` or `arm64`; the default is `x64`. For example, `package.cmd x86` creates `build/PxToolkit-NotepadPlusPlus-0.2.1-win-x86.zip`. Packaging builds the production DLL, verifies the pinned server and runtime archives, and runs tests where the build host can execute the target. See [Contributing](CONTRIBUTING.md) for build and test details.

For live editor tests, put the official `npp.8.9.8.portable.x64.zip` archive in `build`, run `package.cmd`, then run:

```powershell
.\test\native-smoke.ps1
.\test\native-smoke.ps1 -Dark
```

The runner builds a separate test DLL, launches an isolated portable Notepad++, exercises the real LSP and Scintilla controls, and writes assertions and screenshots under `build/native-smoke-*`. It exits with an error on failure and closes its test process. Add `-KeepOpen` to inspect fixture tabs or `-Architecture x86` or `-Architecture arm64` to use another matching package and portable editor on a compatible host. Never distribute a DLL from `build/smoke-dll*`; use the production package.

See the [project wiki](https://github.com/JDeffner/px-toolkit-notepadpp/wiki) for installation, features, troubleshooting and development guidance. [Integration decisions and validation](docs/notepadpp-integration.md) records sources and test limits.

## Contributing and support

See [Contributing](CONTRIBUTING.md) for setup, tests, and pull requests, and [Support](SUPPORT.md) for questions and bug reports. Report vulnerabilities privately using the [security policy](SECURITY.md). The [code of conduct](CODE_OF_CONDUCT.md) applies to project discussions and contributions.

## Source layout

`plugin.cpp` connects Notepad++ notifications to LSP requests and editor changes. `panel.cpp` and `plugin.rc` implement the native panel and options. `lspfeatures.cpp` validates workspace edits, fold ranges and semantic tokens. `lspclient.cpp` owns the server process and pipes. Its reader posts responses to the UI thread; editor calls run there. Document revisions change immediately on edits, while synchronization is debounced by 150 ms.

## License

Copyright 2026 Joel Deffner. This project uses **GPL-3.0-or-later**, the same license as [Paradox Modding Toolkit](https://github.com/JDeffner/paradox-modding-toolkit). You may redistribute and modify it under the GNU General Public License, version 3 or, at your option, any later version. It comes without warranty. See [LICENSE](LICENSE) for the full terms and [third-party notices](THIRD-PARTY-NOTICES.md) for bundled dependencies.
