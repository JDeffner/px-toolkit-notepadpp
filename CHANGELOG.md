# Changelog

## 0.2.2 (2026-10-06)

- Bundle px-lsp 0.3.8 from PX Toolkit 0.5.5, with the release archive verified by SHA-256. Automatic server updates still accept newer stable releases only.
- Declare versioned workspace edits and file creation so the new LSP can offer localization fixes and rename symbols in unsaved buffers.
- Include the README's icon, screenshot gallery and linked documentation in release packages. Hide text carets in screenshot captures.
- Include the settings, architecture and reliability improvements prepared in the unreleased 0.2.1 build: x86 and ARM64 packages, folder browsing, a localization selector, stable semantic colors, correct folding margins, guarded completion, nonblocking transport and unsaved-buffer restoration.
- Add PX NPP branding and document limited maintenance and the invitation for a new maintainer.

## 0.2.0 (2026-09-10)

- Add docked Problems, Outline, References, Symbols and Changes tabs, rename previews, localization quick fixes, folding, signature help and semantic highlighting.
- Add native Options and fix Windows mod-path handling for mod diagnostics and quick fixes.
- Bundle px-lsp 0.3.4.

## 0.1.1 (2026-09-10)

- Add syntax highlighting for Paradox script, GUI and localization files inside a mod. Comments, strings, numbers, keys, operators, boolean literals and variables have distinct styles, with colors for light and dark backgrounds.
- Highlighting works immediately, without waiting for the language server.

## 0.1.0 (2026-09-09)

- Wait for Notepad++ initialization before starting the server for restored tabs.
- Add background LSP updates with a daily check, SHA-256 verification, a version check and offline fallback. Updates apply on the next server start.
- Bundle px-lsp 0.3.3 and include the plugin license notices in the release zip.

- The release zip carries the server. `package.cmd` assembles `PxToolkit.dll`
  plus the pinned px-lsp win-x64 payload (server and Node runtime), and the
  plugin looks for `px-lsp\px-lsp.cmd` next to its own DLL before it searches
  PATH. Installing is now one extraction into `plugins\`, with no Node and no
  npm on the machine.
- First version. Starts `px-lsp --stdio`, syncs Paradox script, localization and
  `.gui` buffers, and shows diagnostics, completion, hover, go to definition and
  document formatting in Notepad++. Adds the server's own surface: scope at
  caret, snippet insertion, a status-bar index readout, script_docs reload and a
  server log.
