# Editor screenshots

These captures show the Paradox Modding Toolkit plugin 0.2.2 candidate with px-lsp 0.3.8 in Notepad++ 8.9.8 (Windows x64, dark mode). The scripts belong to the repository's synthetic CK3 test mod. No game installation is needed for these examples. The captured run passed all 57 editor assertions. Mouse pointers and text carets are absent.

## Outline and highlighting

The outline lists the scripted effect and its nested `if` block. The editor shows syntax colors and fold controls beside the script.

![Highlighted CK3 scripted effect with its effect and nested if block listed in the Outline tab](outline.png)

## Cross-file references

Find references lists both the effect's definition and its use in an event. Open a result to jump to that location.

![References tab listing a scripted effect declaration and its use in a separate event file](references.png)

## Rename preview

The Changes tab shows the old and new names in both files. The buffers remain unchanged until **Apply preview** is selected.

![Rename preview showing two edits from px_smoke_effect to px_smoke_renamed](rename-preview.png)

## Localization quick fix

A missing game-concept localization key has a quick fix. Its preview shows the localization file that will be created before the change is applied.

![Localization quick-fix preview showing a Create file operation in the Changes tab](quick-fix.png)

## Options

Choose the game, documentation folders, localization language, completion behavior and editor features in the options window. The empty server override uses the packaged server. Automatic update checks are disabled for this isolated test session.

![Paradox Toolkit options with game and language selectors, folder browse buttons, and editor feature controls](options.png)

## Refresh the captures

From the repository root, run `test/native-smoke.ps1 -Dark` after preparing the package and portable editor as described in [Contributing](../../CONTRIBUTING.md#test-editor-changes). The runner uses a separate server cache and a larger editor window and docked panel so result rows remain visible with display scaling. It writes images and test results into a new `build/native-smoke-*` directory. Captures hide Scintilla and native edit carets; `PrintWindow` does not composite the mouse pointer. Check that `done.txt` reports `0 failures`, inspect each image for cursors and clipped results, then copy the five matching PNG files into this directory. Update the version information above and in the [main README](../../README.md#screenshots) when the capture environment changes.
