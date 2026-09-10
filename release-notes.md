Paradox Modding Toolkit for Notepad++ 0.2.0

Adds docked Problems, Outline, References, Symbols and Changes tabs; cross-file rename with before/after previews; localization quick fixes; code folding; signature help; and semantic highlighting. Native Options lets users choose their game, data paths, feature switches and LSP update behavior.

Fixes Windows mod-path handling that prevented some mod-specific diagnostics and quick fixes. Document revisions guard delayed responses, and edit previews refuse changed or read-only targets. Existing-file edits remain unsaved and can be undone separately in each file.

Includes px-lsp 0.3.4 and Node. Automatic LSP updates are enabled by default and take effect on the next server restart. Plugin DLL updates require installing a plugin release and restarting Notepad++.

Tested with Notepad++ 8.9.8 x64 on Windows 10, using the real LSP in light and dark modes. Unit tests and updater tests also pass.

Close Notepad++, extract the whole `PxToolkit` folder from the zip into its `plugins` folder, then reopen it. Existing user settings are retained.
