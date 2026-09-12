# Notepad++ integration decisions and validation

Researched and tested on 2026-09-10 through 2026-09-12. The current checks cover the prepared, unreleased 0.2.1 changes. The implementation uses native dialogs and the existing C++ client. No browser view or additional UI framework is required.

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
| Release x64 and x86 builds and unit suites | Passed: classification, framing, markdown, lexical colors, UTF-16 positions, invalid/overlapping edit rejection, CreateFile merging, nested fold levels, semantic deltas, transport failure/recovery and options persistence, including Unicode paths. |
| x86 and x64 Notepad++ 8.9.8 with LSP 0.3.4 | Each passed all 56 native assertions in light and dark modes, including semantic color retention during typing and undo, change-history gutter preservation, startup notification handling, outline, references, symbols, rename, quick fixes, signature help, stale completion, unsaved-buffer restoration, and custom-language settings. |
| ARM64 | Production DLL, test executable and package cross-built on x64. DLL and bundled Node PE architecture and plugin exports checked. No local ARM64 execution. CI is configured to run the same native checks on an ARM64 Windows host. |
| Wine on Linux/macOS | Experimental and untested. No runnable Wine environment was available. Windows results do not establish Wine compatibility. |
| Updater | Passed: current version, verified download, daily throttle, offline fallback, checksum rejection, compatible asset selection and executable architecture rejection. |
| Screenshots | Captured from actual Notepad++ windows. The runner saves panel, rename, quick-fix, signature and options images with its assertion log. |

The native fixture uses CK3 and small files. This does not establish large-mod performance, complete keyboard accessibility, every split-view/Save As sequence, every third-party plugin combination, or coverage of every game feature. Diagnostics are server-reported results, not a claim that every file has been validated. New-file creation has no editor undo operation; existing-file edits have one undo step per file.

## Settings in 0.2.1

Options shows full game names, folder browse buttons and explanations beside the affected fields. Game installations are not detected automatically. A blank game data folder disables vanilla file indexing; a blank script docs folder uses the selected game's bundled documentation. These are independent settings.

Localization offers the server's nine standard keys and Other for a custom key. The dialog previews the filename suffix and header, stores canonical keys behind the display names, and rejects blank or invalid custom values without overwriting saved settings. Native checks save a built-in language, save and reopen a custom language, and exercise rejection through the dialog. Screenshots were inspected in light and dark modes. Folder-picker interaction and keyboard-only navigation have not been automated.

Each Windows package includes a matching Windows Node runtime. Server updates use an architecture-specific cache and require a matching upstream asset. Wine requires the Windows runtime and launcher to work inside the editor's prefix; managed downloads additionally require Windows PowerShell. Use manual plugin packages when PowerShell is unavailable. See the [installation guide](https://github.com/JDeffner/px-toolkit-notepadpp/wiki/Installation#wine-on-linux-or-macos).

The first x86 native run exposed a startup failure caused by applying the 4 GiB Node heap ceiling to a 32-bit process. x86 now uses 1 GiB, while x64/ARM64 retain 4 GiB. The corrected x86 runs passed in both themes. Use a 64-bit editor for large vanilla or mod indexes.

## LSP reliability fixes (unreleased, 2026-09-11)

Folding now uses Notepad++'s folding margin (3), leaving its change-history margin (2) intact. Previously, replacing the change-history mask with fold markers caused an unsaved-change marker to color the entire line orange. The native regression checks that fold controls occupy one margin and that edited lines retain their change markers in the gutter.

Semantic colors on unchanged tokens now survive edits while the next server response is pending. Their byte ranges shift with inserted or deleted text. Changed tokens lose their cached classification until the server returns a current result. This prevents whole-document switching between lexical and semantic colors on each keystroke. Native tests check color retention immediately after an edit and undo, before the server can respond.

Transport validation accepts both object and array parameters, as JSON-RPC permits. The bundled server's `paradox/indexChanged` notification uses `[null]`; rejecting it previously produced a false language-server error in the status bar. Scalar parameters remain invalid, and feature handlers still validate the payloads they consume. Transport fixtures cover parameter shapes, and the native runner checks that the real index-change notification arrives without a client protocol error.

Completion results and selections now retain their request context, including the caret and request identity. Movement, edits, buffer changes, and restarts invalidate obsolete results. Completion uses the same checked text-edit conversion as formatting and workspace edits; it never extends a server range to a later caret position.

Text-position conversion builds a line index for each snapshot and reuses a forward cursor for ordered positions. This removes repeated document-prefix scans while retaining UTF-16 boundary checks. A 620 KB, 20,000-token fixture that took about 19 seconds during the audit now converts in milliseconds. Tests also cover many tokens on one long line.

The transport writes through a worker with a bounded queue. Shutdown queues its final messages, allows a short exit window, then terminates the process job and cancels I/O before joining the workers. Pending requests receive an error when the session ends. Frame headers are limited to 8 KiB and bodies to 32 MiB; input and output queues are each limited to 64 MiB, with additional frame-count limits of 4,096 and 1,024. Malformed frames end the session. Message-envelope errors and payload exceptions are contained and logged at dispatch.

Tracked editor buffers now outlive server sessions. Initialization resends snapshots for the active mod, including unsaved inactive tabs. Other mods remain tracked but are not sent to that session. Closing a buffer removes it from the registry.

The test executable includes self-contained fixture servers for blocked writes, queue saturation, malformed messages, and recovery. Native regressions cover stale completion, valid completion and undo, inactive unsaved declarations after restart, and isolation and restoration when switching mods. The semantic-style assertion now targets the server-annotated `add_gold` command. The previous byte-2 declaration assertion depended on indexing state; lexical punctuation is checked separately.

User and developer guidance is maintained in the [project wiki](https://github.com/JDeffner/px-toolkit-notepadpp/wiki). These fixes are not present in the released 0.2.0 archive.
