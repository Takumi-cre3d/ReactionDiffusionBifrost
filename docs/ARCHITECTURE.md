# Architecture

## レイヤー

1. `native/core` — Maya / Bifrost非依存の数値参照実装
2. `native/bifrost` — Amino配列とCore型を変換する薄いNative Operator層
3. `maya_module` — インストール、Seed Painter、グラフ同期、表示専用プレビュー
4. `scripts` — SDK packのビルド、インストール、整合性検証

数値式をUIやBifrostラッパーへ重複実装しません。Surface / Volume / CUDA backendも、参照CPU実装に対して同じ入力から比較可能にします。2D CUDA sourceはCPUと同じ式・Stencil・境界契約を持ち、CUDA build時の自動parity testで検証します。

## 2Dデータ契約

現在の2D Operatorはrow-majorの`width * height` float配列を使用します。

- `concentration_a`, `concentration_b`: Solver State
- `pattern`: B濃度のスカラー出力
- `gradient_u`, `gradient_v`: B濃度の中心差分
- `backend_used`, `status`, `elapsed_milliseconds`: 診断出力

Seedは正規化UV、正規化半径、strength、Add / Erase / Set modeの並列配列です。周期境界ではSeed自体も境界を跨ぎます。

## Feedback State契約

フレーム間Stateは単一の`array<float>`に`[A0..An, B0..Bn]`の順で格納します。
`reaction_diffusion_initialize_state`が初期State、`reaction_diffusion_state_step`が次State、
`reaction_diffusion_state_outputs`が通常のA/B・pattern・gradient出力を生成します。

Native Operatorは静的／グローバルな可変Stateを持ちません。Stateの寿命、開始フレームへのReset、
連続フレームのキャッシュはBifrost標準Feedback Portが担当します。これにより同じOperatorを
通常グラフ、Feedback Compound、将来のキャッシュ処理で再利用できます。

## 数値モデル

Gray–Scott式を陽Euler法で積分します。2Dラプラシアンは参照Web版と同じ、center `-1.0`、上下左右 `0.2`、対角 `0.05`の9点Stencilです。WebGL版との差異を減らすため、既定で各Step後のA/Bを`[0, 1]`へclampします。

## 空間表現

- Grid: 密な2D配列。参照実装とUVワークフローに使用
- Volume: `((z * height) + y) * width + x`の密な3D配列を参照実装に使用。正規化6近傍Stencilで計算し、Operator境界を変えずSparse表現へ移行可能にする
- Surface: メッシュ隣接とcotangent weightを構築し、頂点番号に対応した濃度Stateを再利用する。現状は毎呼び出し再構築し、永続キャッシュは未実装

いずれも濃度State、pattern、空間勾配を共通概念として公開します。

## 非目標

- Solver内で最終メッシュ／ポイント／テクスチャを固定生成すること
- Maya UIがなければ利用できない設計
- CUDAだけに依存し、検証用CPU経路を持たない実装
