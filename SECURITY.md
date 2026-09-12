# Security policy

## Supported versions

Security fixes target the latest plugin release. Older releases do not have a separate backport policy. The plugin DLL and px-lsp server have separate versions and update paths; include both versions in a report. Check whether the latest releases already address the problem, without running a suspected exploit against your normal files.

Joel maintains this plugin in his available time, with PX Toolkit for VS Code taking priority. There is no guaranteed response or fix deadline.

## Report a vulnerability privately

Use [GitHub private vulnerability reporting](https://github.com/JDeffner/px-toolkit-notepadpp/security/advisories/new) when it is available for this repository. Do not put exploit details, private files, or credentials in a public issue or pull request.

If private reporting is unavailable, [open a security contact request](https://github.com/JDeffner/px-toolkit-notepadpp/issues/new?title=Security%20contact%20request) with no vulnerability details. Wait for a private contact route before sending the report.

GitHub supports private vulnerability reporting only for public repositories where a maintainer has enabled it. Before making this repository public, the maintainer should arrange a private reporting channel and enable GitHub's reporting option as soon as it becomes available.

Include:

- Plugin, px-lsp, Notepad++, and host OS versions, plus how you installed them. For Wine, include its version and prefix architecture.
- A minimal reproduction using synthetic files, or a proof of concept that does not expose private data.
- Expected and actual behavior, the affected files or settings, and the access an attacker would need.
- The impact, such as unintended file writes, command execution, or disclosure of local data.

Reports will be assessed privately. Disclosure and reporter credit can be agreed during the report, including a request to remain anonymous.

## Scope

Report problems in the plugin's LSP transport, server process launch, updater, downloaded-package validation, editor integration, and file-edit paths here. Report problems in px-lsp itself through the [upstream security policy](https://github.com/JDeffner/paradox-modding-toolkit/blob/main/SECURITY.md). If the boundary is unclear, report privately here first. Bugs in Notepad++ or a Paradox game should go to that project's maintainers.

Ordinary incorrect diagnostics, missing completions, and game crashes are bug reports unless they expose a security impact.

## Local execution and downloads

The plugin reads mod buffers and sends them to a local language server. Game data paths and other configured server options are passed to that process. A custom server launcher runs with your Windows account's permissions; configure only launchers you trust.

Managed server updates contact GitHub and download release archives into the local user cache. The updater checks the release asset's SHA-256 digest before activating it. Automatic server checks can be disabled in Options. DLL updates require installing a plugin release and restarting Notepad++.

Formatting, rename, and quick fixes can change mod files. Edit previews leave existing buffers unsaved; a localization fix can create a file on disk. These features and their limits are described in the [README](README.md#editor-tools).
