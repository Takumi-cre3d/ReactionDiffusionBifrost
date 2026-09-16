# Preview 8 コア機能とデータ契約

Maya 2026 / Bifrost 2.15、CUDA 12.6、RTX 4070 Ti SUPERを検証対象とします。
ソルバーはすべてNative Operatorです。Python UIを使わずグラフ内で組み合わせられます。

## 完成要件への対応

| 要件 | 実装 |
|---|---|
| 2D・Surface・Volume | Gray–Scott CPU / CUDA。AutoはGPU利用可能ならCUDA |
| 入力メッシュに沿った拡散 | 三角形のcotangent Laplace–Beltrami、lumped mass、自然境界 |
| 3D空間での拡散 | dense 3D配列、6近傍、Periodic / NoFlux / FixedInitial |
| 他ノードで使える結果 | A/B、pattern、UVまたはXYZ gradient、明示的State、診断 |
| 再生・リセット | 標準Feedback、連続フレーム、開始フレームへのリセット |
| Seed操作 | Add / Erase / Set、フレーム別イベント、Web版5プリセット |
| ポイント・形状・テクスチャ | samples / pixelsアダプターと標準Bifrostノード |

`backend_used`を確認してください。CUDAが使用できないAuto環境はCPUへ戻ります。
CPUとCUDAは同じ入力で比較します。FMA融合を無効にして丸め順序を揃えていますが、
異なるGPU・コンパイラーを含むビット単位の同一性を保証するものではありません。

## 使用例

MayaのPythonタブ:

```python
import reaction_diffusion_bifrost as rd
rd.show()
# UIのCreate Surface Graphは選択メッシュを入力にします。
# Create Volume Graphは24³のサンプルと中央Zスライスを表示します。
```

```python
surface = rd.create_surface_graph("pSphere1", start_frame=0, substeps=8)
volume = rd.create_volume_graph(dimensions=(32,32,32), start_frame=0, substeps=8)
```

Surfaceは標準`triangulate_mesh` / `get_mesh_structure`経由で入力メッシュを読みます。
頂点番号・接続が変わらない変形はA/Bを維持します。トポロジを変更した場合はリセットが必要です。
`live=False`は作成時の形状を固定します。UVを接続計算に使用しないため、UVシームで拡散は切れません。
幾何的に別頂点へ分離した境界は接続されません。退化面・非多様体辺・孤立頂点は拒否します。

Surfaceの距離は入力座標の単位です。小さな三角形には小さな`time_step`が必要です。
拡散安定性のため自動細分ステップを使いますが、過大な分割数はエラーにします。
Volume / Gridは正規化Seed座標を使用します。Volume配列順は`(z*height+y)*width+x`です。

```python
from reaction_diffusion_bifrost import seed_events, presets
presets.apply(volume["graph"], "Coral")
seed_events.set_events(volume["graph"], [
    dict(frame=10, position=[.25,.5,.5], radius=.1, strength=1, mode=0),
    dict(frame=20, position=[.25,.5,.5], radius=.1, strength=1, mode=1),
])
```

イベント変更後は開始フレームから再生してください。飛ばしたフレームのSeedは注入されません。
タイムラインは「Play every frame」を使用します。逆再生や任意フレームへのジャンプを
全履歴の再計算と同等とは扱いません。開始フレームへ戻してから順に評価してください。
2D Painterは次フレームにイベントを登録します。Surface / VolumeへのUV Painter流用は拒否し、XYZ Seedを使用します。

## 標準Bifrost出力

`reaction_diffusion_samples`は閾値以上のサンプルを`float3[]`位置・半径・濃度へ変換します。
サンプルグラフは`construct_points`と`set_geo_property`で通常のPoints Objectを出力します。
濃度は`point_pattern`、半径は`point_size`です。Volume位置は0〜1の格子座標です。
TransformやInstance、Particle処理を下流へ自由に接続できます。

```python
from reaction_diffusion_bifrost import outputs
outputs.add_volume_mesh(volume["graph"], volume["points_source"], detail_size=.04)
grid = rd.create_stateful_preview_graph(64,64,15,0)
outputs.add_image(grid["graph"], grid["outputs_node"]+".pattern", 64,64)
```

Volume / Meshアダプターは`points_to_volume`→`volume_to_mesh`です。
これは選別したポイントの形状化であり、元の密な濃度配列を完全に復元する変換ではありません。
元のpattern配列は変更されません。ポイント半径と解像度は用途に合わせて調整してください。

画像はRGBA `(B,B,B,1)`、左下始点row-majorです。濃度を正規化・色変換しません。
`construct_image` / `construct_texture`の出力を`sample_texture`でUV参照したり、
`write_texture`でEXRへ保存できます。数値用途は`file_color_space=raw`を指定します。
Surface濃度をUV画像へベイクする場合は標準`bake_texture_samples`などでUVサンプルを構築します。
任意メッシュへの自動UVベイクUIは含めません。

## State・キャッシュと未実装の高速化

明示的Stateは`[A...,B...]`。pack/unpackで外部の初期状態や保存した状態をSolverへ再入力できます。
Geometry出力のディスクキャッシュは標準`file_cache`を使用できます。
専用のチェックポイント管理UI・自動キャッシュ再開は未実装です。

GPU bufferはOperator呼び出しごとに確保・転送します。GPU常駐State、Sparse GPU Volume、
Surface隣接情報の永続キャッシュは未実装です。これらは性能拡張であり、完了したものとして扱いません。
Python頂点カラーは表示専用です。大量点での表示速度はSolver時間とは別に測定してください。

プリセット値の出典: [TuringPattern_Generator](https://github.com/Takumi-cre3d/TuringPattern_Generator)（2026-09-17確認）。
