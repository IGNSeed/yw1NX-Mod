# yw1NX-Mod

「妖怪ウォッチ1 for Nintendo Switch」向けMODの開発基盤です。
現在はPhase 0A / 0Bで、ゲーム機能・Hook・メニュー描画・注入処理は未実装です。
このリポジトリの成果物は静的ライブラリで、そのままSwitchに導入できるMODではありません。

基準バージョンは1.4.0、Title IDは`0100C0000CEEA000`です。
EdiZon表示値`7F35BDFC5DE46CF1`は完全なNSO Build IDと区別します。
ゲームダンプ・解析DB・解析結果・ローカルツール・テストは公開しません。

## ビルド

CMake 3.21以上、Ninja、devkitPro/devkitA64が必要です。WindowsのPowerShellでは、
Windows版CMakeとNinjaをPATHに用意し、リポジトリのルートで実行します。

```powershell
cmake -S . -B ../output/build-aarch64 -G Ninja `
  '-DCMAKE_TOOLCHAIN_FILE=cmake/devkit_a64.cmake' `
  '-DDEVKITPRO=C:/devkitPro' '-DCMAKE_BUILD_TYPE=Release'
cmake --build ../output/build-aarch64
```

生成物は`../output/build-aarch64/libyw1nx_foundation.a`です。
Windows版CMakeはdevkitProに含まれるMSYS版とは別に選択してください。
Linuxでは`DEVKITPRO`環境変数または`-DDEVKITPRO=/opt/devkitpro`を指定します。
MSVCなどのホスト環境でも、toolchain指定を省略して静的ライブラリをビルドできます。
ホスト動作の検証とSwitch実機での動作確認は別の判定です。

## 構成と更新への対応

現在の実装は`source/game/versions/`と`source/game/symbols/`です。

- `VersionManager`はTitle ID、モジュール名、32バイトのBuild ID全体を照合します。
  空ID、未知バージョン、重複プロファイルは拒否します。
- `SymbolRegistry`は照合済みプロファイルを受け取り、同じBuild ID・モジュールの
  検証済みシンボルだけを解決します。候補、範囲外、加算オーバーフロー、
  不正な整列、重複シンボルは拒否します。ゲームメモリは読み書きしません。

プロファイル・シンボル一覧は呼び出し元が所有し、参照する管理オブジェクトと照合結果より
長く保持してください。実ゲーム用のプロファイル表やアドレスは未登録です。
新バージョンは完全な識別情報と独立した解析根拠を確認して追加します。

後続Phaseでは、共通初期化を`source/core/`、フック管理を`source/hooks/`、
Switch固有処理を`source/platform/`、描画・入力を`source/ui/`、
個別機能を`source/modules/`へ配置します。各機能から直接アドレスを参照せず、
バージョン照合とシンボル解決を経由します。

## 依存関係

[dependencies.json](dependencies.json)に調査したリビジョンと統合状況を記録しています。
Phase 0では外部コードを同梱・リンクしていません。

- [exlaunch](https://github.com/shadowninja108/exlaunch): GPL-2.0、Make/devkitA64。
- [imgui-xeno](https://github.com/roccodev/imgui-xeno): GPL-2.0、CMake/C++23、NVN backend。
  指定リビジョンに固定されたDear ImGuiと、同梱のNNSDK関連宣言を使用する構成です。
- [Dear ImGui](https://github.com/ocornut/imgui): MIT。独立調査版とimgui-xenoの固定版は別です。

外部ライセンス・著作権表示は統合時に保持し、配布形態を含めて各ライセンスに従います。
NVN、入力、NNSDK ABI、loaderとゲーム固有の接続経路は今後の検証対象です。
外部リポジトリのテストや資料を公開対象へそのまま取り込まない方針です。

## 開発規約

UTF-8・LF、インデント4スペース、原則100桁、C++20を基準にします。
C++はK&R形式、PascalCaseの型、camelCaseの関数・変数、`kPascalCase`の定数、
snake_caseの`.hpp`/`.cpp`を使用します。自作コードのコメントは日本語です。
整形設定は`.clang-format`と`.editorconfig`にあります。

コミット件名は`種類: 日本語の変更概要 / English summary`です。
種類は「実装・修正・改善・整理・設定・文書・テスト」。force pushは禁止です。
