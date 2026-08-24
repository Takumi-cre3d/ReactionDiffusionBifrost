# Roadmap

## 方針

ReactionDiffusionBifrostは、表示までを抱え込む単体ツールではなく、Bifrostグラフへ数値フィールドを渡す小さく組み合わせ可能なOperator群として開発します。各Solverは濃度A/Bとパターン値、空間勾配、診断情報を出力し、メッシュ化・ポイント化・ボリューム化・テクスチャ化は既存ノードとの組み合わせに委ねます。

## Milestone 0 — Baseline repository

- [x] `0.2.0-preview2`を正本として統合
- [x] 過去スナップショットをGit追跡から除外
- [x] C++ / Pythonのローカル回帰テスト
- [x] CI、設計文書、Contributionガイドを追加
- [ ] Maya 2026 / Bifrost 2.15でクリーンインストール再検証

## Milestone 1 — Stateful 2D node

- Bifrost Simulation StateでA/Bを保持し、毎Stepの全履歴再計算を解消
- Web版のMitosis / Coral / Spots / Stripes / WormsプリセットをCompoundとして提供
- 入力メッシュUVへの直接プレビュー／ベイク
- UVシームを跨ぐ接続方針とテストデータを確定

## Milestone 2 — Volume solver

- [x] 密3D voxel grid初期化、球状Seed、正規化6近傍LaplacianのCPU参照実装
- [x] row-major `float`配列のpattern / XYZ gradient出力
- [x] Bifrost Native Operator宣言とexport検査を追加
- [ ] Maya 2026 / Bifrost 2.15でOperator生成・検索・数値出力を実機検証
- [ ] Bifrost Volume型への変換Compoundと可視化サンプル
- Sparse volumeへ移行可能なデータ境界を維持
- 2D Solverとの数値的一貫性、境界条件、メモリ上限を検証

## Milestone 3 — Surface solver

- 三角形メッシュ隣接情報の構築
- cotangent Laplace–Beltramiによるメッシュ表面拡散
- 頂点／面／位置ベースSeed
- トポロジ固定の変形メッシュとState再利用

## Milestone 4 — Acceleration and production hardening

- CPUタイル化とベンチマーク基準の確立
- CUDA ping-pong backend
- キャッシュ／再開／決定論モード
- Maya/Bifrost対応表、サンプルグラフ、Release pack、CI成果物

## 完了条件

- 2D、Surface、Volumeが同じGray–Scottパラメータ体系とSeed操作を共有する
- Solver出力を既存Bifrostノードだけでメッシュ、ポイント、ボリューム、テクスチャへ変換できる
- Maya UIは補助機能であり、コア機能はBifrost Operator単体で使用できる
- 各backendは参照CPU実装と許容誤差内で一致し、自動テストで回帰を検出できる
