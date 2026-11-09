# Blueprint Tag Manager 1.0.0

An Unreal Engine 5.7 editor plugin for managing **Actor Tags** and **Component Tags** across project Actor Blueprints from the main editor. Blueprints do not need to be opened individually.

Windows x64 is supported. Testing used the Epic Games Launcher build of Unreal Engine 5.7.4; other patches and custom engine builds may require rebuilding. macOS and Linux are not supported. See [VALIDATION.md](VALIDATION.md) for the test scope and results.

## Installation

1. Close Unreal Editor and extract `BlueprintTagManager-1.0.0-UE5.7-Win64.zip`.
2. Copy the **BlueprintTagManager** folder into `<YourProject>/Plugins/`. When replacing an existing installation, replace the plugin folder with the release version.
3. Confirm that the descriptor is at `<YourProject>/Plugins/BlueprintTagManager/BlueprintTagManager.uplugin`.
4. Enable **Edit > Plugins > Blueprint Tag Manager** and restart the editor.
5. Open **Tools > Blueprint Tag Manager**, **Window > Blueprint Tag Manager**, or the main toolbar's **Blueprint Tags** button.

Use the separately published release ZIP for prebuilt binaries. GitHub's **Code > Download ZIP** and automatic **Source code (zip)** downloads contain source only. Source builds require a supported compiler and SDK.

## Scanning and browsing

- The default scan scope is `/Game` and enabled project plugins that contain content. Leave the folder field blank for this scope, or enter a subfolder such as `/Game/Characters` and click **Scan project**.
- Saved assets are loaded into memory to read their defaults, without opening their Blueprint editors. Loaded Blueprints are retained while the panel uses them. Large projects can take time to scan.
- Progress shows assets read, Actor Blueprints found, target count, skipped non-Actor assets, and failures. Hover over the progress text for failed asset paths.
- **Cancel scan** keeps partial results and labels them **Canceled: partial results**. An individual asset load must finish before cancellation takes effect.
- Each Blueprint starts collapsed. Its overview summarizes Actor Tags, the union of all component tags, and matching/total component counts. The summary always covers the entire Blueprint, including components hidden by filters.
- Click the **Blueprint name or arrow** to expand or collapse it. The separate **Open Blueprint** button opens the asset. Hover over the name for its full path. **Expand all / Collapse all** changes the presentation only.
- **Actor Tags** appear as a separate row with a blue background and bold blue text. This row edits actor defaults, independently of component tags.
- The **Source** column identifies a component's owning Blueprint, inherited Blueprint, or native C++ class. Actor rows are labeled **Actor defaults**.

## Filters

**StaticMesh only** is on by default. It shows `StaticMeshComponent` and derived component types, including Instanced Static Mesh (ISM) and Hierarchical Instanced Static Mesh (HISM). The actual component class is used; its name and assigned mesh asset do not affect this classification. Actor Tags remain a separate target. Turn the toggle off to include other component types.

The three search fields are independent:

| Field | Searches |
| --- | --- |
| Blueprint search | Blueprint name and asset path |
| Component search | Component name; excludes the Actor Tags row while nonempty |
| Tag search | Actual tag values |

All nonempty fields must match. Within each field, all space-separated terms must match, case-insensitively.

| Tag filter | Behavior |
| --- | --- |
| All | Does not exclude targets based on tag presence |
| With tags | Includes only Blueprints with tags on their actor or a component, and displays only tagged targets |
| Without tags | Retains every Blueprint and its Actor Tags row regardless of actor tag presence; displays only untagged components |

Type and text filters also apply. A Blueprint whose actor and every component have tags is labeled **All targets have tags**. **Actor tags: Present / None** reports actual actor tag presence, not search matching.

## Selection and batch editing

New targets start unchecked. Use individual target checkboxes, a Blueprint's group checkbox, or **Check matches / Uncheck matches**. A group checkbox selects matching editable targets and shows a partial check when only some are selected. Read-only targets cannot be checked.

Batch operations and the inventory include matching targets inside collapsed groups. Collapsing a Blueprint does not change its batch scope. Checked targets excluded by a type, tag, or text filter are excluded from the batch. Check the selected count and preview before applying.

The inventory counts tag occurrences and distinct owning targets across matching results. Click a tag to fill **Existing tag**.

| Operation | Behavior |
| --- | --- |
| Preview add | Adds **New tag** without creating a duplicate |
| Preview rename | Renames **Existing tag** to **New tag**, merging collisions |
| Preview remove | Removes every occurrence of **Existing tag** |
| Preview clear | Clears all tags on selected targets |
| Preview deduplicate | Removes duplicate tags on each target, preserving first-occurrence order |

Review the per-asset before/after list, then click **Apply preview**. The complete batch is validated before tag writes. If a target changed after preview, the entire batch is rejected; refresh and preview again. One **Ctrl+Z / Ctrl+Y** undoes or redoes the whole tag batch across Blueprints.

Apply does not save assets. Use **Compile changed**, then **Save changed...** to compile and save assets edited in the current panel session. Compilation is cancellable; saving uses Unreal's checkout/save dialog. Check Compiler Results or Output Log for failures. Closing the panel leaves applied, unsaved changes in the editor; the normal Save All action is also available.

**Refresh tags** re-reads loaded results. **Scan project** also discovers new, moved, or deleted assets. Compile, Undo/Redo, and property changes trigger a refresh. Scanning and editing are disabled during Play/Simulate.

## Inheritance and scope

- Actor defaults, local Blueprint components, inherited Blueprint components, and native C++ default components are supported.
- Editing an inherited component creates an override in the selected child Blueprint. Parent and child assets can be edited together. A parent asset is directly modified only when its own targets are selected. Normal Unreal inheritance can propagate parent changes to descendants without overrides.
- Components that disallow inherited editing are displayed as read-only. Engine content is outside the editing scope.
- Non-Actor Blueprints, such as Widgets, Animation Blueprints, and Component Blueprints, are counted as skipped.
- Gameplay Tags, dynamically created components, tags overwritten by Construction Script or gameplay code, and level-instance overrides are outside this tool's scope.
- Child Actor Component tags are included when that component passes the type filter. Its spawned actor's Blueprint appears separately if it is in the scan scope.
- FName matching is case-insensitive. Case-only renames are no-ops. Empty inputs and `None` are rejected.

## Building from source

Install Unreal Engine 5.7 and Visual Studio 2022 with **Game development with C++**, a supported MSVC toolset, and Windows SDK. Keep `Scripts` beside the plugin folder and run from the repository or extracted release root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Build-BlueprintTagManager.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.7' -RunTests
```

Set `-EngineRoot` to your engine installation. The script uses the engine's bundled .NET runtime and creates a separate HostProject. It prints the built plugin and checked release ZIP paths. An explicit `-OutputDirectory` must be a new, short ASCII path of at most 100 characters. The default is a new temporary directory.

The build records diagnostic source paths relatively. The public ZIP includes the DLL, module manifest, source, configuration, documentation, scripts, and MIT license. Logs, PDBs, caches, HostProject, and Intermediate are excluded.

## Feedback and contributions

For bug reports, include your Unreal Engine version, Windows version, reproduction steps, and the expected and actual behavior. For feature requests, describe the workflow you want to improve.

When submitting a code change, explain its purpose and include validation results from the source build command with `-RunTests`.

## License

Original plugin code and scripts use the included [MIT License](LICENSE). Unreal Engine is a separate dependency under Epic's license.
