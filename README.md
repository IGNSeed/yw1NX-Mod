# yw1NX-Mod

「妖怪ウォッチ1 for Nintendo Switch」向けMODの開発基盤です。
Phase 1 Stage B.0.1の開発用Bootstrapを追加しました。固定版exlaunchのModule初期化と
`exl_main`への到達を診断します。Switch実機での起動・権限・ログ取得はPENDINGです。
ゲームの描画・入力Hook、ImGui描画、ゲーム機能変更は実装していません。

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

### Bootstrap（開発用）

こちらはCMake 3.24以上、C++23対応devkitA64、devkitProの`elf2nso`が必要です。
上記と別のビルド先を指定します。

```powershell
cmake -S . -B ../output/build-bootstrap-release -G Ninja `
  '-DCMAKE_TOOLCHAIN_FILE=cmake/devkit_a64.cmake' `
  '-DDEVKITPRO=C:/devkitPro' '-DCMAKE_BUILD_TYPE=Release' `
  '-DYW1_BUILD_BOOTSTRAP=ON'
cmake --build ../output/build-bootstrap-release
```

固定リビジョンのexlaunchをビルド領域に取得し、アーカイブとソース全体のSHA-256を
検証します。既に取得した同一ソースを使う場合は`YW1_EXLAUNCH_SOURCE_DIR`を指定できます。
上流のリンカースクリプトとModule初期化を使い、ゲームのRTLDは置換しません。
出力は`yw1_bootstrap.elf`と`yw1_bootstrap.nso`です。自動配置・SDコピーは行いません。

導入には、対象ゲーム専用に検証した開発用NPDMとの組合せが必要です。
元NPDMのままでは必須SVCが不足します。NPDM・ゲーム原本・生成バイナリは公開しません。
対象はAtmosphère 1.12.1で、MesosphereのGetInfo拡張によりprocess handleを取得します。
取得失敗またはProgram ID不一致では上流初期化をスキップします。
上流RwPages初期化中の失敗はabortになる可能性があり、正常起動は実機確認が必要です。
ログは初期化時の`OutputDebugString`だけです。SVC成功はログ受信を保証しません。

SDログ診断版（B.0.1.2）は、別のビルド先で上記に
`-DYW1_BOOTSTRAP_SD_LOGGING=ON`を加えて生成します。初期化イベントを固定32 KiBに蓄積し、
独立した低優先度Workerからraw SVCの非同期CMIF/HIPCで一度だけ追記します。
保存先は`SD:/atmosphere/contents/0100C0000CEEA000/yw1NX-MOD/logs.txt`です。
NNSDK/libnxのFS初期化、ゲームのmount表・TLS・allocatorは使用しません。
libnxの参照リビジョンとISC表示は依存manifestと本README末尾に保持します。

IPCの待機は1要求最大1秒、全体8秒・最大64要求、SM接続は1回です。
Worker作成はkernel内で最大100 msの資源待機を伴い得ます。起動失敗はResultで中断します。
タイムアウト後は取消完了とみなさず、静的バッファと所有セッションを再利用しません。
SMは対象Atmosphèreの`DetachClient`完了後だけ閉じ、ゲームのservice登録を保護します。
Detachの完了が確認できない場合も、追加セッションをプロセス終了まで保持します。
保存用バッファ32 KiBに加え、専用stack64 KiB、IPC/path/完了記録用12 KiB等を使用します。
既存ログを削除・truncateせず、1 MiBから安全余裕分を引いた上限で追記を停止します。
`payload_flush_completed`は直前の起動ログ本体のWrite/Flush成功後に追記する記録です。
自身を含む最後の完了記録のFlush結果はファイル内から自己証明できないため、区別します。
エラー時にゲーム側をabortしませんが、既存exlaunch初期化のabort条件は残っています。
FS権限は既存NPDMのDebug bitに依存し、実機SDアクセス成功はPENDINGです。

上流のIPC fallbackを使わず、専用BSSヒープの限定アロケーターを使用します。
外部動的importを禁止し、専用init関数以外の動的exportを隠すことで、ゲームの
`malloc`やランタイムへの依存・シンボル競合を防ぎます。アロケーターは単一初期化中の
予約ノード専用で、一般用途・並列処理・繰返しHook登録には使用できません。

`source/diagnostics/egl_snapshot.*`はB.0.2向けの未接続の準備コードです。
Bootstrapにリンクしません。実行範囲検証済み関数とcurrent contextを要求し、
GLの照会発行と成功を区別します。EGLエラーの保持・Hookの登録時期・スレッド安全性を
確認するまでB.0.2のビルド・配置は保留します。

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
Bootstrapを有効にした場合のみ、固定版exlaunchの必要なソースを外部からビルドします。

- [exlaunch](https://github.com/shadowninja108/exlaunch): GPL-2.0、Make/devkitA64。
- [imgui-xeno](https://github.com/roccodev/imgui-xeno): GPL-2.0、CMake/C++23、NVN backend。
  指定リビジョンに固定されたDear ImGuiと、同梱のNNSDK関連宣言を使用する構成です。
- [Dear ImGui](https://github.com/ocornut/imgui): MIT。独立調査版とimgui-xenoの固定版は別です。

取得したexlaunchのLICENSE・著作権表示を保持します。リンクした成果物の配布時は
GPL-2.0の条件に従い、対応ソースとこのビルド設定を併せて扱ってください。
今後の描画はDear ImGui公式OpenGL3バックエンドを第一候補とします。
imgui-xenoとその固定ImGui依存はNVN再評価用に保持し、Bootstrapにはリンクしません。
NVN、入力、NNSDK ABI、loaderとゲーム固有の接続経路は今後の検証対象です。
外部リポジトリのテストや資料を公開対象へそのまま取り込まない方針です。

## 開発規約

UTF-8・LF、インデント4スペース、原則100桁、C++20を基準にします。
C++はK&R形式、PascalCaseの型、camelCaseの関数・変数、`kPascalCase`の定数、
snake_caseの`.hpp`/`.cpp`を使用します。自作コードのコメントは日本語です。
整形設定は`.clang-format`と`.editorconfig`にあります。

コミット件名は`種類: 日本語の変更概要 / English summary`です。
種類は「実装・修正・改善・整理・設定・文書・テスト」。force pushは禁止です。

## libnxプロトコル参照の著作権表示

Copyright 2017-2018 libnx Authors

Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted, provided that the above copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
