# ReactionDiffusionBifrost 0.2.0 Preview 3

Maya 2026 / Bifrost 2.15向けのGray–Scott反応拡散ツールです。
ネイティブBifrost Solverに加え、Seed Painter、Painter→Bifrost同期、
ビューポート表示、Reset/Step操作を一つのMaya UIにまとめています。

本ツールの最終的な高速実行backendはCUDAを第一候補とします。CPUは数値参照、
fallback、CUDA非搭載環境のために維持します。

## 0.2.0 Preview 3の追加内容

- Maya 2026 Python APIで利用できない`MFnMesh.createColorSetWithName`を廃止
- Color Set作成を公式`cmds.polyColorSet`へ変更し、Refresh Existing Outputを修正
- Pythonのみ再インストールした際も既存Bifrost Packを保持
- CUDA Toolkit自動検出とCUDA優先Auto dispatch
- 実験的な2D CUDA ping-pong kernelとCPU/CUDA数値比較テストを追加
- CUDA Volume未実装時のfallback理由を明示

## 0.2.0 Preview 2の追加内容

- MayaのDPI／画面解像度で下部が切れる問題を修正
- コントローラーをリサイズ可能な縦スクロールUIへ変更
- `Refresh Existing Output`をグリッド解像度の直下へ移動

Preview 1から引き続き、次の機能を含みます。

- Bifrostの`pattern`配列をMayaの頂点カラー平面として表示
- PainterのUVシード配列を`reaction_diffusion_grid_step`へ自動同期
- `Total Steps`と`Step +`による決定論的な進行操作
- `Reset (0 steps)`でシード直後の状態を確認
- ストローク完了時の自動同期・自動プレビュー
- 64×64以外のWidth / Heightへ対応
- 表示専用コントラスト正規化
- PainterデータとPreviewメッシュを分離し、Preview削除時もストロークを保持

開発リポジトリでは次期機能として、密な3D voxel配列を扱う実験的CPU参照実装と
`reaction_diffusion_initialize_volume` / `reaction_diffusion_volume_step`を追加しています。
この2ノードはまだMaya 2026 / Bifrost 2.15上での実機検証前です。

数値計算は引き続きC++ネイティブBifrost Operatorで行います。Pythonは
ポート設定、評価要求、頂点カラー表示だけを担当します。

## 必要環境

- Windows 10 / 11
- Maya 2026
- Bifrost 2.15.0.0（SDKを含む）
- Visual Studio 2022 C++ Desktop Development
- CMake

## インストール

Mayaを終了し、このフォルダでPowerShellを開いて実行します。

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\install_all.ps1 -MayaVersion 2026 -Configuration Release
```

7項目が`PASS`になり、最後に次が表示されれば成功です。

```text
ReactionDiffusionBifrost 0.2.0 installation completed successfully.
```

0.1.0は別バージョンフォルダとして残りますが、
`ReactionDiffusionBifrost.mod`は0.2.0をロードするよう更新されます。
インストール後はMayaを完全に終了して再起動してください。

## 既存グラフの準備

トップレベルに次の出力を公開した既存グラフを使用できます。

- 必須: `pattern`
- 推奨: `backend_used`、`status`、`elapsed_milliseconds`

グラフ直下には次のノードを1個ずつ配置します。

```text
reaction_diffusion_initialize_grid
reaction_diffusion_grid_step
```

InitializeのA/B出力をStepのA/B入力へ接続します。Width / HeightはUIが両ノードへ
同じ値を設定します。Stepの`seed_u`、`seed_v`、`seed_radius`、
`seed_strength`、`seed_mode`は未接続にしてください。UIがこれらの既定値を更新します。

## UIの起動

Maya Script EditorのPythonタブで実行します。

```python
import reaction_diffusion_bifrost
reaction_diffusion_bifrost.show()
```

## 最短の表示手順

1. `Use Latest`で対象Bifrost Graphを設定します。
2. Graphと同じ`Width` / `Height`を設定します。初期値は64×64です。
3. UVを持つポリゴンメッシュを選択して`Set Target`を押します。
4. `Paint Add Seeds`でメッシュ上をドラッグします。
5. `Sync Painted Seeds + Preview`を押します。
6. 作成された`RD_SimulationPreview`を選択して`F`キーでフレームします。
7. `Step + Preview`で反応拡散を進めます。

`Auto sync and preview after each painted stroke`がオンなら、手順5は自動です。

## Previewの意味

`RD_SimulationPreview`はUV 0–1グリッドを表す表示専用ポリゴン平面です。
`rdPattern`カラーセットへ`pattern`値を書き込みます。シミュレーション出力自体は
Bifrostのfloat配列のままなので、後続ノードによるメッシュ、ボリューム、ポイント、
テクスチャ変換を妨げません。

`Normalize preview contrast`は表示のみを正規化します。数値結果は変更しません。

## Stepの現在の方式

Preview 2の`Step + Preview`は、`Total Steps`を増やして初期グリッドから再計算します。
同じシードとパラメーターなら同じ結果になるため、数値検証とUndoが簡単です。
長時間計算では再計算量が増えるため、次段階でA/Bをフレーム間Stateとして保持する
Simulation Compoundへ置き換えます。

## 直接Pythonから使用

現在のグラフ出力だけを表示します。

```python
import reaction_diffusion_bifrost as rd
rd.refresh_preview(width=64, height=64)
```

保存済みPainterシードをStepノードへ同期します。

```python
rd.sync_seeds()
```

## 現在の制限

- 検証済みSolver領域は2D / UVグリッドです。3D Volumeはdense CPU実装の実機検証前、メッシュ表面Laplace–Beltramiは未実装です。
- 2D CUDA kernelは実装済みですが、開発機にCUDA Toolkit / `nvcc`がないため未コンパイルです。現在インストール済みPackの実行BackendはCPUです。
- シードポートが別ノードから接続済みの場合、UIは上書きせず警告を返します。
- PreviewはMayaの頂点カラー表示で、入力メッシュへのUVテクスチャ投影は次段階です。
- Deforming SurfaceやUVシーム接続は未実装です。

## 次段階

1. A/B配列をBifrost Simulation Stateとして保持するFeedback Compound
2. `pattern`の入力メッシュUVへの直接表示／ベイク
3. CUDA Toolkit導入後の2D kernel実機コンパイル、CPU数値比較、benchmark
4. Surface SolverとVolume Solver

## ビルド安全策

累積ホットフィックスをすべて含みます。

- Visual Studio 2022 STLとBifrost SDK parserの互換ガード
- `Amino::Cpp` / `Amino::Core`の明示リンク
- Operator関数のDLL export宣言と`dumpbin`検査
- build packの固定パス選択
- インストールDLLのSHA-256一致検査
- Maya Module、Python、Preview、Bridge、Pack Config、DLL、Operator JSONの7項目検証

## ライセンス

MIT License。元の2D Web実装を数値・視覚比較の参考にしています。
