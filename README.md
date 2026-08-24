# ReactionDiffusionBifrost

Maya 2026 / Bifrost 2.15向けの、Gray–Scott反応拡散シミュレーション専用ノードです。

目標は、[TuringPattern Generator](https://github.com/Takumi-cre3d/TuringPattern_Generator)のインタラクティブな2Dシミュレーションを出発点に、Bifrostグラフ内で再利用できる汎用ノードへ発展させることです。最終的にはUVグリッドだけでなく、入力メッシュ表面と3Dボリューム上で反応拡散を計算し、`float`フィールド、勾配、メッシュ、ポイント、ボリューム、テクスチャなど任意の後続処理へ接続できる構成を目指します。

## 現在地

`0.2.0-preview2`を引き継ぎ元の正本としてリポジトリ化しました。

- C++17のGray–Scott CPUソルバー（OpenMP対応）
- Bifrost Native Operator: 2Dグリッド初期化／ステップ
- 実験的な3D dense volume CPUソルバー／Native Operator
- Periodic / NoFlux / FixedInitial境界
- UV Seed PainterとBifrost配列の同期
- Maya頂点カラーによる非破壊プレビュー
- 決定論的なReset / Step操作
- C++単体テストとMaya非依存Python回帰テスト

2D / UVグリッドが検証済みの基準実装です。3D Volumeは密配列CPU参照実装まで追加済みですが、Maya内検証、Bifrost Volume型への変換、Sparse化は未完了です。Surface Solver、Simulation State、CUDAもロードマップ上の開発項目です。

## クイックスタート

必要環境はWindows 10/11、Maya 2026、Bifrost 2.15 SDK、Visual Studio 2022、CMakeです。Mayaを終了してからPowerShellで実行します。

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

## License

[MIT License](LICENSE)
