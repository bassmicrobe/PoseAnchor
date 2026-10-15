# リリース前監査 — 2026-09-26

対象は `7931c10` を基点とする作業ツリー。コア、ドライバー、状態画面、
インストール・解除、配布ビルド、依存ライセンス、CIを確認して修正した。
自動検証と未署名の候補インストーラー生成は完了。今回の修正を含む実機検証と
新しいGitHub Actions実行はまだ完了していないため、正式版の公開判定は保留。

## 修正した問題

| 重要度 | 問題と影響 | 対応 |
| --- | --- | --- |
| P1 | 新しい実測pose・切断通知の後に古いwatchdog補間poseが送られ得た | Tracker単位で転送を直列化し、世代番号を送信直前に確認。例外で生poseを通す場合も古い補間を失効 |
| P1 | 無効なサンプルや通信途絶をまたいで復帰候補が蓄積された | 候補を破棄し、確認時間をposeの予測時刻ではなく到着時刻で測定。修正前に28件の失敗を再現 |
| P1 | DebugとReleaseが同じリンク先を使い、差分ビルドでDebug版を配布する可能性 | 構成別出力を毎回ステージング。構成・バージョンのメタデータを最後に生成済み配布フォルダへ反映し、パッケージ作成前に検証 |
| P1 | `VR_PATHREG_OVERRIDE` 使用時に登録変更先と復元先が異なった | GUIと開発用スクリプトで変更を拒否。初回登録も登録ファイル全体のバックアップ対象に追加 |
| P1 | 前回のWindows CIがConfigureで失敗し、ドライバーテストを実行できていなかった | VS2022を含む `windows-2022` を明示。checkoutをNode24対応版のSHAへ固定し、配布構成の切替試験とインストーラー生成もCIに追加 |
| P2 | 別の同一バージョンhost実装が既存フックの転送先を上書きし得た | 同一バージョンの異なるフック対象を拒否し、既存転送先を維持 |
| P2 | RunFrameとCleanupが重なった際にコンテキスト解放が先行し得た | 例外時のログ出力も含め、処理中フレームの終了を待つ |
| P2 | 未対応Trackerの欠落プロパティを毎フレーム繰り返し取得 | 初回は即時、以降100 msから最大1秒の間隔で再試行 |
| P2 | 別ドライブのSteamVRでログを誤検出し、確認不能状態を正しく表示しなかった | OpenVRのログ設定と環境変数を参照。ローカルパスだけを読み、証拠不足は「確認できない」と表示 |
| P2 | 状態画面がログ本文を最大16 MiB保持し、大量Trackerで表示が伸び過ぎた | 要約後に本文解放、Tracker表示を6件までに制限。完全な一覧はログで確認 |
| P2 | 不正UTF-8、JSON以外の空白、奇数バイトのレジストリ文字列を受理し得た | 厳格に拒否し、JSONパーサーに回帰テストを追加 |
| P2 | 開発用登録・解除がツールの終了コードだけで成功扱い | 操作直前にSteamVRを再確認し、操作後に登録状態を照合 |
| P2 | ベンチマーク出力の欠落項目が0として扱われた | 必須キー、数値形式、有限値、非負を検証してから集計 |

READMEの予測精度に関する過剰な保証、起動と再捕捉の条件の混同、署名が必須と
読める表現も修正した。過去の実機測定値は過去版の結果であることを明記した。

## 今回の検証結果

| 検証 | 結果 |
| --- | --- |
| MSVC 19.44 x64 Releaseビルド | 成功 |
| CTest | 6/6成功：core、status parser、installer scripts、pose adapter、device registry、hooks |
| ASan + UBSan（既存のZig/clang Windows構成） | 3/3成功：core、status parser、installer scripts |
| PowerShellヘルパーの回帰試験 | 18項目成功、Windows PowerShell 5.1とpwsh |
| 実際のMinHookを使う模擬host試験 | 新旧pose競合、切断、同一スレッド再入、例外、解除、転送先維持を確認。SteamVRには接続しない |
| 構成切替試験 | Release → Debug → Releaseで、DLL・EXEのSHA-256が各構成の生成物と一致 |
| 誤構成での `-SkipBuild` | インストーラー出力作成前に拒否 |
| PE検査 | x64、`HmdDriverFactory` エクスポート、ASLR・NXを確認。DLL/EXEはWindows標準DLLにのみ依存 |
| Inno Setup 6.7.3 | 日英リソース・コード・同梱アンインストーラーのコンパイル成功。配布対象を明示し、古いテスト用DLLやPDBを含めない |
| 状態画面 | 起動し、Escapeによる終了後にウィンドウ・プロセスが残らないことを確認 |
| 差分 | `git diff --check` 成功 |

サニタイザでは既存の浮動小数点境界テストがコンパイラーの丸めに依存していたため、
二進表現が厳密な入力に修正して再検証した。ビルド中に一度発生したZigキャッシュの
読み取りエラーは、逐次ビルドで解消した。

依存ソースの再取得はTLS通信エラーで失敗したため、ローカルに保存されていた
同じOpenVR固定コミット `0924064316de3effbcd1acf1e309182a2deb1c05` のクリーンな
チェックアウトから復旧した。依存バージョンや証明書検証は変更していない。
新規PCからの依存取得を含むクリーンCIは、次回push後の確認対象。

## 性能の確認

Release、各100万サンプル、7回の中央値。入力生成を除くコア処理の値。

| 処理 | 中央値 | 最小〜最大 |
| --- | ---: | ---: |
| 正常Tracking | 110.84 ns/sample | 108.02〜119.50 |
| 外れ値・Hold・Recovery系列 | 152.78 ns/sample | 144.68〜158.66 |
| 分類待ちなし | 0.79 ns/call | 0.75〜1.01 |
| 64スロットwatchdog走査 | 63.29 ns/frame | 60.52〜68.10 |

通常・外乱の計測区間のヒープ確保は0件。状態画面のPrivate memoryは単回観測で
約3.76 MiB。複数アプリとSteamVRが起動中の測定であり、過去版との厳密な性能比較や
VR実機での改善量の測定ではない。コアの数値にドライバー全体のロック・OS・SteamVR費用は含まれない。

## 候補配布物と残る確認

- 未署名インストーラー：`dist/release-audit-2026-09-26/PoseAnchor-Setup-0.1.0.exe`
- SHA-256：`e81c75e5c31c933256de5c1c5830ff3025f72dafe1dccd95dc525861a85961b6`
- 同じフォルダに `.sha256` を生成。Authenticodeは `NotSigned`。

今回の検証では稼働中のSteamVR、登録済みドライバー、ユーザー設定を変更していない。
候補版を使った新規インストール・更新・解除・ロールバック、実Trackerの遮蔽・高速運動・
切断・再接続・SteamVR終了は、リリース前に隔離した環境で再試験する必要がある。
署名付きビルドも証明書を使った実行確認は未実施。

Computer Useで画面の目視確認を試みたが、キャプチャが
`D3D11CreateDevice ... 0x8007000E`（メモリ不足）で失敗し、アクセシビリティも取得できなかった。
起動・終了は確認できたが、レイアウト、高DPI、多数Tracker時の見た目は未確認。

CIは設定修正済みだが今回の変更をまだpushしていない。GitHub上の最新実行は過去版の失敗であり、
修正後のCIが成功したとは扱っていない。

## 根拠と再現手順

- [前回失敗したCI](https://github.com/bassmicrobe/PoseAnchor/actions/runs/33580606382/job/100093892929)：`windows-2025-vs2026` 上でVS2022のインスタンスが見つからず失敗。
- [GitHubのWindows2022環境](https://github.com/actions/runner-images/blob/main/images/windows/Windows2022-Readme.md)：VS2022・Inno Setupを提供。
- [checkoutの公式説明](https://github.com/actions/checkout/blob/main/README.md)：v6のNode24対応。

```powershell
.\scripts\build.ps1 -Configuration Release
.\tests\package_staging_tests.ps1
.\scripts\benchmark.ps1 -SkipBuild -Runs 7
.\scripts\build-installer.ps1 -SkipBuild -OutputDirectory dist\release-audit-2026-09-26
```
