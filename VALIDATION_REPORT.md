# ReactionDiffusionBifrost 0.2.0 Preview 8 Validation

## Core completion checks (2026-09-17)

- CPU-only and CUDA builds: CTest 2/2 each. Existing 2D CPU behavior preserved.
- 2D / Volume (3 boundary modes, plus 24³ x 64 steps) / Surface (planar and
  curved 99-vertex mesh): CPU/CUDA maximum errors 0 with FMA fusion disabled.
  Before this change, the identical 24³ playback fixture exceeded the existing
  2e-5 tolerance (3.06368e-5). The tolerance was not relaxed.
- Maya 2026 / Bifrost 2.15 native pack builds and all operator exports verified.
- Installed package under Documents/maya/modules verified by DLL hash and
  separate interactive Maya processes using an isolated preference directory.
- Real Play, parallel evaluation, every frame 0→8→0:
  Grid CPU error 0; deforming Surface 2.91038e-11; Volume 0. Full-array reset
  error 0 in all three domains. Unlike earlier tests, these run `cmds.play`
  and compare every frame with CPU results, including the production preview.
- Playback root cause: repeated display flag / color-set updates and explicit
  dirtying of the display mesh invalidated evaluation during playback. Display
  settings are now changed only when necessary, and the mesh is not explicitly
  dirtied after API color writes. Solver array outputs are still explicitly
  dirtied. No global evaluation-mode workaround is required.
- Surface/Volume A/B, gradients, state continuation and reset: PASS.
- Timed seed replay, native presets, image dimensions, EXR writing through
  standard write_texture, points→volume→nonempty Maya mesh: PASS.
- Existing Maya color-set / 2D CUDA / feedback / callback checks: PASS.
- Python helpers and timeline error/recovery checks: PASS.
- Headless Bifrost initialization logs include menuSet and missing Cg fragment
  warnings; integration assertions pass. MayaUSD loading PySide6/Shiboken emits
  a NumPy 1.x/2.3.3 compatibility warning even with isolated preferences.
  This environment warning remains unresolved; no NumPy dependency was added.
  Earlier runs also emitted userSetup/Fabricator messages. Full logs remain in build/.

Reproduction: `scripts/test_maya_interactive.ps1 -Domain grid|surface|volume`,
`tests/test_maya_spatial.py`, `tests/test_maya_core_features.py`, and
`tests/run_maya_test.py <legacy-test-name.py>`. Set RD_TEST_PACK and
RD_TEST_PACKAGE_ROOT to test a specific installed pack/package.

Not claimed: GPU-resident state, Sparse GPU storage, arbitrary topology changes,
automatic checkpoint/restart UI, cross-hardware bitwise identity, performance
benchmarks for every new resolution, or arbitrary-mesh one-click UV baking.

The following sections are historical Preview 7 baseline records.

## Timeline diagnostics follow-up (2026-09-17)

- Added visible timeline errors with repeated-warning suppression and recovery.
- Identical failure/recovery regression: fails before the change, passes after.
- The Maya callback test now runs the production UI refresh function; only UI
  input values are substituted because standalone Maya has no interactive UI.
- Frames 0→1→2→3→0: PASS; Pattern differences remain `0.4157934` and
  `0.0891801`; reset compares the entire initial array, maximum error `0.0`.
- Interactive playback remains unverified. `currentTime` callback delivery is
  not evidence that the Play button works; previous playback claims exceeded
  the tested conditions. CI checks do not include interactive Maya playback.
- Maya startup emitted existing userSetup/menu initialization errors; the
  integration assertions completed successfully. Full local log:
  `build/timeline-production.log` (ignored).

## Automated checks in this distribution

- C++ Gray–Scott core unit tests
- Dense 3D volume stationarity, periodic seed, bounded concentration and output-size tests
- CPU-only CUDA fallback contract and explicit Volume CUDA fallback tests
- Conditional CPU/CUDA parity test when a compiled CUDA backend and device are available
- Release core benchmark
- Python syntax compilation for all Maya modules
- Preview pattern flattening, size validation and color-ramp tests with Maya stubs
- Bifrost array-literal and graph-node discovery tests with Maya stubs
- Scroll layout, resizable window and Refresh button ordering regression checks
- Painter release callback regression test by static inspection
- Product / module / pack version consistency scan
- Source archive integrity check
- Packed State round-trip and split-step equivalence test
- Maya Feedback State frame progression and start-frame reset test
- Maya DG playback-time callback delivery and non-recursive refresh test

## Previously validated on the Maya 2026 target machine

The 0.1.0 native operator baseline used by this version produced:

```text
backend_used: CPU
status: auto_selected_cpu
elapsed_milliseconds: 2.7653000354766846
pattern size: 4096
```

The Seed Painter also created drag curves successfully after the MPoint conversion fix.

## Development baseline validation (2026-08-24)

- Visual Studio 2022 / Windows SDK native core build: PASS
- CTest (2D and dense 3D reference solver): PASS
- Maya-independent Python regression suite: PASS
- Bifrost 2.15.0.0 SDK operator generation and Release build: PASS
- DLL export inspection for grid, Feedback State and volume operators: PASS
- Installed Maya module / Python / graph builder / pack config / DLL / operator JSON checks: PASS
- Generated operator JSON contains all grid, Feedback State and volume nodes: PASS
- Maya 2026 Color Set creation regression test: PASS
- Maya 2026 `mayapy` real-mesh Color Set integration test: PASS
- CUDA Toolkit 12.6 / RTX 4070 Ti SUPER Bifrost Pack build: PASS
- CPU/CUDA maximum error（64×48、80 substep）: `1.72853e-6`
- Maya 2026 CUDA Auto dispatch: `CUDA` / `auto_selected_cuda`
- Visible sample pattern and vertex-color integration: PASS
- Packed Feedback State frame 1→2→3 progression: PASS
- Start-frame Feedback reset maximum error: `0.0`
- Maya 2026 DG time callback delivery at frames 2, 3 and 4: PASS
- Playback callback requests no recursive forced viewport refresh: PASS
- Product-path callback evaluation at frames 0, 1, 2 and 3: PASS
- Product-path Pattern differences 0→1 / 1→2: `0.4157934` / `0.0891801`
- Product-path return-to-zero initial-seed reset: PASS

Maya UIでのVolume Operator検索、グラフ評価、Volume可視化は未確認です。Mayaを
再起動した後に、下記チェックを実行する必要があります。

CUDA Toolkit / `nvcc`は12.6 Update 3を最小構成で導入済みです。2D CUDA kernelの
コンパイル、RTX 4070 Ti SUPER上のCPU/CUDA一致、benchmark、Maya評価を完了しています。

## Maya checks for Preview 7

The following require the user's Maya 2026/Bifrost 2.15 runtime and cannot be executed in
the packaging environment:

1. Install 0.2.0 and restart Maya.
2. Confirm the grid and Feedback State custom operators are searchable.
3. Open the previously validated 64 x 64 graph.
4. Launch `reaction_diffusion_bifrost.show()`.
5. Confirm the controller can be resized and vertically scrolled.
6. Click `Refresh Existing Output`; confirm `RD_SimulationPreview` has 4096 vertices.
7. Paint a seed and click `Sync Painted Seeds + Preview`.
8. Confirm `Reset (0 steps)` shows the seed and `Step + Preview` grows the pattern.
9. Create a Stateful Playback Graph and play consecutive frames.
10. Confirm returning to the playback start frame resets the pattern.

The development baseline additionally requires checking that
`reaction_diffusion_initialize_volume` and `reaction_diffusion_volume_step` are searchable,
produce `width * height * depth` values, and expose all three gradient arrays. These new
operators were not yet validated at the Preview 7 baseline; see Preview 8 above.

## Performance interpretation

Vertex-color preview creation and Python-to-VNN seed synchronization are UI operations and are
not included in `elapsed_milliseconds`. That output remains the native Solver time. Preview
refresh cost should be measured separately before increasing the default resolution.
