# Third-party notices

This plugin is GPL-3.0-or-later. Copyright 2026 Joel Deffner.

## Notepad++ plugin headers (`sdk/`)

Vendored verbatim from the Notepad++ repository at commit
`40f896e6f6c49a29b6ca3f559696dd786f40152d` (tag `v8.9.8`):

| File in `sdk/` | Path in notepad-plus-plus |
|---|---|
| `PluginInterface.h` | `PowerEditor/src/MISC/PluginsManager/PluginInterface.h` |
| `Notepad_plus_msgs.h` | `PowerEditor/src/MISC/PluginsManager/Notepad_plus_msgs.h` |
| `menuCmdID.h` | `PowerEditor/src/menuCmdID.h` |
| `Scintilla.h` | `scintilla/include/Scintilla.h` |
| `Sci_Position.h` | `scintilla/include/Sci_Position.h` |

Notepad++ is Copyright (C) Don HO and contributors, licensed GPL-3.0-or-later.
Source: <https://github.com/notepad-plus-plus/notepad-plus-plus>. The full
license text is in `LICENSE`.

`Scintilla.h` and `Sci_Position.h` come from Scintilla, Copyright 1998-2021 by
Neil Hodgson, distributed under the Scintilla license (an MIT-style permissive
license). Source: <https://www.scintilla.org/>.

## nlohmann/json (`third_party/nlohmann/json.hpp`)

Version 3.12.0, the single-header release artifact, unmodified.
Copyright (c) 2013-2025 Niels Lohmann, MIT License.
Source: <https://github.com/nlohmann/json>.

```
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## px-lsp and Node.js (packaged zip only)

Neither is vendored into this repository. `package.cmd` fetches the pinned Windows payload from <https://github.com/JDeffner/paradox-modding-toolkit> and puts it in `PxToolkit\px-lsp\` inside the plugin zip. `server-version.txt` records the bundled server version; `package.ps1` records the upstream release and SHA-256 digest. The automatic updater downloads newer stable payloads into the user's local cache and preserves their accompanying license files. These payloads redistribute:

- **`@px-lsp/server`**, GPL-3.0-or-later, from the Paradox Modding Toolkit. Its
  own `LICENSE` and `THIRD-PARTY-NOTICES.md` travel with it in `px-lsp\`.
- **Node.js** (`node.exe`), an unmodified official nodejs.org win-x64 build,
  under the license text in the accompanying `px-lsp\NODE-LICENSE`. Node.js is
  Copyright Node.js contributors and others: <https://nodejs.org>.

A plugin built from source alone carries neither: the DLL starts whichever
`px-lsp` it finds beside itself or on PATH.

## Derived data

The list of folder names the plugin treats as script (`src/classify.cpp`) is the
union of the folder tables in that repository's per-game schemas
(`packages/server/src/games/<ck3|vic3|eu5>/`). No game knowledge is hand-written
here.
