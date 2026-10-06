# Releasing the Notepad++ plugin

This procedure covers **plugin 0.2.2**, bundling **px-lsp 0.3.8** from the **PX Toolkit v0.5.5 prerelease**. The upstream prerelease is an explicit dependency choice. Automatic server updates remain restricted to newer stable releases.

## Release identity

The existing `v0.2.1` tag points to the earlier, unreleased build. Use a new `v0.2.2` tag for this release. Do not publish the old draft or move its tag to represent the new code.

Keep the plugin version in `package.ps1`, `src/plugin.rc`, download filenames and release notes consistent. `server-version.txt` contains the independent LSP version; `package.ps1` pins its upstream tag and SHA-256 digest.

## Required checks

1. Run `package.cmd x64`, `package.cmd x86` and `package.cmd arm64` sequentially. Each package checks the DLL version, upstream checksums, executable architecture and required build tests. ARM64 executable checks require an ARM64 Windows host.
2. Run `test/updater-tests.ps1`. It must preserve the bundled 0.3.8 server when the latest stable release offers an older version, and reject prereleases as automatic updates.
3. Run `test/native-smoke.ps1 -Architecture <arch>` and the same command with `-Dark` on a matching host for all three architectures. Use the pinned Notepad++ 8.9.8 portable archives from the build workflow. All assertions must pass.
4. Inspect the captured images. Mouse pointers and text carets must be absent. Refresh the documentation images and their version labels from a passing run.
5. Inspect each ZIP: production `PxToolkit.dll`, the matching Node runtime, LSP 0.3.8, updater, version files, license notices and the README's local image and documentation dependencies must be present. Never package the smoke-test DLL.
6. Record results and remaining limits in [integration validation](notepadpp-integration.md). A previous CI run against LSP 0.3.4 is not validation of this release.

The `build` workflow runs these architecture and theme checks, including native ARM64 tests on `windows-11-arm`. Its publishing job depends on all Windows jobs passing. ARM64 validation is required before release; a local x64/x86 pass is not sufficient.

## Publication

After the maintainer approves publication, land the reviewed changes and confirm the build workflow passes for that exact source. Remove the release-candidate wording in the README, release notes and wiki, and date the 0.2.2 changelog entry. Keep the statement that the bundled server comes from an upstream prerelease.

Create `v0.2.2` at the approved source and push that tag. The tag workflow rebuilds and tests every architecture, then publishes the three `PxToolkit-NotepadPlusPlus-0.2.2-win-<arch>.zip` files using `release-notes.md`. Confirm the resulting assets report plugin 0.2.2 and LSP 0.3.8. If a draft named `v0.2.2` already exists, resolve it before tagging because the workflow creates a new release.

Publish the prepared wiki changes after the release links resolve. Upload `assets/branding/social-preview.png` in the repository's social-preview settings. The README icon is included in source and packages. The old 0.2.1 draft can remain unpublished.

Preparation does not publish a release or change repository settings.
