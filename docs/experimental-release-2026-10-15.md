# PoseAnchor 0.1.1 実験版 — 配布準備記録

日付: 2026-10-15（日本時間）。この記録はローカルでの配布準備を扱い、公開済みを意味しません。

## 公開時の説明文

PoseAnchor 0.1.1は、SteamVR / Lighthouse環境のHTC Vive Tracker向け実験的フィルターです。
短時間の異常や欠測の影響の緩和を目指しますが、実機で同一条件のON/OFF比較による改善効果は
未実証です。誤判定や予測で動きが悪化する可能性があります。長い遮蔽中の本当の位置は復元
できず、反射・見通し・無線などの物理的な原因も解決しません。安全な場所でTracker 1個から
試し、異常や違和感があれば使用を中止してください。正式版・効果保証のある製品ではありません。

PoseAnchor 0.1.1 is an experimental SteamVR / Lighthouse filter for HTC Vive Trackers.
Tracking improvement has not been demonstrated by controlled ON/OFF comparisons.
False detections or predictions can worsen motion. It cannot recover the true position
during long occlusions or fix physical tracking problems. Start with one tracker in a
clear, safe area and stop if motion is unexpected. This is not a stable release or a
product with guaranteed effectiveness.

## この版で変更したこと

- 版番号を0.1.1に進め、旧0.1.0候補と区別。SetupとWindowsアプリ一覧にもexperimentalを表示。
- ソースREADME、配布README、日英マニュアル、日英セットアップ注意書きに検証の限界を明記。
- READMEの検証・性能リンクを更新。過去の実機結果と今回の検証を混同しないように整理。
- 配布フォルダを最新版のSetup EXEとSHA-256ファイル1組に整理。旧候補・ログは非公開退避。
- フィルターの判定・予測・補正アルゴリズムは変更していない。実機効果の改善を示す更新ではない。

## 検証と未実施項目

今回のローカル検証結果:

- Windows x64 / MSVC Releaseビルド、CTest **8/8合格**。
- セットアップ・文書の回帰チェック **148項目合格**（Windows PowerShell 5.1 / PowerShell 7）。
- 配布物のRelease → Debug → Release切替テスト合格。最終配置は0.1.1 / Release。
- 隔離アンインストーラー **6シナリオ・71項目合格**（両PowerShell版）。
- 古い同梱文書を`-SkipBuild`が拒否することを、ハッシュ取得の模擬入力でも確認。
- 同梱文書・ライセンス原文のハッシュを元ファイルと照合。配布用のEXE / DLLで、
  Windowsユーザーの絶対パス形式の埋め込みは今回のASCII / UTF-16検索では検出されなかった。
  これはあらゆる秘匿情報の不存在を保証する検査ではない。
- Inno Setup **6.7.3**で最終Setupをコンパイル。バージョン情報0.1.1、AuthenticodeはNotSigned。
- 旧候補とログの計17ファイルを非公開バックアップへ移し、移動前後のSHA-256一致を確認。
  `dist/`を再帰確認し、最新版EXEとチェックサムの2ファイルだけであることを確認。
- GitHubのCIは今回の未push変更に対して実行していない。上記はローカルの検証結果。

未実施・未実証の範囲:

- 0.1.1での実Trackerを使った新規導入・更新・解除、SteamVRロード、遮蔽、連続キャリブレーション、
  長時間使用、別PCでの試験は未実施。
- 同一条件の実機ON/OFF比較による飛び・真の位置精度・復帰時間・追加遅延の改善は未実証。
- 合成入力の回帰テストは設計した処理の確認であり、実際のトラッキング改善の証明ではない。
- 隔離アンインストーラーテストは模擬登録と無害なファイルを使う。実SteamVR登録試験の代替ではない。
- 過去の性能・UI監査は[2026-10-15の記録](performance-ui-audit-2026-10-15.md)、
  以前のリリース監査は[2026-09-26の記録](release-audit-2026-09-26.md)を参照。

## 配布対象と公開条件

- 対象: `dist/PoseAnchor-Setup-0.1.1.exe` と `dist/PoseAnchor-Setup-0.1.1.exe.sha256` のみ。
- SetupのSHA-256: `71dfb07f1f76bfdd9502a76f9b8fe566963e6da332a4f71332e971c391386830`。
- Setupには日英README・マニュアル、MIT / 第三者ライセンス原文、アンインストーラーを同梱。
- Authenticode未署名。Windowsや組織の方針により警告・ブロックされる場合がある。
- GitHubで公開する場合はPre-releaseにし、上記の実験版説明と未実施項目を掲載する。
  `dist/`全体、旧候補、試験ログ、PDB、秘密鍵、非公開バックアップを添付しない。
- 最新バイナリの実機検証前は、制限に同意したテスター向けとして扱う。
- GitHub上の旧コミットに残る個人メールの削除依頼は別件で継続中。
  今回の資料整備は、過去データが完全に削除されたという宣言ではない。
- ローカル生成のみで、GitHub Releaseの作成・アップロード、インストール先やSteamVR登録の変更は行わない。

## 異常時の中止・復旧

予想外の動き、誤補正、SteamVRの不安定化があれば使用を中止する。SteamVRと状態画面を終了し、
同梱の[マニュアル](../MANUAL.md#5-安全なアンインストール)に従って解除する。
登録解除に失敗した場合は登録先フォルダを手動削除せず、表示された案内に従って修復する。
問題報告には関連箇所のみを使い、ユーザー名・パス・Trackerシリアルなどを伏せる。
