# Blueprint Tag Manager 1.0.5 - Validation Record

- Validation date: 2026-11-12 (Korea Standard Time).
- Platform: Windows 11 x64, Epic Games Launcher engine installations.
- Source target: Unreal Engine **5.0 through 5.8**.
- Executed verification is limited to **5.3.2, 5.5.4, and 5.7.4**. No additional engines were installed or executed. Other versions are unverified source targets.

## Source builds and automation

| Engine | MSVC | Windows SDK | Source build | Automation |
| --- | --- | --- | --- | --- |
| 5.3.2 | 14.38.33145 | 10.0.26100.0 | Passed | 9 passed, 0 warnings, 0 failed, 0 not run |
| 5.5.4 | 14.44.35220 | 10.0.22621.0 | Passed | 9 passed, 0 warnings, 0 failed, 0 not run |
| 5.7.4 | 14.44.35220 | 10.0.22621.0 | Passed | 9 passed, 0 warnings, 0 failed, 0 not run |
| 5.0, 5.1, 5.2, 5.4, 5.6, 5.8 | Not tested | Not tested | Not executed | Not executed |

Each engine compiled the same Source and Config files in a fresh HostProject and ran the nine-test suite with NullRHI / RenderOffscreen. UE 5.3 explicitly selected toolset directory 14.38.33130, whose compiler reports 14.38.33145. UE 5.5 and 5.7 selected 14.44.35207, whose compiler reports 14.44.35220. Both toolsets remain installed; no global compiler preference was changed.

The plugin module disables PCH and Unity while preserving the engine's precompiled target layout. UE 5.5 emits compiler-version notices with MSVC 14.44. Zero warnings above refers to automation results, not a claim that engine logs contain no notices. Engine source files were not modified.

## Thumbnail feature

- Blueprint and Static Mesh component thumbnails have independent switches. Both default off, allowing all four visibility combinations.
- The shared size defaults to 32x32, with 16x16 and 64x64 alternatives. All 12 visibility/size combinations were exercised.
- Blueprint groups resolve to their Blueprint asset. Component rows resolve to the assigned Static Mesh on the effective template, including ISM, HISM, native defaults, and child Blueprint overrides.
- Unassigned StaticMesh, ISM, and HISM components and non-StaticMesh components resolve to no asset and display No image when component thumbnails are enabled. Actor Tags remain text-only.
- Display changes preserve checked targets, matching scope, expansion, and tag summaries. They rebuild visible row widgets without rescanning or invalidating an edit preview.
- Resolving thumbnails does not open Blueprint editors. Actor tags and the parent's mesh assignment remain unchanged when reading a child's override.
- The shared pool is created lazily, is limited to 128 thumbnail slots, and services visible virtualized rows. Hover-based real-time rendering is disabled. Display preferences last for the current panel session.

## Automated coverage

| Test | Coverage |
| --- | --- |
| TagOperations | Add, rename/collision merging, removal of every occurrence, clear, input validation, and ignored add/clear inputs |
| BlueprintInheritance | Child overrides, parent protection, Undo/Redo, and persistence after compilation |
| NativeComponents | Native component editing, protection of original C++ defaults, and persistence after compilation |
| EditorIntegration | Manual scan, main-editor tab, default StaticMesh filter, filtered summaries, expand/collapse, collapsed selection, separate searches, and tag-presence filters |
| ProjectBatch | Multi-asset batches, stale-preview rejection without partial writes, one-step Undo/Redo, and persistence after compilation |
| ProjectDiscovery | Saved/unloaded assets, folder boundaries, reading defaults without Blueprint editors, and non-Actor asset handling |
| ViewFiltering | Search combinations, selection scope, tag-presence cases, all-tagged status, and summaries of shown component tags only |
| StaticMeshFilter | Class detection, independent ISM/HISM exclusions, 24 type/exclusion/presence combinations, summaries, batch scope, and inherited/native components |
| ThumbnailDisplay | Default-off switches, 12 display combinations, exact asset resolution, missing assets, child overrides, native meshes, and preservation of selection/scope/expansion/tags |

Full names start with BlueprintTagManager. followed by the test name above. Build scripts require all nine tests to pass with no warnings or skipped tests.

## Unified ZIP installation and rendered UI

The unified ZIP was extracted, and its installer selected the matching prebuilt profile for each installed engine in a new, empty project. The plugin loaded without recompilation. All nine tests passed again with actual rendering:

| Engine | Rendering mode | Installation automation |
| --- | --- | --- |
| 5.3.2 | Direct3D 11 / RenderOffscreen | 9 passed, 0 warnings, 0 failed, 0 not run |
| 5.5.4 | Direct3D 12 / RenderOffscreen | 9 passed, 0 warnings, 0 failed, 0 not run |
| 5.7.4 | Direct3D 11 / RenderOffscreen | 9 passed, 0 warnings, 0 failed, 0 not run |

Rendered runs used -BTMCaptureUI. In addition to idle, exclusion, and expanded-list captures, ThumbnailDisplay captured both switches off, Blueprint-only, component-only, and both-on at 16, 32, and 64 pixels. Latent capture commands yield normal editor frames so the engine pool can render actual Blueprint and mesh previews. The fixture includes a cube, sphere, cone, non-mesh component, and unassigned StaticMesh/ISM/HISM components.

Captures were reviewed for correct assets, independent visibility, sizing, readable No image labels, and preserved checks/expansion. Captures and logs are excluded from the release. Final profile DLLs match those used in these installation runs.

## Compatibility and installer

- Common source has no EngineVersion pin. UE 5.0 uses EditorStyle and legacy Asset Registry names/paths; later versions use AppStyle and class paths. The guarded MSVC feature-test fallback handles older engine headers without changing engine files.
- Build scripts read Build.version, locate bundled .NET, and choose the corresponding UBA flags. The matrix records only executed engines as built; missing engines cannot be reported as verified.
- The single Windows ZIP contains common source plus UE 5.3, 5.5, and 5.7 descriptor/DLL/manifest profiles. Source and Config hashes must match across profiles before packaging.
- Prebuilt binaries are selected only when the engine Build ID matches. Other target versions and different/custom Build IDs require a compatible local compiler and SDK for source fallback. They remain unverified if not executed.
- Project files are not edited. Existing plugins require -Replace and are backed up under Saved/BlueprintTagManager/Backups, outside plugin discovery. Original module Build IDs are retained.

## Release checks

VersionName is **1.0.5**, with internal Version **6**. English README and validation documents, scripts, common source/configuration, and the MIT license are included.

Public release checks inspect Git-visible files and ZIP entries for private profile paths and known token patterns. DLL checks also reject embedded absolute Windows paths; compiler diagnostic paths are trimmed to relative paths. Logs, PDBs, HostProjects, build intermediates, caches, Git metadata, and nested ZIPs are excluded. Pattern checks are not an exhaustive secret detector.

The unified archive passed public release checks. Every entry was compared byte-for-byte against common repository files or its verified prebuilt profile, and the SHA-256 sidecar was checked. The common source folder has no Binaries or Intermediate directory; only the three allowlisted profiles contain DLLs and module manifests. Unreal Engine SDKs and compiler installers are not distributed.
