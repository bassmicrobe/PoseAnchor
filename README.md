<picture>
  <source media="(prefers-color-scheme: dark)" srcset="assets/branding/poseanchor-logo-horizontal-white.svg">
  <source media="(prefers-color-scheme: light)" srcset="assets/branding/poseanchor-logo-horizontal-ink.svg">
  <img alt="PoseAnchor" src="assets/branding/poseanchor-logo-horizontal-ink.svg" width="320" height="48">
</picture>

# PoseAnchor 0.1.2

**SteamVR / Lighthouse環境のVive Trackerだけを対象に、短時間の異常poseや欠測の影響緩和を目指す、実験的なWindows x64サーバードライバです。**

[![CI](https://github.com/bassmicrobe/PoseAnchor/actions/workflows/ci.yml/badge.svg)](https://github.com/bassmicrobe/PoseAnchor/actions/workflows/ci.yml)

## 概要

`vrserver.exe`内にロードされるフィルタ層です。LighthouseドライバがSteamVRへ報告する
`DriverPose_t`をMinHookで横取りし、Vive Trackerのものだけを検査・補正してから
本来の受け口へ渡します。SteamVR起動時に自動ロードされ、常駐アプリは不要です。
Base Station 2〜4台の構成を想定しています。HMD、コントローラー、Base Station、
他社Trackerのposeには触れません。

**改善効果は未実証です。** 実機で同一条件のON/OFF比較による、飛びの減少・位置精度・
復帰時間・追加遅延の改善は確認していません。誤判定や予測により、動きが悪化する可能性も
あります。自動テスト合格や低いCPU負荷は、トラッキング改善の証明ではありません。
正式版・効果保証のある製品としてではなく、検証に協力できる方に向けた実験版として扱ってください。

Experimental build: tracking improvement has not been demonstrated by controlled ON/OFF
comparisons. False detections or predictions can worsen motion. Automated tests and CPU
benchmarks do not establish real-world tracking benefits.

判断できない状況では元のposeを素通しします（fail-open）。公開されたpose差し替えAPIではなく
MinHook方式のため、SteamVR更新で壊れる可能性があり、OpenVR Space Calibratorなど同じpose関数を
フックするドライバとの同時利用は非対応です。処理の詳細と制約は
[フィルタの挙動と設定](docs/behavior.md)を参照してください。

## 技術スタック

- C++20 / CMake 3.24以降 / Visual Studio 2022（MSVC、Windows x64ドライバ）
- OpenVR v2.15.6（ヘッダー・API、commit固定）
- MinHook v1.3.4（pose callbackフック、静的リンク、commit固定）
- Inno Setup 6（日英GUIインストーラー）
- PowerShell（ビルド・登録・ベンチマーク）
- CTest。GitHub ActionsでMSVCビルド+テストと、コアフィルタ・状態パーサーのclang ASan/UBSan

## セットアップ

### 配布版（Windows x64・実験版 0.1.2）

- [インストーラー：PoseAnchor-Setup-0.1.2.exe](https://github.com/bassmicrobe/PoseAnchor/releases/download/v0.1.2/PoseAnchor-Setup-0.1.2.exe)
- [SHA-256チェックサム：PoseAnchor-Setup-0.1.2.exe.sha256](https://github.com/bassmicrobe/PoseAnchor/releases/download/v0.1.2/PoseAnchor-Setup-0.1.2.exe.sha256)
- [リリース説明・検証範囲・注意事項](https://github.com/bassmicrobe/PoseAnchor/releases/tag/v0.1.2)

インストーラーは未署名です。Windowsや組織の方針によって警告・ブロックされる場合があります。
SteamVRと「PoseAnchor ステータス」を完全に終了してから実行してください。
コマンド操作や管理者権限は不要で、既定では`%LOCALAPPDATA%\Programs\PoseAnchor`へ入ります。

[簡易マニュアル](MANUAL.md)に、インストール・状態確認・困ったとき・アンインストールを
まとめています。マニュアルは配布版にも同梱し、インストール後はスタートメニューの
「PoseAnchor マニュアル」から開けます。

### ソースからビルド

Visual Studio 2022の「C++によるデスクトップ開発」、CMake、Windows SDK、Gitが必要です。
不足している場合は管理者PowerShellで`.\scripts\install-build-tools.ps1`を実行します。

```powershell
.\scripts\build.ps1
```

成功すると`build\pose_anchor\`にSteamVRへ登録できる配布物が生成されます。
GUIインストーラーは[Inno Setup 6](https://jrsoftware.org/isinfo.php)を入れたうえで
`.\scripts\build-installer.ps1`です。署名・検証・手動登録の詳細は
[ビルド・配布・検証](docs/development.md)を参照してください。

## 使い方

インストールと登録が正常に完了していれば、補正本体はSteamVRと一緒に起動・終了します。
「PoseAnchor ステータス」は必要なときだけ開く確認画面で、閉じていても補正は動きます。
初回はSteamVR設定とルームセットアップをバックアップし、転倒や衝突につながりにくい
Tracker 1個から確認してください。

解除はSteamVRと状態画面を閉じてから、Windowsの
「設定 → アプリ → インストールされているアプリ → PoseAnchor」またはスタートメニューの
「PoseAnchor をアンインストールする」から行えます。

設定の既定値は`driver/pose_anchor/resources/settings/default.vrsettings`です。
変更はSteamVR再起動後に反映されます。項目の意味は[挙動と設定](docs/behavior.md)を参照してください。

## 構成

```text
assets/branding/      ロゴ・アイコン
docs/                 詳細ドキュメントと検証記録
driver/pose_anchor/   SteamVRマニフェストと既定設定
include/pose_anchor/  フィルタ公開ヘッダ
installer/            Inno Setupスクリプト
licenses/             第三者ライセンス原文
scripts/              ビルド・登録用PowerShell
src/core/             OpenVR非依存のフィルタ
src/driver/           SteamVRサーバードライバ
src/status/           オンデマンド状態画面
tests/                テストとベンチマーク
```

## ドキュメント

- [簡易マニュアル](MANUAL.md) — インストール・状態確認・困ったとき・アンインストール
- [フィルタの挙動と設定](docs/behavior.md) — pose処理、状態遷移、制約、初期設定
- [ビルド・配布・検証](docs/development.md) — ビルド、手動登録、検証状況、性能方針
- [公開準備チェックリスト](docs/public-release-checklist.md) — 公開・再配布前の確認項目
- [0.1.2 ロゴ更新記録](docs/branding-update-2026-10-25.md)
- [0.1.1 実験版の配布準備記録](docs/experimental-release-2026-10-15.md)
- [性能・UI監査（2026-10-15）](docs/performance-ui-audit-2026-10-15.md)
- [性能・UI監査（2026-10-05）](docs/performance-ui-audit-2026-10-05.md)
- [リリース監査（2026-09-26）](docs/release-audit-2026-09-26.md)
- [インストーラー](installer/README.md) — Inno Setupの登録・解除フロー

## ライセンス

PoseAnchor自身のコードとドキュメントは[MIT License](LICENSE)です。利用・改変・再配布・
商用利用が認められていますが、著作権表示と許諾文の保持が必要です。無保証で提供されます。
依存物まで一律にMITになるわけではなく、それぞれのライセンスが適用されます。

| 対象 | 用途 | ライセンス / 原文 |
| --- | --- | --- |
| PoseAnchor | 本体・ドキュメント | [MIT](LICENSE) |
| OpenVR v2.15.6 | Valveのヘッダー・API（固定コミット） | [BSD-3-Clause](https://github.com/ValveSoftware/openvr/blob/0924064316de3effbcd1acf1e309182a2deb1c05/LICENSE) |
| MinHook v1.3.4 | ドライバーに静的リンクするフック実装（固定コミット） | [BSD-2-Clauseと同梱HDE 32/64の通知](https://github.com/TsudaKageyu/minhook/blob/c3fcafdc10146beb5919319d0683e44e3c30d537/LICENSE.txt) |
| Inno Setup 6 | ビルド用ツール。ランタイムはSetup・アンインストーラーに含まれる | [Inno Setup License](licenses/Inno-Setup-LICENSE.txt) |
| OpenVR Space Calibrator | フック構成の参考。ソースファイル自体は同梱していない | [MITの参考実装通知](licenses/OpenVR-SpaceCalibrator-LICENSE.txt) |

詳細な帰属・固定コミットは[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)を参照してください。
配布パッケージとインストーラーには、`LICENSE`、`THIRD_PARTY_NOTICES.md`、`licenses/`の
各原文を含めます。再配布する場合もこれらを残してください。SteamVR本体は同梱しません。
SteamVR・VIVE等の名称は各権利者に帰属し、本プロジェクトはValve・HTCによる公式製品や
動作保証を意味しません。
