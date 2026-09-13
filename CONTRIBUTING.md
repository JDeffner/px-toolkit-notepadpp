# Contributing

Bug reports, fixes, tests, and documentation are welcome. Joel will look into reported issues, but PX Toolkit for VS Code has priority. He does not use Notepad++ himself, and this plugin is open to a new maintainer. Open an issue to discuss a handover or a substantial change before starting work. Review and release dates are not guaranteed.

Follow the [code of conduct](CODE_OF_CONDUCT.md). Use [Support](SUPPORT.md) for questions and [Security](SECURITY.md) for private vulnerability reports.

## Build from source

Use Windows 10 or later, Git, and Visual Studio 2022 Build Tools with the Desktop development with C++ workload. ARM64 builds also require the MSVC ARM64 build tools. This is a C++17/MSBuild project. A game installation is not needed for the helper and transport tests.

```powershell
git clone https://github.com/JDeffner/px-toolkit-notepadpp.git
cd px-toolkit-notepadpp
.\build.cmd
.\build\x64\Release\PxToolkitTests.exe
.\test\updater-tests.ps1
```

`build.cmd` and `package.cmd` accept `x86`, `x64`, or `arm64`; the default is `x64`. Packaging builds the production DLL, verifies the pinned server and Node archives, checks the DLL and runtime architecture, and creates `build/PxToolkit-NotepadPlusPlus-0.2.1-win-<arch>.zip`. The package version must match the DLL version and, in a tagged CI build, the release tag. This is the Notepad++ plugin version; `server-version.txt` records the separate bundled px-lsp version. Packaging runs the helper and transport tests and the bundled server's version command when the host can execute the target. ARM64 cross-builds on x64 skip those executable checks; run them on ARM64 before claiming runtime validation.

| Argument | MSBuild platform and production directory | Bundled runtime |
|---|---|---|
| `x86` | `Win32`, `build/Win32/Release` | Windows x86 Node 22.23.2 |
| `x64` | `x64`, `build/x64/Release` | Windows x64 Node 24.18.1 |
| `arm64` | `ARM64`, `build/ARM64/Release` | Windows ARM64 Node 22.23.2 |

All packages use the same px-lsp 0.3.4 JavaScript and data. The first package run downloads the required archives. A separate Node installation is not needed. Run architecture package commands one at a time because they share the extracted upstream server directory.

## Test editor changes

Use the native smoke runner for changes to editor notifications, styling, completion, folding, panels, or document synchronization. Download the official [Notepad++ 8.9.8 portable x64 ZIP](https://github.com/notepad-plus-plus/notepad-plus-plus/releases/tag/v8.9.8) into `build` as `npp.8.9.8.portable.x64.zip`, then run:

```powershell
.\package.cmd
.\test\native-smoke.ps1
.\test\native-smoke.ps1 -Dark
```

For x86, use `npp.8.9.8.portable.zip`, run `package.cmd x86`, and add `-Architecture x86` to each smoke command. For ARM64, use `npp.8.9.8.portable.arm64.zip`, run `package.cmd arm64`, and add `-Architecture arm64` on an ARM64 Windows host.

The runner builds a test-only DLL and starts an isolated portable editor with fixture mods. Assertions and screenshots are saved under `build/native-smoke-*`. The runner closes its own test process; `-KeepOpen` leaves it open for inspection. Never install or distribute a DLL from `build/smoke-dll*`. Use the production package.

For manual testing, use a separate portable Notepad++ installation and copies of mod files. Extract the complete packaged `PxToolkit` folder into that editor's `plugins` directory. Restart the editor after replacing a DLL. Mod files must be under a directory containing `descriptor.mod` or `.metadata`.

## Make a focused change

- Inspect the affected code, callers, and tests before editing. Preserve unrelated behavior.
- Keep editor integration here. Game rules and server behavior belong in [Paradox Modding Toolkit](https://github.com/JDeffner/paradox-modding-toolkit). Do not add hand-written lists of game commands to the client.
- Keep Scintilla calls on the editor thread. LSP positions use UTF-16; editor positions use byte offsets. Use the shared conversion and edit validators.
- Validate server data and retain document, revision, and session checks before applying asynchronous results.
- Add tests that reproduce changed behavior. Run the build and relevant checks, and record any checks you could not run. Documentation-only changes need link and content review, not a native editor run.
- Update user documentation when behavior changes. Keep unreleased changes clearly marked.
- Keep generated binaries, credentials, personal paths, and proprietary game assets out of commits. Use small synthetic mod fixtures that you have the right to share.

The [README](README.md#source-layout) maps the source files. [Integration decisions](docs/notepadpp-integration.md) describe the editor and protocol boundaries and the limits of current testing.

## Submit a pull request

Fork the repository and branch from `main`. Describe the problem, the resulting behavior, and how you tested it. Link the related issue when there is one. Include before/after screenshots for visible editor changes. Keep each pull request focused on one outcome.

CI is configured to build and package x86, x64, and ARM64, with an ARM64 Windows runner for ARM64 executable tests. Each architecture runs native smoke tests in light and dark modes; updater tests run in the x64 job. Report local checks in the pull request, including manual testing of behavior outside the smoke fixtures. A passing build does not replace testing through the affected editor feature.

Wine on Linux or macOS is experimental and untested. If testing it, report the host OS, Wine distribution and version, prefix architecture, editor architecture, server startup, and editor features checked. The Windows PowerShell updater is a separate check from the bundled server. See the [Wine setup](https://github.com/JDeffner/px-toolkit-notepadpp/wiki/Installation#wine-on-linux-or-macos).

## License

Contributions are submitted under [GPL-3.0-or-later](LICENSE), the project's existing license. Submit only work you have the right to contribute. Preserve copyright and license notices, and document any new third-party code in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). No separate contributor agreement or copyright assignment is required.
