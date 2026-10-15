# PoseAnchor 0.1.1 実験版 — 簡単マニュアル

English quick guide follows the Japanese section.

## 1. 最初に

PoseAnchorはWindows x64 / SteamVR / Lighthouse環境のHTC Vive Tracker向け
実験的ドライバです。短い追跡の飛びや欠測の軽減を目指しますが、改善は保証しません。
HMD・コントローラー・Base Stationのposeは変更しません。

実機で同一条件のON/OFF比較による改善効果は未実証です。誤判定や予測で動きが悪化する
可能性があり、自動テストや低いCPU負荷は効果の証明ではありません。0.1.1の実機での導入・
更新・解除、遮蔽や長時間使用の再試験も未実施です。過去版の結果と区別して扱ってください。

初回はSteamVR設定とルームセットアップをバックアップし、周囲の安全を確保して
Tracker 1個から試してください。異常や違和感があれば使用を中止してください。
OpenVR Space CalibratorやMotion Compensationなど、同じpose関数を変更する
ツールとの併用は非対応です。

## 2. インストール・更新

1. SteamVRが使える状態にしてから、SteamVRと「PoseAnchor ステータス」を閉じます。
2. 信頼できる配布元の `PoseAnchor-Setup-<バージョン>.exe` を開きます。
3. セットアップの案内に従います。通常はコマンド操作・管理者権限は不要です。
   既定のインストール先は `%LOCALAPPDATA%\Programs\PoseAnchor` です。
4. 登録成功の案内を確認してセットアップを終了し、SteamVRを起動します。

更新時も同じ手順です。別フォルダの登録を置き換える確認が出た場合は、表示された
旧・新の場所を確認してください。セットアップがSteamVRを勝手に終了することはありません。
未署名版ではWindowsの警告が出る場合があります。配布元・チェックサムを確認し、
端末や組織のセキュリティ方針に従ってください。

このマニュアルはスタートメニューの「PoseAnchor マニュアル」から開けます。
インストール先の `MANUAL.md` をメモ帳で直接開くこともできます。

## 3. 普段の使い方

登録された補正ドライバはSteamVRと一緒に起動・終了します。別の常駐アプリを起動する
必要はありません。正常なposeはそのまま通し、異常・欠測時だけ制限付きの予測・復帰補正を
行います。

状態を確認したいときだけ、スタートメニューの「PoseAnchor ステータス」を開きます。
この画面を閉じても、SteamVRが動いていれば補正は続きます。画面はSteamVRに合わせて
自動で開閉するものではなく、閉じると状態画面のプロセスは終了します。

表示は「確認時刻」のスナップショットです。SteamVRの起動・再起動や設定変更後は
「再確認」を押してください。「詳細を表示」で登録先などを確認できます。

- Tracker一覧：現SteamVRセッションで認識した履歴。現在の接続台数ではありません。
- Hold：短時間の減衰予測。既定では最大150 ms、25 cm / 45°までです。
- Recovery：検証済みの追跡結果へ滑らかに戻す補正です。
- Lost：位置を信頼できないため、無効なposeを報告している状態です。

イベント件数はログで確認できた状態遷移数の下限です。ログが全てあっても出力間隔の
制限で未記録の遷移が残る場合があり、実際の飛びの総数や改善率ではありません。
「補正の準備ができています」という表示も、全Trackerの接続や補正効果の保証ではありません。

## 4. 困ったとき

- 起動直後・待機中：Trackerを起動し、少し待って「再確認」を押します。
- 未登録・別の場所に登録：表示された場所を確認し、SteamVRと状態画面を閉じてから
  セットアップをやり直します。登録されているフォルダを先に削除しないでください。
- 「確認できない」：停止中という意味ではありません。画面の案内に従って再確認します。
  セットアップの実行状態確認が失敗する場合は、案内に従ってWindowsを再起動します。
- 登録・復元の失敗：SteamVRを起動せず、表示された手順で修復します。
  セットアップログの場所はエラーに表示されます。失敗後の完了画面では
  「セットアップログを開く」を選び、「完了」で記録を確認できます。
- 動きが不自然・SteamVR更新後に問題：使用を中止し、下記の手順で解除してください。

長い遮蔽中の正しい絶対位置は復元できません。Base Stationの見通しや反射、無線などの
物理的な原因はこのドライバでは解決できません。SteamVR更新で互換性が崩れる場合もあります。
設定を調整する場合は同梱 `README.md` を参照し、変更後にSteamVRを再起動してください。

## 5. 安全なアンインストール

アンインストーラーはSetupに同梱され、インストール時に自動で配置されます。
別途ダウンロードしたり、コマンドを実行したりする必要はありません。

1. SteamVRと「PoseAnchor ステータス」を自分で閉じます。
2. Windowsの「設定 → アプリ → インストールされているアプリ → PoseAnchor →
   アンインストール」、またはスタートメニューの「PoseAnchor をアンインストールする」を選びます。
3. 確認画面に従って解除します。アンインストーラーはこのインストール先のドライバ登録を
   解除・確認してから、インストール時に記録したファイルとショートカットを削除します。

SteamVRや状態画面を強制終了することはありません。実行中、または実行状態を確認できない
場合は処理を中止します。登録解除や解除確認が失敗した場合もファイル削除前に中止し、
失敗を無視して削除を続ける選択肢はありません。表示された案内に従って問題を解決してから
再実行してください。登録先フォルダだけを先に削除しないでください。

削除対象の登録は、このアンインストーラーのインストール記録にある場所だけです。
別フォルダのPoseAnchor、他のSteamVRドライバ、SteamVRのユーザー設定やログは削除しません。
インストール先を丸ごと再帰削除することもないため、後から追加した独自のファイルがあると
フォルダが残る場合があります。配布ファイル自体を編集した場合は削除対象となるため、
必要な変更は事前に別の場所へバックアップしてください。

## 6. ログを共有する前に

PoseAnchorのドライバ・状態画面・インストーラーには、独自の自動テレメトリ送信や
ログアップロード機能はありません。診断情報はローカルのSteamVRログやセットアップログに
記録されます。SteamVR・Windowsなど他のソフトウェアの通信や記録は別です。

状態画面の「ログを開く」はローカルのログを開く操作です。ログ・画面写真には
Windowsユーザー名、フォルダの絶対パス、Trackerのシリアル番号、PoseAnchor以外の
SteamVR情報が含まれる場合があります。Issuesなどへ載せる前に必ず中身を確認し、
不要な個人情報を伏せ、問題に関係する部分だけを共有してください。
ライセンスとOSS表記は同梱 `LICENSE`、`THIRD_PARTY_NOTICES.md`、`licenses` を参照してください。

## English quick guide

PoseAnchor is an experimental Windows x64 SteamVR driver for HTC Vive Trackers in a
Lighthouse setup. Improvement is not guaranteed. It does not modify HMD,
controller, or Base Station poses. Back up SteamVR settings and room setup, then
start with one tracker in a clear, safe area. Do not combine it with other tools
that replace the same pose function, such as Space Calibrator or Motion Compensation.

Tracking improvement has not been demonstrated by controlled ON/OFF comparisons.
False detections or predictions can worsen motion. Automated tests and low CPU
cost do not prove effectiveness. Live-tracker installation, upgrade, removal,
occlusion, and long-session tests have not been repeated for 0.1.1; earlier
results are not validation of this binary.

1. Close SteamVR and PoseAnchor Status. Run `PoseAnchor-Setup-<version>.exe` from a
   trusted source and follow Setup. No commands or administrator rights are normally
   needed. Use the same procedure for updates. Respect Windows and organization
   security policy if an unsigned build is warned about or blocked.
2. After successful registration, start SteamVR. The driver starts and stops with
   SteamVR; no separate resident app is required. Open **PoseAnchor Status** only
   when needed. Closing Status does not stop filtering while SteamVR is running.
   Status does not automatically open or close with SteamVR.
3. Select **Refresh** after changing the runtime or settings. The screen is a
   timestamped snapshot, not a live monitor. Recognized trackers are session
   history, not the current connection count. **Hold** is brief prediction,
   **Recovery** is a smooth return to tracking, and **Lost** means no reliable pose.
   Counts are lower bounds from logs, not total tracking failures or improvement
   rates; rate-limited events can remain unrecorded even with the complete log.
4. For registration problems, keep SteamVR closed and follow the error. The error
   gives the setup-log location; a failed setup also offers **Open the setup log**
   on its completion page. An inspection failure does not prove SteamVR is stopped.
   Close Status and SteamVR before repair. Do not delete a registered folder first.
5. To remove PoseAnchor, close SteamVR and Status and use **Windows Settings → Apps
   → Installed apps → PoseAnchor → Uninstall**, or **Uninstall PoseAnchor** in the
   Start menu. The uninstaller is bundled with Setup and installed automatically;
   no separate download or commands are needed. It removes and verifies the
   registration for this installation before deleting its recorded files and
   shortcuts. It never force-closes SteamVR or Status. A running process, failed
   process inspection, or failed unregistration/verification stops removal before
   deleting files; there is no continue-anyway option. Resolve the reported problem
   and retry. Do not manually delete the registered folder first.

The uninstaller targets only its own installation path, not another PoseAnchor
copy or other SteamVR drivers. SteamVR user settings and logs are retained. It
does not recursively wipe the installation folder: files you added separately
may leave a folder behind. Files originally installed by Setup remain removal
targets even if edited, so back up any changes you want to keep elsewhere first.

By default, prediction is limited to 150 ms and 25 cm / 45°. Long occlusions cannot
be reconstructed correctly without an independent sensor. Physical tracking
problems and SteamVR compatibility changes may still occur. Stop using PoseAnchor
if motion becomes unexpected. See `README.md` for advanced settings; restart
SteamVR after changes. Open this guide from **PoseAnchor Manual** in the Start menu.

PoseAnchor's driver, Status app, and installer do not implement automatic telemetry
or log uploads. Logs are local; SteamVR and Windows have their own behavior.
**Open log** only opens the local log. Before posting logs or screenshots, redact
Windows usernames, local paths, device serial numbers, and unrelated SteamVR data.
Share only the relevant excerpts. See `LICENSE`, `THIRD_PARTY_NOTICES.md`, and
`licenses` for the license texts and third-party notices.
