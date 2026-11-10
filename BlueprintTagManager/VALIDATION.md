# Blueprint Tag Manager 1.0.2 - Validation Record

- Validation date: 2026-11-10 (Korea Standard Time).
- Environment: Windows 11, Epic Games Launcher Unreal Engine 5.7.4, MSVC 14.44, Windows SDK 10.0.22621.0.
- Build: source and configuration copied into a separate HostProject; compiled and linked with PCH, shared PCH, and Unity Build disabled.
- Engine automation: **8 passed, 0 passed with warnings, 0 failed, 0 not run**.

## Automated coverage

| Test | Coverage | Result |
| --- | --- | --- |
| TagOperations | Add without duplicating an existing tag, rename and collision merging, removal of every matching occurrence, clear, input validation, add ignoring Existing tag, and clear ignoring both tag inputs | Passed |
| BlueprintInheritance | Child overrides, parent protection, Undo/Redo, and persistence after compilation | Passed |
| NativeComponents | Native C++ component editing, protection of original C++ defaults, and persistence after compilation | Passed |
| EditorIntegration | No scan or targets on panel open and idle ticks, explicit Scan project action, asset discovery, default StaticMesh filter, expand/collapse without opening Blueprint editors, collapsed-group batch scope, separate search fields, and tag-presence filters | Passed |
| ProjectBatch | Simultaneous parent/child/unrelated asset changes, complete preflight validation, rejection of a stale batch without partial tag writes, one-step Undo/Redo, and persistence after compilation | Passed |
| ProjectDiscovery | Save and unload assets, discover within folder boundaries, read tags from disk assets, and distinguish non-Actor Blueprints | Passed |
| ViewFiltering | Separate search fields and AND matching, group selection scope, tagless Blueprint exclusion under With tags, actor-only/component-only tag cases, all-Blueprint/actor retention under Without tags, untagged component filtering, all-tagged status, actors without components, and restoration under All | Passed |
| StaticMeshFilter | Actual-class detection for StaticMesh/ISM/HISM, independent ISM and HISM classification, default exclusion states, all 24 combinations of StaticMesh-only state / HISM exclusion / ISM exclusion / tag presence, excluded-target omission from group selection and batch previews, unchanged whole-Blueprint summaries, combined component/tag searches, inherited exclusions with StaticMesh only on or off, and native StaticMesh detection | Passed |

Full test names use the prefix `BlueprintTagManager.` followed by the test name above. The source-build suite runs with NullRHI and RenderOffscreen. Other engine patches, custom engine builds, operating systems, and user projects require separate verification.

## Rendered UI review

The same eight tests passed in a fresh project using Direct3D 11 / RenderOffscreen with `-BTMCaptureUI`: **8 passed, 0 passed with warnings, 0 failed, 0 not run**.

Captures of the idle panel and expanded Blueprint results were reviewed for the initial Not scanned state, the independent HISM / ISM toggles below StaticMesh only, the separate Checked targets and Match an existing tag groups, independent add and replacement inputs, the absence of Preview deduplicate, and content-sized StaticMesh only / Apply preview controls. Actor Tags retain bold-only emphasis. The layout continues to use Slate splitters and resizable table columns. Screenshot artifacts remain outside the release archive.

## Release checks

Public release checks scan source and archive contents for private profile paths and known token patterns. DLL checks also reject embedded absolute Windows paths. Diagnostic source paths are recorded relatively. Logs, PDBs, HostProject, Intermediate, and private Git configuration are excluded. The MIT license for original code and scripts is included.

Pattern checks detect common private paths and known token formats; they are not an exhaustive secret detector.

## Clean installation check

The archive includes English `README.md` and `VALIDATION.md` files and declares release version **1.0.2** with internal version **3**.

The release ZIP was installed into a new, empty project. The plugin loaded without recompilation, and all eight automation tests passed in the rendered run described above. The final release DLL matches the DLL used for this installation check. The extracted download passed the source-only public release check, and the repository/archive release scan checked 46 files without finding matching private-path or known-token patterns.
