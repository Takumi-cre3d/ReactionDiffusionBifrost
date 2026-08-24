# Contributing

## 基本ルール

- 数値処理はまず`native/core`へMaya非依存で実装し、単体テストを追加します。
- `native/bifrost`は型変換とOperator公開に限定します。
- Maya PythonコードはMayaなしで検証できる純粋関数をできるだけ分離します。
- 生成物、ローカルSDK、過去配布スナップショットはcommitしません。

## Pull request前の確認

```powershell
cmake -S . -B build -DRD_ENABLE_OPENMP=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python tests/test_python_tools.py
```

Bifrost Operatorを変更した場合は`README_JA.md`のMayaチェックも実行し、Maya / Bifrost / Visual Studioのバージョンと結果をPRへ記録してください。
