# Blueprint Tag Manager 1.0.1 - Validation Record

- Validation date: 2026-11-10 (Korea Standard Time).
- Environment: Windows 11, Epic Games Launcher Unreal Engine 5.7.4, MSVC 14.44, Windows SDK 10.0.22621.0.
- Build: source and configuration copied into a separate HostProject; compiled and linked with PCH, shared PCH, and Unity Build disabled.
- Engine automation: **8 passed, 0 passed with warnings, 0 failed, 0 not run**.

## Automated coverage

| Test | Coverage | Result |
| --- | --- | --- |
| TagOperations | Add, rename, collision merging, remove, clear, deduplication, and input validation | Passed |
| BlueprintInheritance | Child overrides, parent protection, Undo/Redo, and persistence after compilation | Passed |
| NativeComponents | Native C++ component editing, protection of original C++ defaults, and persistence after compilation | Passed |
| EditorIntegration | No scan or targets on panel open and idle ticks, explicit Scan project action, asset discovery, default StaticMesh filter, expand/collapse without opening Blueprint editors, collapsed-group batch scope, separate search fields, and tag-presence filters | Passed |
| ProjectBatch | Simultaneous parent/child/unrelated asset changes, complete preflight validation, rejection of a stale batch without partial tag writes, one-step Undo/Redo, and persistence after compilation | Passed |
| ProjectDiscovery | Save and unload assets, discover within folder boundaries, read tags from disk assets, and distinguish non-Actor Blueprints | Passed |
| ViewFiltering | Separate search fields and AND matching, group selection scope, tagless Blueprint exclusion under With tags, actor-only/component-only tag cases, all-Blueprint/actor retention under Without tags, untagged component filtering, all-tagged status, actors without components, and restoration under All | Passed |
| StaticMeshFilter | Actual-class detection for StaticMesh/ISM/HISM, Box/SkeletalMesh exclusion, components without an assigned mesh asset, inherited/native component detection, combined tag/type filters, hidden-target exclusion from group selection, and restoration when the type filter is disabled | Passed |

Full test names use the prefix `BlueprintTagManager.` followed by the test name above. The eight-test suite runs with NullRHI and RenderOffscreen. Other engine patches, custom engine builds, operating systems, and user projects require separate verification.

## Rendered UI review

EditorIntegration also passed in a separate Direct3D 11 / RenderOffscreen run with `-BTMCaptureUI`: **1 passed, 0 passed with warnings, 0 failed**. Captures of the idle panel and expanded Blueprint results were reviewed for section separation, readable controls, the initial Not scanned state, and bold-only Actor Tags without a custom background or text color. The layout uses Slate splitters for resizable sections and table-header dividers for resizable columns. Screenshot artifacts remain outside the release archive.

## Release checks

Public release checks scan source and archive contents for private profile paths and known token patterns. DLL checks also reject embedded absolute Windows paths. Diagnostic source paths are recorded relatively. Logs, PDBs, HostProject, Intermediate, and private Git configuration are excluded. The MIT license for original code and scripts is included.

Pattern checks detect common private paths and known token formats; they are not an exhaustive secret detector.

## Clean installation check

The archive includes English `README.md` and `VALIDATION.md` files and declares release version **1.0.1** with internal version **2**.

The release ZIP was installed into a new, empty project. The plugin loaded without recompilation, and the same eight automation tests completed with **8 passed, 0 passed with warnings, 0 failed, 0 not run**. The final release DLL matches the DLL used for this installation check. The repository and archive release scan checked 46 files and found no matching private-path or known-token patterns.
