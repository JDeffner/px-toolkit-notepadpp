# Notepad++ integration decisions and validation

Researched and tested for version 0.2.0 on 2026-09-10/11. The implementation uses native dialogs and the existing C++ client. No browser view or additional UI framework is required.

## Host integration

Notepad++ provides docking through `NPPM_DMMREGASDCKDLG`, `NPPM_DMMSHOW` and `NPPM_DMMHIDE`. Registration keeps its title and module-name strings alive, names `PxToolkit.dll`, and uses a stable plugin command index. Both dialogs register for modeless keyboard handling. Native controls receive the host's dark theme. These choices follow the [plugin API](https://github.com/notepad-plus-plus/notepad-plus-plus/blob/v8.9.8/PowerEditor/src/MISC/PluginsManager/Notepad_plus_msgs.h) and [docking definitions](https://github.com/notepad-plus-plus/notepad-plus-plus/blob/v8.9.8/PowerEditor/src/WinControls/DockingWnd/Docking.h).

Scintilla uses byte offsets; LSP positions use UTF-16 by default. Edits are converted and checked before application, then applied from the end of the document toward the start. Semantic token deltas use the server's legend. Fold levels preserve header state and avoid rewriting unchanged levels. See [Scintilla's reference](https://www.scintilla.org/ScintillaDoc.html) and the [LSP workspace-edit contract](https://github.com/microsoft/language-server-protocol/blob/gh-pages/_specifications/lsp/3.17/types/workspaceEdit.md).

The client waits for initialization before opening documents. Requests carry document revision, buffer identity and server generation checks. Modifications are associated with the notifying Scintilla document pointer. The server runs in a Windows job so stopping the launcher also stops its Node child.

## Server coverage

The upstream [protocol contract](https://github.com/JDeffner/paradox-modding-toolkit/blob/main/docs/PROTOCOL.md), source and live initialization confirm providers for references, rename, document/workspace symbols, code actions, folding, signature help and semantic tokens. The client implements those standard requests and keeps lexical colors as a fallback.

The server's plain-client localization fix uses `CreateFile` plus `TextDocumentEdit`, or appends to an existing localization file. Both forms can be previewed. Unsupported editor-command actions do not execute. Workspace edits are limited to the active mod and preflighted before mutation; existing buffers remain unsaved, and newly created files are written to disk.

Live testing exposed a path separator mismatch: the classifier returned forward slashes for the mod root, but the server's Windows ownership check compared it with backslash paths. Passing the native Windows path restored required-localization diagnostics and their quick fixes.

## Updates

The [official plugin catalog](https://github.com/notepad-plus-plus/nppPluginList) supplies Plugins Admin's entries. PxToolkit was absent from its x64 catalog when checked. A GitHub release alone therefore cannot update this DLL through Plugins Admin. Options states that distinction and links to plugin releases.

The separate LSP updater checks stable upstream assets, verifies their digest and version, then publishes a cache pointer for the next server start. Live cache records showed LSP 0.3.4 downloaded successfully on September 10. Native tests verified both the downloaded server and the packaged 0.3.4 launcher.

## Validation

| Check | Result |
|---|---|
| Release x64 build and unit suite | Passed: classification, framing, markdown, lexical colors, UTF-16 positions, invalid/overlapping edit rejection, CreateFile merging, nested fold levels, semantic deltas and options persistence, including Unicode paths. |
| Notepad++ 8.9.8 with LSP 0.3.4 | Passed in light and dark modes: semantic styles, fold headers and collapse persistence, docked outline, cross-file references, symbol search, rename preview/application, per-file undo, localization fix creation, read-only refusal, preview invalidation after typing, signature calltips, and options save/application. |
| Updater | Passed: current version, verified download, daily throttle, offline fallback and checksum rejection. |
| Screenshots | Captured from actual Notepad++ windows. The runner saves panel, rename, quick-fix, signature and options images with its assertion log. |

The native fixture uses CK3 and small files. This does not establish large-mod performance, complete keyboard accessibility, every split-view/Save As sequence, every third-party plugin combination, or coverage of every game feature. Diagnostics are server-reported results, not a claim that every file has been validated. New-file creation has no editor undo operation; existing-file edits have one undo step per file.
