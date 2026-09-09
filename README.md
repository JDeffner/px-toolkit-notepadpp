# Paradox Modding Toolkit for Notepad++

A Notepad++ plugin that runs the [px-lsp](https://www.npmjs.com/package/@px-lsp/server)
language server and shows its answers in the editor: diagnostics, completion,
hover, go to definition and document formatting for Crusader Kings III,
Victoria 3 and Europa Universalis V mod files.

Notepad++ has no LSP support of its own, so this repository is also a worked
example of wiring a language server into an editor that gives you nothing but a
DLL entry point. If you want a generic LSP client for Notepad++ instead of this
Paradox-specific one, use [Ekopalypse's NppLspClient](https://github.com/Ekopalypse/NppLspClient).

**Status:** 0.1.0 includes px-lsp 0.3.3, a bundled Node runtime and automatic LSP updates. Built and tested on Windows 10, with a live connection and mod indexing verified in Notepad++.

## Requirements

Notepad++ 8.x, 64-bit. Nothing else: the release zip carries the language
server and the Node runtime it needs (an unmodified official nodejs.org
win-x64 build), so no Node and no npm install is required.

## Install

1. Take `PxToolkit-<version>-win-x64.zip` from [Releases](https://github.com/JDeffner/px-toolkit-notepadpp/releases), or build one
   with `package.cmd` (see below).
2. Extract it whole into the `plugins\` folder inside your Notepad++ folder. It
   contains one `PxToolkit\` folder holding the DLL and the server beside it.
3. Restart Notepad++.

The DLL alone will not work: without its `px-lsp\` neighbour the plugin finds no
server and says so.

## Settings

`px-toolkit.ini` is created in the plugin config directory on first run
(`Plugins > Paradox Modding Toolkit > Open settings` opens it). Edit it, then
pick `Restart server`, which is what applies changes.

| Key | Meaning |
|---|---|
| `serverCommand` | Command that starts the server. Empty uses the bundled `px-lsp\px-lsp.cmd` next to the DLL, and falls back to `px-lsp` on PATH. |
| `gameId` | `ck3`, `vic3` or `eu5`. One server instance serves one game. |
| `gamePath` | The game's `game/` folder, the source of vanilla definitions. |
| `logsPath` | Folder holding the `script_docs` dumps the game writes. |
| `locLanguage` | Localization language for previews and coverage, default `english`. |

## Automatic LSP updates

The zip includes px-lsp 0.3.3. When the plugin first starts a server in a Notepad++ session, it checks the latest stable upstream release in the background, at most once every 24 hours. Editing can start immediately with the bundled or previously downloaded server.

Updates go into `%LOCALAPPDATA%\PxToolkit\servers`, so they need no administrator access. The updater verifies GitHub's SHA-256 digest, checks the payload files and runs the server's version command before activating a download. It keeps existing versions and never replaces a running server. The new version starts when you next launch Notepad++ or select **Restart server**. Network errors and failed validation leave the current server available.

`update.log` in that cache folder records downloads and failures. To force a check, run `update-server.ps1 -Force` from the installed `PxToolkit` folder. A non-empty `serverCommand` selects your own server and disables the automatic update path. Plugin DLL updates still require a new plugin installation.

## What works

Diagnostics, completion, hover, go to definition and formatting. What each of
those covers per file type is the server's own table, under
["What works where"](https://github.com/JDeffner/paradox-modding-toolkit/blob/main/packages/server/README.md#what-works-where).

The plugin declares no snippet support, because Scintilla has no tabstops, so
every insert arrives as plain text.

A buffer is only sent to the server when it sits inside a mod: the nearest
ancestor folder holding `descriptor.mod` (CK3) or a `.metadata` folder
(Victoria 3, EU5). Inside one, `.gui` files, `.yml` files under a `localization`
folder and `.txt` files under a script folder are opened as `paradox-gui`,
`paradox-loc` and `paradox` respectively.

## Menu

| Item | Does |
|---|---|
| Complete (Ctrl+Space) | Asks for completion at the caret. Typing an identifier character asks too. |
| Go to definition (F12) | Opens the target and places the caret. Several results open the first. |
| Format document | Applies the server's edits in one undo step. |
| Scope at caret | `paradox/scopeAt`, shown as a calltip. Several scopes read `a\|b`, none reads `unknown`. |
| Insert snippet | `paradox/snippets` as a Scintilla user list; the chosen entry's plain form is inserted. |
| Reload script_docs | `paradox/reloadDocs`, after dumping fresh logs from the game. |
| Show server log | Opens the collected `window/logMessage` lines. Read this first when completion is empty. |
| Open settings | Opens `px-toolkit.ini`. |
| Restart server | Stops the server and starts it again with the current settings. |

The status bar's document-type field carries `paradox/status`: the definition and
token counts, or `PX: indexing...` while the index is being built.

## How it is wired

Each LSP step lives in one place.

| Step | File |
|---|---|
| Spawning `px-lsp --stdio`, `NODE_OPTIONS`, `shutdown`/`exit`, JSON-RPC ids | [`src/lspclient.cpp`](src/lspclient.cpp) |
| `Content-Length` frame reassembly across pipe reads | [`src/framing.cpp`](src/framing.cpp) |
| Which files are Paradox files, and which mod they belong to | [`src/classify.cpp`](src/classify.cpp) |
| Scintilla byte offsets to LSP UTF-16 positions and back | [`src/textpos.cpp`](src/textpos.cpp) |
| `initialize`, document sync, diagnostics, completion, hover, definition, formatting, the `paradox/*` requests | [`src/plugin.cpp`](src/plugin.cpp) |
| Hover markdown reduced for a calltip | [`src/markdown.cpp`](src/markdown.cpp) |

Two constraints shape the rest. Scintilla and Notepad++ messages may only be
sent from the UI thread, so the pipe reader thread decodes frames and posts them
to a message-only window that the plugin owns; every callback runs there.
And the server's parse cache is keyed by URI plus version, so document changes
are debounced by 150 ms and each one bumps the version.

## Build

Visual Studio 2022 Build Tools with the C++ workload. No CMake.

```
build.cmd
```

That produces `build\x64\Release\PxToolkit.dll` and
`build\x64\Release\PxToolkitTests.exe`. Run the exe: it covers the file
classification, the offset conversion, the frame parser and the markdown
reduction, which is all the logic that does not need a running editor.

```
package.cmd
```

builds, then downloads the pinned px-lsp win-x64 payload with `curl.exe`
(cached in `build\`, so it is fetched once), unpacks it with `tar.exe` and
writes `build\PxToolkit-<version>-win-x64.zip`. The plugin version, upstream release tag and checksum are in `package.ps1`; the bundled server version is in `server-version.txt`. The payload is never
committed here.

## License

GPL-3.0-or-later, copyright 2026 Joel Deffner. See `THIRD-PARTY-NOTICES.md` for
the vendored Notepad++ headers and nlohmann/json.
