# CUDA backend

## 目的

ReactionDiffusionBifrostの本番向け高速実行経路はCUDAです。`Backend::Auto`は、CUDAを
含むbuildで実行可能なGPUが見つかった場合にCUDAを優先します。CPU実装は数値参照、
自動回帰テスト、CUDA非搭載環境、明示的fallbackのために残します。

## Preview 3で実装済み

- CMakeとBifrost Pack buildでCUDA Toolkit / `nvcc`を自動検出
- CPU-only buildを維持しながら`RD_HAS_CUDA`をCUDA buildだけへ定義
- Web版／CPU版と同じ9点LaplacianとGray–Scott式の2D CUDA kernel
- Device上のA/Bを入れ替えるping-pong substep
- Periodic / NoFlux / FixedInitial境界
- CUDA優先Auto dispatchと、buildなし／deviceなしを区別するstatus
- CUDA build時だけ有効になるCPU/CUDA数値比較テスト
- Turing 7.5、Ampere 8.6、Ada 8.9向けコード生成設定
- CUDA Runtime静的リンクとMayaのDLL MSVC Runtimeとの競合回避
- Maya/Bifrost境界での例外封じ込め（不正入力は`ERROR` statusへ変換）

## 開発機監査（2026-08-24）

- GPU: NVIDIA GeForce RTX 4070 Ti SUPER、16GB、Compute Capability 8.9
- Driver: 560.94（`nvidia-smi`表示のCUDA互換上限は12.6）
- Host compiler: Visual Studio 2022 17.14 / MSVC 19.44
- CUDA Toolkit: 12.6 Update 3（Driverを除外した最小開発構成）
- `nvcc`: 12.6.85

既存Driverは変更せず、`nvcc_12.6`、`cudart_12.6`、
`visual_studio_integration_12.6`だけを導入しました。NVIDIAのCUDA 12.x minor-version
compatibility範囲内であり、MSVC 19.44による実コンパイルも成功しています。

## 2026-08-24 実機検証結果

- CUDA compiler / device検出: PASS
- C++回帰テスト: PASS
- CPU/CUDA最大誤差（64×48、80 substep）: `1.72853e-6`
- `Backend::Auto`: `CUDA` / `auto_selected_cuda`
- Maya 2026 / Bifrost 2.15 native operator評価: PASS
- Packed Feedback Stateのフレーム1→2→3増分CUDA評価と開始フレームReset: PASS
- 不正`time_step=0`: Mayaを終了させず`ERROR` / `error: ...`へ変換
- Pack DLL: `cudart64_12.dll`への動的依存なし

1024×1024、15 substep、7回の中央値（RTX 4070 Ti SUPER）:

| Backend | 15 substep | 1 substep | cells/sec |
| --- | ---: | ---: | ---: |
| OpenMP CPU | 74.543 ms | 4.970 ms | 2.110e8 |
| CUDA | 3.654 ms | 0.244 ms | 4.304e9 |

この条件でCUDAはCPU比`20.398x`です。現在のCUDA値はOperator呼び出しごとの
Device確保とHost/Device転送を含むため、GPU常駐化前の保守的な測定です。

## Toolkit導入後の検証ゲート

```powershell
cmake -S . -B build-cuda -G "Visual Studio 17 2022" -A x64 -T cuda=12.6 `
  -DRD_ENABLE_CUDA=ON `
  -DRD_REQUIRE_CUDA=ON `
  -DRD_CUDA_ARCHITECTURES=89
cmake --build build-cuda --config Release
ctest --test-dir build-cuda -C Release --output-on-failure
```

続いてBifrost Packをbuildし、`tests/test_maya_bifrost_cuda.py`をMaya batchで実行します。
このテストは`backend_used == "CUDA"`、status、実評価、不正入力の例外封じ込めを確認します。

## 次の性能段階

Preview 5ではBifrost Feedback Stateにより初期フレームからの全履歴再計算を解消しました。
ただしCUDA kernelはOperator呼び出しごとにDeviceメモリを確保し、A/BをHostとDevice間で
転送します。長いsubstepでは高速化を見込めますが、最終形ではありません。次段階で
GPU常駐A/B bufferを導入してフレーム間の再確保・再転送をなくします。
3D Volume CUDAはその後、同じbackend契約へ追加します。
