# Changelog

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
