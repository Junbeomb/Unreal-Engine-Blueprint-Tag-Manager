# Blueprint Tag Manager 1.0.4

An Unreal Engine editor plugin for managing **Actor Tags** and **Component Tags** across project Actor Blueprints from the main editor. The source targets **UE 5.0 through 5.8**. Blueprints do not need to be opened individually.

Windows x64 is supported. See [VALIDATION.md](VALIDATION.md) for actual build and test results by engine version, including untested versions. Prebuilt DLLs are specific to an engine version; other patches and custom engine builds may require rebuilding. macOS and Linux are not supported.

Verified engine builds are **5.3.2**, **5.5.4**, and **5.7.4**. Other versions in the 5.0-5.8 source target range have not been executed or verified.

## Installation

1. Download **BlueprintTagManager-1.0.4-UE5.0-5.8-Win64.zip**, extract it, and close your project's Unreal Editor.
2. From the extracted folder, run the installer with your project file and engine installation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Install-BlueprintTagManager.ps1 -ProjectFile 'D:\Projects\MyGame\MyGame.uproject' -EngineRoot 'C:\Program Files\Epic Games\UE_5.3'
```

3. The installer creates `<YourProject>/Plugins/BlueprintTagManager/BlueprintTagManager.uplugin`. Enable **Edit > Plugins > Blueprint Tag Manager** and restart if requested.
4. Open **Tools > Blueprint Tag Manager**, **Window > Blueprint Tag Manager**, or the main toolbar's **Blueprint Tags** button.

The single ZIP combines common source with **Prebuilt/UE5.3**, **Prebuilt/UE5.5**, and **Prebuilt/UE5.7** profiles. A matching engine Build ID uses its bundled DLL without compiling. Other versions in the 5.0-5.8 range, custom builds, and different Build IDs build from source using the selected engine. **Source fallback requires a compatible MSVC compiler and Windows SDK.** The six versions outside 5.3, 5.5, and 5.7 are unverified source targets. The installer does not download engines or compilers, edit the project file, or force incompatible DLLs to load.

Add **-Replace** when upgrading. It backs up the existing plugin to `<YourProject>/Saved/BlueprintTagManager/Backups/<timestamp>/BlueprintTagManager` before installing. Without this option, existing installations are refused. Keep the editor closed during upgrades.

Use **-BuildFromSource** to bypass profiles. **-CompilerVersion '14.38.33130'** selects an installed toolset when compiling; **-BuildOutputDirectory** selects a fresh, short ASCII build path. The common BlueprintTagManager folder alone contains source, not a prebuilt DLL; use the installer to merge in the correct profile.

GitHub's **Code > Download ZIP** and automatic **Source code (zip)** downloads do not include Prebuilt profiles. Their installer builds source instead. Keep Scripts beside BlueprintTagManager. Alternatively, run Build-BlueprintTagManager.ps1 as described in the repository README, then copy its completed plugin folder into your project. The common descriptor omits a single EngineVersion pin; each built descriptor and module manifest records its own engine compatibility.

## Scanning and browsing

- Opening the panel leaves it in **Not scanned** state and does not load Blueprint assets. The default scope is `/Game` and enabled project plugins that contain content. Leave the folder blank for this scope, or enter a subfolder such as `/Game/Characters`. Click **Scan project** in the **Project scan** section to begin.
- Saved assets are loaded into memory to read their defaults, without opening their Blueprint editors. Loaded Blueprints are retained while the panel uses them. Large projects can take time to scan.
- Progress shows assets read, Actor Blueprints found, target count, skipped non-Actor assets, and failures. Hover over the progress text for failed asset paths.
- **Cancel scan** keeps partial results and labels them **Canceled: partial results**. An individual asset load must finish before cancellation takes effect.
- Each Blueprint starts collapsed. Its overview summarizes Actor Tags, the union of tags on matching components, and matching/total component counts. **Components (shown)** follows the current type, exclusion, search, and tag-presence filters, including matching children inside collapsed groups. Tags belonging only to excluded components are omitted. Total component counts and **All targets have tags** still describe the whole Blueprint.
- Click the **Blueprint name or arrow** to expand or collapse it. The separate **Open Blueprint** button opens the asset. Hover over the name for its full path. **Expand all / Collapse all** changes the presentation only.
- **Actor Tags** appear as a separate row with bold text and the normal text color and background. This row edits actor defaults, independently of component tags.
- The **Source** column identifies a component's owning Blueprint, inherited Blueprint, or native C++ class. Actor rows are labeled **Actor defaults**.

## Layout and resizing

Labeled sections separate **Project scan**, **Search**, **Filters**, **Blueprints & components**, **Tag inventory**, **Batch edit**, and **Change preview**. Drag the dividers between the main rows to change their heights. Drag dividers between side-by-side sections to adjust the scan/search/filter widths, the inventory/batch-edit widths, and the two batch-operation groups. Drag table header dividers to adjust column widths. The type-filter toggles and **Apply preview** keep their content-sized width instead of stretching across the section.

Control sections can be scrolled after being reduced in size. Layout adjustments remain for the current panel session.

## Search and filters

**StaticMesh only** is on by default. It shows `StaticMeshComponent` and derived component types, including Instanced Static Mesh (ISM) and Hierarchical Instanced Static Mesh (HISM). The actual component class is used; its name and assigned mesh asset do not affect this classification. Actor Tags remain a separate target. Turn the toggle off to include other component types.

Two independent exclusion toggles appear below **StaticMesh only**. Both default to off and keep working when StaticMesh only is off:

| Exclusion | Hides |
| --- | --- |
| Exclude HISM | Hierarchical Instanced Static Mesh components and their subclasses |
| Exclude ISM | Instanced Static Mesh components and their non-hierarchical subclasses; HISM is controlled separately |

Use either toggle independently, or enable both to hide both categories. Enabled exclusion toggles are **red**, while StaticMesh only keeps its normal selection color. Excluded components are left out of the results, component-tag summary, inventory, and batch-edit scope. Total component counts still include them. The summary label is **Components (shown)**; it changes immediately with filters and stays the same when groups are expanded or collapsed.

The three fields in the **Search** section are independent:

| Field | Searches |
| --- | --- |
| Blueprint search | Blueprint name and asset path |
| Component search | Component name; excludes the Actor Tags row while nonempty |
| Tag search | Actual tag values |

All nonempty fields must match. Within each field, all space-separated terms must match, case-insensitively.

| Tag presence in Filters | Behavior |
| --- | --- |
| All | Does not exclude targets based on tag presence |
| With tags | Includes only Blueprints with tags on their actor or a component, and displays only tagged targets |
| Without tags | Retains every Blueprint and its Actor Tags row regardless of actor tag presence; displays only untagged components |

Type and text filters also apply. A Blueprint whose actor and every component have tags is labeled **All targets have tags**. **Actor tags: Present / None** reports actual actor tag presence, not search matching.

## Selection and batch editing

New targets start unchecked. Use individual target checkboxes, a Blueprint's group checkbox, or **Check matches / Uncheck matches**. A group checkbox selects matching editable targets and shows a partial check when only some are selected. Read-only targets cannot be checked.

Batch operations and the inventory include matching targets inside collapsed groups. Collapsing a Blueprint does not change its batch scope. Checked targets excluded by a type, tag, or text filter are excluded from the batch. Check the selected count and preview before applying.

The inventory counts tag occurrences and distinct owning targets across matching results. Click a tag to fill **Existing tag**.

The batch editor separates operations into two groups with independent input fields. Each action has its own block: **Tag to add** and **Preview add** sit together in **Add a tag**, while **Preview clear** has a separate block without an input. Under **Match an existing tag**, the shared **Existing tag** field appears above the operations. **Replacement tag** and **Preview rename** sit together in **Rename a tag**; **Preview remove** is separate below and uses only Existing tag.

| Group | Operation | Inputs and behavior |
| --- | --- | --- |
| Checked targets | Preview add | Adds **Tag to add** without creating a duplicate; ignores Existing tag and Replacement tag |
| Checked targets | Preview clear | Clears all tags on checked matching targets; ignores every tag input |
| Match an existing tag | Preview rename | Renames **Existing tag** to **Replacement tag**, merging collisions; ignores Tag to add |
| Match an existing tag | Preview remove | Removes every occurrence of **Existing tag**; ignores the other inputs |

Every operation applies only to checked matching targets, including the Actor Tags row if checked. Preview deduplicate has been removed; add still avoids adding an existing tag, and rename still merges collisions with its replacement.

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

Install your chosen Unreal Engine version from **5.0 through 5.8** and Visual Studio with **Game development with C++**, an MSVC toolset supported by that engine, and Windows SDK. Keep `Scripts` beside the plugin folder and run from the repository or extracted source root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Build-BlueprintTagManager.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.7' -RunTests
```

Set `-EngineRoot` to your engine installation. The script uses the engine's bundled .NET runtime and creates a separate HostProject. It prints the built plugin and checked release ZIP paths. An explicit `-OutputDirectory` must be a new, short ASCII path of at most 100 characters. The default is a new temporary directory.

The script reads the selected engine's Build.version and detects its bundled .NET layout. Archive names and the generated descriptor use the actual build engine. Select an installed compiler when needed with `-CompilerVersion '14.38.33130'`. Older engines may need an older MSVC toolset even when the newest Visual Studio is installed.

To build all installed versions in the target range, run `Scripts/Build-EngineMatrix.ps1 -RunTests` with a fresh, short ASCII `-OutputDirectory`. It discovers standard Epic and Launcher installations; `-EngineRoots` accepts custom paths, `-MinorVersions` selects minor versions, and `-CompilerVersions` accepts a per-engine hashtable. Each engine gets its own HostProject and archive. `CompatibilityResults.json` records **Built / Failed / NotInstalled** and whether tests ran. `-RequireAllVersions` rejects missing engines before building. Missing installations are never marked as verified.

The build records diagnostic source paths relatively. The unified release puts the matching DLLs and module manifests in separate Prebuilt profiles beside the common source. It includes configuration, English documentation, scripts, and the MIT license. Logs, PDBs, caches, HostProject, and Intermediate are excluded. Build scripts do not download engines or modify their source.

## Feedback and contributions

For bug reports, include your Unreal Engine version, Windows version, reproduction steps, and the expected and actual behavior. For feature requests, describe the workflow you want to improve.

When submitting a code change, explain its purpose and include validation results from the source build command with `-RunTests`.

## License

Original plugin code and scripts use the included [MIT License](LICENSE). Unreal Engine is a separate dependency under Epic's license.
