# ReactionDiffusionBifrost 0.2.0 Preview 3 Validation

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
- DLL export inspection for grid and volume initialize/step operators: PASS
- Installed Maya module / Python / pack config / DLL / operator JSON checks: 7/7 PASS
- Generated operator JSON contains all four grid and volume nodes: PASS
- Maya 2026 Color Set creation regression test: PASS
- Maya 2026 `mayapy` real-mesh Color Set integration test: PASS
- CUDA-aware CMake and Bifrost Pack build without Toolkit (CPU fallback): PASS

Maya UIでのVolume Operator検索、グラフ評価、Volume可視化は未確認です。Mayaを
再起動した後に、下記チェックを実行する必要があります。

CUDA Toolkit / `nvcc`は開発機に未導入です。2D CUDA kernelのコンパイル、RTX 4070 Ti
SUPER上のCPU/CUDA一致テスト、benchmarkはToolkit導入後の必須検証です。

## Maya checks required for Preview 2

The following require the user's Maya 2026/Bifrost 2.15 runtime and cannot be executed in
the packaging environment:

1. Install 0.2.0 and restart Maya.
2. Confirm the two custom operators are searchable.
3. Open the previously validated 64 x 64 graph.
4. Launch `reaction_diffusion_bifrost.show()`.
5. Confirm the controller can be resized and vertically scrolled.
6. Click `Refresh Existing Output`; confirm `RD_SimulationPreview` has 4096 vertices.
7. Paint a seed and click `Sync Painted Seeds + Preview`.
8. Confirm `Reset (0 steps)` shows the seed and `Step + Preview` grows the pattern.
9. Report any complete Script Editor traceback.

The development baseline additionally requires checking that
`reaction_diffusion_initialize_volume` and `reaction_diffusion_volume_step` are searchable,
produce `width * height * depth` values, and expose all three gradient arrays. These new
operators have not yet been validated in Maya.

## Performance interpretation

Vertex-color preview creation and Python-to-VNN seed synchronization are UI operations and are
not included in `elapsed_milliseconds`. That output remains the native Solver time. Preview
refresh cost should be measured separately before increasing the default resolution.
