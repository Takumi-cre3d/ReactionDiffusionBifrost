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

## 開発機監査（2026-08-24）

- GPU: NVIDIA GeForce RTX 4070 Ti SUPER、16GB、Compute Capability 8.9
- Driver: 560.94（`nvidia-smi`表示のCUDA互換上限は12.6）
- Host compiler: Visual Studio 2022 17.14 / MSVC 19.44
- CUDA Toolkit / `nvcc`: 未導入

このため、現在のCUDA sourceはCPU-only構成を壊さないことまで検証済みですが、CUDA
コンパイルと実機実行は未検証です。ToolkitとHost compiler、Driverの互換組み合わせを
確定してから導入します。

## Toolkit導入後の検証ゲート

```powershell
cmake -S . -B build-cuda `
  -DRD_ENABLE_CUDA=ON `
  -DRD_REQUIRE_CUDA=ON `
  -DRD_CUDA_ARCHITECTURES=89
cmake --build build-cuda --config Release
ctest --test-dir build-cuda -C Release --output-on-failure
```

続いてBifrost Packをbuildし、`backend_used == "CUDA"`、status、CPUとの最大誤差、
1024²以上のbenchmarkを確認します。

## 次の性能段階

Preview 3 kernelはOperator呼び出しごとにDeviceメモリを確保し、A/BをHostとDevice間で
転送します。長いsubstepでは高速化を見込めますが、最終形ではありません。次段階で
Bifrost Simulation StateとGPU常駐A/B bufferを組み合わせ、フレーム間の再確保・再転送・
初期状態からの全再計算をなくします。3D Volume CUDAはその後、同じbackend契約へ追加します。
