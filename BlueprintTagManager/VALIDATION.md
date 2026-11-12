# Blueprint Tag Manager 1.0.4 - Validation Record

- Validation date: 2026-11-12 (Korea Standard Time).
- Platform: Windows 11 x64, Epic Games Launcher engine installations.
- Source target: Unreal Engine **5.0 through 5.8**.
- Executed verification is limited to the three installed versions below. Other versions were not run and are not claimed as verified.

## Engine matrix

| Engine | MSVC | Windows SDK | Source build | Automation |
| --- | --- | --- | --- | --- |
| 5.3.2 | 14.38.33145 | 10.0.26100.0 | Passed | 8 passed, 0 warnings, 0 failed, 0 not run |
| 5.5.4 | 14.44.35220 | 10.0.22621.0 | Passed | 8 passed, 0 warnings, 0 failed, 0 not run |
| 5.7.4 | 14.44.35220 | 10.0.22621.0 | Passed | 8 passed, 0 warnings, 0 failed, 0 not run |
| 5.0, 5.1, 5.2, 5.4, 5.6, 5.8 | Not tested | Not tested | Not executed | Not executed |

The three source builds ran separately, each in a fresh HostProject, followed by the eight-test suite with NullRHI / RenderOffscreen. UE 5.3 was rebuilt and retested after adding MSVC 14.38 alongside the existing 14.44 installation. Its build explicitly selected toolset directory 14.38.33130, and the build log confirmed compiler 14.38.33145. The unified ZIP uses this new 5.3 DLL. The 5.5 and 5.7 profiles retain their verified 14.44 builds.

The plugin module disables PCH and Unity in its module rules; the engine's precompiled target layout is preserved. UE 5.5 emits compiler-version notices with MSVC 14.44. The zero-warning counts above refer to plugin automation results, not a claim that the engine build log has no notices. No additional Unreal Engine versions were installed or executed.

## Compatibility changes

- The source descriptor omits the former 5.7 EngineVersion pin. Built binary descriptors use the selected engine's minor-version baseline; the unified archive separates binaries into named engine profiles.
- UE 5.0 uses the EditorStyle API and legacy Asset Registry class names / object paths. Later versions use AppStyle and class paths. Text ellipsis is limited to the later-version branch.
- A guarded feature-test fallback, following the [Clang language-extension guidance](https://clang.llvm.org/docs/LanguageExtensions.html#has-feature-and-has-extension), handles older UE headers compiled with MSVC when the Clang macro is absent. No engine files were changed. MSVC 14.38 was added as a separate toolset; 14.44 was retained, and no global compiler preference was changed.
- Build scripts support both windows and win-x64 bundled .NET layouts, read the actual Build.version, and select the appropriate UBA flags. An installed MSVC version can be selected explicitly.
- The engine-matrix script produces separate builds and test reports. Its installed-engine discovery and missing-engine preflight rejection were checked. Missing engines are recorded as NotInstalled and cannot be reported as passing.
- The unified ZIP has one common source folder and three prebuilt descriptor/DLL/manifest profiles. Each profile keeps its real Build ID; manifests are not rewritten to force cross-version loading. See [Epic's versioning documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/versioning-of-assets-and-packages-in-unreal-engine#buildid).

## Automated coverage

| Test | Coverage |
| --- | --- |
| TagOperations | Add, rename/collision merging, removal of every matching occurrence, clear, input validation, and ignored inputs for add/clear |
| BlueprintInheritance | Child overrides, parent protection, Undo/Redo, and persistence after compilation |
| NativeComponents | Native C++ component editing, protection of original C++ defaults, and persistence after compilation |
| EditorIntegration | Manual scan, main-editor tab, default StaticMesh filter, filtered summaries, expand/collapse, collapsed-group selection, separate search fields, and tag-presence filters |
| ProjectBatch | Parent/child/unrelated asset batches, stale-preview rejection without partial tag writes, one-step Undo/Redo, and persistence after compilation |
| ProjectDiscovery | Saved/unloaded assets, folder boundaries, reading defaults without opening Blueprint editors, and non-Actor asset handling |
| ViewFiltering | Search combinations, selection scope, tag-presence cases, all-tagged status, and summaries of shown component tags only |
| StaticMeshFilter | Actual-class detection, independent ISM/HISM exclusions, all 24 type/exclusion/presence combinations, exact summaries, group-selection and batch scope, and inherited/native components |

Full test names use the prefix BlueprintTagManager. followed by the test name above.

## Clean installation and rendered UI

The unified ZIP installer selected the matching prebuilt profile for each of the three installed engines and installed it into a new, empty project. The plugin loaded without recompilation. All eight tests passed again in the rendered installation runs:

| Engine | Rendering mode | Installation automation |
| --- | --- | --- |
| 5.3.2 | Direct3D 11 / RenderOffscreen | 8 passed, 0 warnings, 0 failed, 0 not run |
| 5.5.4 | Direct3D 12 / RenderOffscreen | 8 passed, 0 warnings, 0 failed, 0 not run |
| 5.7.4 | Direct3D 11 / RenderOffscreen | 8 passed, 0 warnings, 0 failed, 0 not run |

The initial 5.5 offscreen Direct3D 11 attempt exited in D3D11RHI during renderer startup before automation began. The successful rendered verification used Direct3D 12; this record does not claim a passing 5.5 offscreen Direct3D 11 run.

Idle, enabled-exclusion, and expanded-result screenshots were captured with -BTMCaptureUI. Captures were reviewed across the tested versions for readable controls, red exclusion toggles, Components (shown), separate action input groups, and bold-only Actor Tags. Screenshots and logs are not included in the release ZIP. The final release DLL for each engine matches the DLL used in its clean-installation run.

## Unified installer checks

- Matching Build IDs selected the correct UE 5.3, 5.5, and 5.7 profiles. Project files were left unchanged.
- Source installation was exercised with the actual UE 5.3 engine and MSVC 14.38. The completed plugin was installed into a separate empty project, and its binaries match the new source build.
- A deliberately mismatched Build ID fixture selected source fallback. Missing build tools then produced an error before changing the project; no engine process was executed for this fixture.
- Installation into an existing plugin folder was refused without -Replace. With -Replace, the previous folder and a validation marker were preserved under Saved/BlueprintTagManager/Backups, outside plugin discovery; the new install did not contain the old marker.
- The packager checks that each prebuilt profile's Source and Config files match the common source, rejects duplicate engine profiles, and retains the original module manifests.
- Release-scanner rejection was checked for an unexpected DLL, PDB, an absolute Windows path embedded in an otherwise allowlisted DLL, and a traversal entry.

The installer supports source fallback across the declared 5.0-5.8 range, but the six uninstalled engines were not run. These packaging checks do not turn those source targets into verified builds.

## Release checks

The plugin declares version **1.0.4** with internal version **5**. English README.md and VALIDATION.md files and the MIT license are included.

Public release checks inspect Git-visible files and archive entries for private profile paths and known token patterns. DLL checks also reject embedded absolute Windows paths. Compiler diagnostic paths are recorded relatively. Logs, PDBs, HostProjects, caches, Intermediate, and private Git configuration are excluded from releases. Pattern checks are not an exhaustive secret detector.

The unified Windows archive passed these checks and includes a SHA-256 sidecar. Source files match the code used in all three engine builds. The common BlueprintTagManager source folder contains no Binaries or Intermediate directory; only the three narrowly allowlisted prebuilt profiles contain DLLs and module manifests. No nested release ZIPs, engine SDK, compiler installer, or compiler binaries are included.
