# Blueprint Tag Manager 1.0.2

A project-wide Unreal Engine 5.7 editor tool for **Actor Tags** and **Component Tags**. Manage Actor Blueprints from the **main editor**, without opening each Blueprint asset.

**Platform:** Windows x64, tested with the Epic Games Launcher build of UE 5.7.4. See the [validation record](BlueprintTagManager/VALIDATION.md). Other engine patches and custom builds may require rebuilding. macOS and Linux are not supported. The UI is in English.

[User guide](BlueprintTagManager/README.md)

## Project-wide workflow

1. Open **Tools > Blueprint Tag Manager**, **Window > Blueprint Tag Manager**, or the main toolbar's **Blueprint Tags** button.
2. Opening the panel does not scan or load Blueprint assets. In **Project scan**, leave the folder blank for `/Game` and enabled project plugins with content, or enter a subfolder such as `/Game/Characters`. Click **Scan project** to start loading targets.
3. Assets are loaded sequentially to read their defaults. Progress, skipped non-Actor Blueprints, and failed assets are shown. **Cancel scan** leaves clearly marked partial results. Individual asset loads can still take time on large projects. Loaded Blueprints are kept in memory while the panel uses them.
4. Each Blueprint starts **collapsed**, with its Actor Tags, combined component tags, and matching/total component counts summarized on one row. Click its **name or arrow** to expand/collapse the list. The separate **Open Blueprint** button opens the asset; hover for its full path. **Actor Tags** are distinguished by bold text using the normal text color and background. **Expand all / Collapse all** changes the presentation.
5. **StaticMesh only** is **on by default**. Component rows are limited to `StaticMeshComponent` and derived types, including ISM/HISM, using their real class regardless of name or assigned mesh asset. **Exclude HISM** and **Exclude ISM** sit below it and work independently, whether StaticMesh only is on or off. Both exclusions default to off. HISM controls hierarchical instanced components; ISM controls non-hierarchical instanced components. Actor Tags remain a separate target. All filters combine, and excluded components are excluded from batch editing and inventory. The overview and total component count still cover the whole Blueprint.
6. In **Search**, use separate **Blueprint search** (name/path), **Component search** (component name), and **Tag search** (actual tag values). All nonempty fields must match; space-separated terms within each field must all match, case-insensitively. A component-name query excludes the Actor Tags row. The Source column identifies the local Blueprint, inherited Blueprint, or native C++ component class. In **Filters**, **Tag presence: All** includes all tags subject to the type/text filters. **With tags** shows only tagged targets in Blueprints with tags. **Without tags** retains each Blueprint's Actor Tags row regardless of actor tag presence, and shows only untagged components. A Blueprint whose actor and all components have tags is marked **All targets have tags**. Other filters still apply. The overview summarizes all tags in that Blueprint. Actor tag presence is reported as **Present / None**, independently of search matching.
7. Check individual targets, a Blueprint's group checkbox, or **Check matches / Uncheck matches**. New targets start unchecked. Group checkboxes select matching editable targets and show partial selection. Batch edits and inventory include **matching targets inside collapsed groups**; checked targets excluded by a search or component filter are excluded from the batch. Expanding/collapsing does not change the batch scope. Review the selected count and preview before applying.
8. In **Batch edit**, **Checked targets** contains **Tag to add**, **Preview add**, and **Preview clear**. These actions ignore Existing tag; clear ignores every input. **Match an existing tag** contains **Existing tag**, **Replacement tag**, **Preview rename**, and **Preview remove**. Click an inventory tag to fill Existing tag. Rename uses Replacement tag; remove ignores it.
9. Review the per-asset before/after list and click **Apply preview**. The whole batch is validated before tag writes. A stale preview rejects the batch. Parent/child assets can be edited together; inherited components get overrides in each selected Blueprint. One **Ctrl+Z** undoes the entire tag batch.
10. Use **Compile changed**, then **Save changed...** to compile and save the assets edited in this panel session. Compilation is cancellable; save uses Unreal's checkout/save dialog. Applying a preview does not save automatically. Closing the panel keeps the editor's unsaved changes; the normal editor Save All is also available.

**Refresh tags** re-reads loaded results after external changes; **Scan project** discovers newly added, renamed, or removed assets. Compile, Undo/Redo, and property changes trigger a refresh. Editing is disabled during Play/Simulate.

The panel separates **Project scan**, **Search**, **Filters**, **Blueprints & components**, **Tag inventory**, **Batch edit**, and **Change preview** with labeled sections. Drag the dividers to resize the section heights and widths. Drag table header dividers to resize columns. Reduced control sections can be scrolled; size adjustments remain in the current panel session.

## Install a release ZIP

Close Unreal Editor, extract `BlueprintTagManager-1.0.2-UE5.7-Win64.zip`, and copy its **BlueprintTagManager** folder into `<YourProject>/Plugins/`. Replace the previous plugin version when upgrading. The descriptor must be at `<YourProject>/Plugins/BlueprintTagManager/BlueprintTagManager.uplugin`. Enable **Edit > Plugins > Blueprint Tag Manager** and restart.

Use the separately published **release asset** for prebuilt binaries. GitHub's **Code > Download ZIP** and automatic **Source code (zip)** downloads contain source only. C++ projects need a supported compiler when compiling the project. A custom engine or another patch can require rebuilding the plugin.

## Build source

Install UE 5.7 plus Visual Studio 2022 with **Game development with C++**, a supported MSVC toolset, and Windows SDK. Keep `Scripts` beside `BlueprintTagManager`, then run from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Build-BlueprintTagManager.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.7' -RunTests
```

Set `-EngineRoot` to your engine installation. The script creates a fresh disposable HostProject and prints the built plugin and checked release ZIP locations. `-OutputDirectory` must be a fresh, short ASCII path, at most 100 characters. The default is a new `BTM-<timestamp>` folder in the system temporary directory.

## Scope and limits

- Actor Blueprints, local components, inherited Blueprint components, and native C++ default components are supported, including untagged components. Non-Actor Blueprints such as Widgets, Animation Blueprints, and Component Blueprints do not have actor/component defaults of their own for this tool to edit and are counted as skipped.
- Engine content is outside the project editing scope. The main results include current effective inherited tags; editing a child uses its own override. A parent asset is changed only when its own targets are included in the batch. Normal Unreal inheritance can affect descendants that have no overrides.
- Gameplay Tags, dynamically created components, tags overwritten by Construction Script/gameplay code, and level-instance overrides are outside this tool's scope. Child Actor Component tags are included; the spawned child actor's own Blueprint appears separately if it is in the scan scope.
- FName matching is case-insensitive. Case-only renames are no-ops. Empty/None tag inputs are rejected. Read-only inherited components are displayed but cannot be checked.
- The plugin has no network service, account, telemetry, or third-party plugin dependency. Unreal Engine and its tools can have their own network behavior.

## Feedback and contributions

For bug reports, include your Unreal Engine version, Windows version, steps to reproduce the issue, and the expected and actual behavior. A minimal reproduction project or relevant log excerpt can help explain the problem.

For feature requests, describe the task you want to accomplish and how the proposed feature would improve that workflow.

When submitting a code change, explain its purpose and include validation results. Use the source build command above with `-RunTests` to run the plugin's automation tests.

## License

Original code and scripts use the [MIT License](LICENSE); a copy is included in the plugin folder. Unreal Engine remains a separate dependency under [Epic's Unreal Engine license](https://www.unrealengine.com/eula/unreal). The engine SDK is not included.
