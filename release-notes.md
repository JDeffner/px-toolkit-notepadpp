Paradox Modding Toolkit for Notepad++ 0.1.0

Language support for CK3, Victoria 3 and EU5 mod files: diagnostics, completion, hover, go to definition, formatting, scope lookup and snippets.

The zip includes px-lsp 0.3.3 and Node. Extract the whole `PxToolkit` folder into your Notepad++ `plugins` folder, then restart Notepad++.

The plugin checks for stable LSP updates in the background when it first starts the server, at most once a day. Downloads are checked against GitHub's SHA-256 digest and tested before activation. An update applies on the next Notepad++ launch or **Restart server**. Failed downloads leave the current server available. A custom `serverCommand` disables this automatic update path.

Requires 64-bit Notepad++ 8.x on Windows 10 or later.
