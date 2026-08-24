# ReactionDiffusionBifrost

Maya 2026 / Bifrost 2.15向けの、Gray–Scott反応拡散シミュレーション専用ノードです。

目標は、[TuringPattern Generator](https://github.com/Takumi-cre3d/TuringPattern_Generator)のインタラクティブな2Dシミュレーションを出発点に、Bifrostグラフ内で再利用できる汎用ノードへ発展させることです。最終的にはUVグリッドだけでなく、入力メッシュ表面と3Dボリューム上で反応拡散を計算し、`float`フィールド、勾配、メッシュ、ポイント、ボリューム、テクスチャなど任意の後続処理へ接続できる構成を目指します。

## 現在地

`0.2.0-preview2`を引き継ぎ元の正本としてリポジトリ化し、現在は
`0.2.0-preview3`を開発しています。最終的な高速実行経路はCUDAを第一候補とし、
CPU実装は数値参照・fallback・CUDAを持たない環境向けとして維持します。

- C++17のGray–Scott CPUソルバー（OpenMP対応）
- Bifrost Native Operator: 2Dグリッド初期化／ステップ
- 実験的な3D dense volume CPUソルバー／Native Operator
- CUDA Toolkit自動検出、CUDA優先Auto dispatch、実機検証済み2D ping-pong kernel
- Periodic / NoFlux / FixedInitial境界
- UV Seed PainterとBifrost配列の同期
- Maya頂点カラーによる非破壊プレビュー
- 決定論的なReset / Step操作
- C++単体テストとMaya非依存Python回帰テスト

2D / UVグリッドのCPU経路が検証済みの基準実装です。2D CUDA kernelはCUDA 12.6、
RTX 4070 Ti SUPERでコンパイル、CPU数値比較、Maya/Bifrost実行まで検証済みです。
1024²・15 substepの開発機測定ではCPU 74.543 msに対してCUDA 3.654 ms（20.398倍）、
最大数値誤差は`1.73e-6`でした。
3D Volumeは密配列CPU参照実装までで、Surface Solver、GPU常駐Simulation State、
Volume CUDA、Sparse化もロードマップ上の開発項目です。詳細は[CUDA開発状況](docs/CUDA.md)を参照してください。

## クイックスタート

必要環境はWindows 10/11、Maya 2026、Bifrost 2.15 SDK、Visual Studio 2022、CMakeです。
CUDA版をビルドする開発機にはCUDA Toolkit 12.xが必要です。生成DLLはCUDA Runtimeを
静的リンクするため、利用側のartist machineにToolkitを要求しません。Mayaを終了してからPowerShellで実行します。

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\install_all.ps1 -MayaVersion 2026 -Configuration Release
```

Maya Script EditorのPythonタブからUIを起動します。

```python
import reaction_diffusion_bifrost
reaction_diffusion_bifrost.show()
```

詳しい使用方法と既存Bifrostグラフの構成は[日本語ガイド](README_JA.md)、設計境界は[Architecture](docs/ARCHITECTURE.md)、開発順序は[Roadmap](ROADMAP.md)を参照してください。

## ローカル検証

```powershell
cmake -S . -B build -DRD_ENABLE_OPENMP=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python tests/test_python_tools.py
```

Bifrost OperatorのビルドとMaya内動作確認には対象マシンのBifrost SDKが必要です。
CUDA PackをインストールしたMaya 2026開発機では、未保存のbatch sceneを使う統合テストも実行できます。

```powershell
$env:MAYA_SKIP_USERSETUP_PY = "1"
$env:PYTHONNOUSERSITE = "1"
$env:RD_MAYA_ALREADY_INITIALIZED = "1"
& "C:\Program Files\Autodesk\Maya2026\bin\mayabatch.exe" -command `
  'python("exec(open(r''D:/path/to/ReactionDiffusionBifrost/tests/test_maya_bifrost_cuda.py'').read())")'
```

## License

[MIT License](LICENSE)
