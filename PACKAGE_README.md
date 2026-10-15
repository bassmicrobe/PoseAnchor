# PoseAnchor 0.1.0 — experimental build

English installation, status, and recovery instructions follow the Japanese section.

SteamVR / Lighthouse環境のHTC Vive Trackerだけを対象に、短いtracking jumpや
Out-of-Rangeの抑制を目指すWindows x64サーバードライバです。改善を保証するものではありません。HMD、コントローラー、
Base Stationには触れません。

## インストール

これは実験版です。最初は転倒や衝突につながりにくいTracker 1個で試してください。
SteamVRと「PoseAnchor ステータス」を終了し、OpenVR Space Calibrator、Motion Compensation
など同じpose関数を変更するツールを無効化してから、配布された`PoseAnchor-Setup-0.1.0.exe`を
ダブルクリックします。セットアップがSteamVRを検出し、ドライバ登録まで自動で行います。
コマンド操作や管理者権限は不要です。

SteamVRが実行中の場合、セットアップはファイルや登録を変更せずに終了を案内します。
SteamVRを自動終了することはありません。完了画面の案内に従い、必要なら同梱READMEを開いて
からSteamVRを起動してください。

SteamVRを起動し、スタートメニューの「PoseAnchor ステータス」で登録と補正の準備状態を
確認します。表示は確認時刻のスナップショットです。SteamVRの起動や設定変更後は
「再確認」を押してください。自動更新や常駐は行わず、閉じるとプロセスは残りません。
Tracker一覧は現セッションで認識した履歴で、現在の接続台数ではありません。
Hold（短時間の予測）、Recovery（復帰補正）、Lost（位置を信頼できない状態）の件数は
ログで確認できた遷移数の下限です。ログが揃っていても出力間隔の制限により未記録の遷移が
残る場合があります。実際の飛びの総数や改善率を表すものではありません。
詳しい記録は画面の「ログを開く」から確認できます。
不具合があればSteamVRと「PoseAnchor ステータス」を終了し、Windowsの
「設定 → アプリ → インストールされているアプリ → PoseAnchor」またはスタートメニューの
「PoseAnchor をアンインストールする」を選びます。
同梱アンインストーラーがSteamVR登録解除後にファイルを削除します。

セットアップで登録に失敗した場合はSteamVRを起動せず、表示された案内に従ってください。
エラー画面にはセットアップログの場所が表示されます。失敗後の完了画面で
「セットアップログを開く」を選択し、「完了」を押すと記録を確認できます。
「実行中か確認できませんでした」と表示された場合は、アプリの起動を検出したという意味では
ありません。安全確認に失敗したため処理を中断しています。表示された再起動の案内を試しても
解決しない場合は、エラーやログを添えてプロジェクトのIssuesへ報告してください。

SteamVR本体へファイルはコピーせず、ユーザーごとのPoseAnchorフォルダをexternal driverとして
登録します。アップグレードや再インストール時も、SteamVRと「PoseAnchor ステータス」を
終了してから同じインストーラーを実行できます。

## 挙動と限界

- 起動時は8個の整合したposeを確認するまでTrackerを一時的に無効化します。
- 正常時は元poseを無加工で通し、固定平滑化遅延は加えません。
- poseが連続していて報告速度だけ不整合な場合は、poseを止めず速度成分だけを修復します。
- 異常時は最大150 ms、かつ最大25 cm / 45°まで減衰予測します。
- pose callback停止時もwatchdogが同じ上限内で合成poseを送ります。
- 150 msを超えるとOut-of-Rangeにし、安定したraw poseを80 ms確認して再開します。
- Hold軌道から0.75 m / 90°を超える再捕捉は滑らかに接続せず、Lost確認後に再開します。
- 長時間遮蔽中の正しい絶対位置は、独立センサーなしには復元できません。

`resources\settings\default.vrsettings`が初期値です。ユーザー設定の
`driver_pose_anchor`セクションで`filterEnabled=false`にすると、次回起動時は
pose hook自体を導入しません。`true`に戻すと補正を有効にできます。
`syntheticWatchdogEnabled=false`でcallback停止時の
自主送信だけを無効化できます。設定変更後はSteamVRを再起動してください。

このドライバはSteamVRの公開されたpose差し替えAPIではなく、MinHookを使うため、
SteamVR更新で動かなくなる可能性があります。OSS表記は同梱の
`THIRD_PARTY_NOTICES.md`を、ソース、設計、テストはソース配布側のREADMEを参照してください。

## English: installation and recovery

PoseAnchor is an experimental Windows x64 SteamVR driver for HTC Vive Trackers in a
Lighthouse setup. It may reduce brief tracking jumps and dropouts. It does not
modify the HMD, controllers, or Base Stations. Start with one tracker in a clear,
safe area. It cannot reconstruct the correct position during a long occlusion.

Close SteamVR and PoseAnchor Status, and disable other tools that replace tracker
poses, including OpenVR Space Calibrator and Motion Compensation. Double-click
`PoseAnchor-Setup-0.1.0.exe` and follow Setup. No commands or administrator rights
are required. Setup detects SteamVR and registers PoseAnchor for the current user;
it does not close SteamVR automatically. For updates, close SteamVR and PoseAnchor
Status again before running the same installer.

After a successful installation, start SteamVR and open **PoseAnchor Status** from
the Start menu. The screen shows a snapshot with its check time. Select **Refresh**
after starting SteamVR or changing settings. There is no background polling, and
closing the window leaves no status process running. The recognized-tracker list
is session history, not the number of trackers connected now. **Hold** means brief
prediction, **Recovery** means blending back to tracking, and **Lost** means a
reliable pose is unavailable. Their counts are minimum transition counts derived
from the available logs: rate-limited events may remain unrecorded even when the
full session log is available. They are not the total number of tracking errors
or a measured improvement rate. Use
**Open log** for the detailed records.

If the status screen reports a problem, follow its next-step guidance. For a missing
or incomplete installation, close SteamVR and PoseAnchor Status before running the
installer again. If Setup reports a registration failure, keep SteamVR closed and
follow the error message. It includes the exact setup-log location. On the failure
completion page, select **Open the setup log** and then **Finish** to inspect it.
If restoring the previous registration failed, repair it before starting SteamVR.
If Setup or the uninstaller says it could not check whether a process is running,
this is a failed safety check, not a detected running app. Follow the restart
guidance; if it persists, report the error or log through the project's Issues page.

To remove PoseAnchor, close SteamVR and PoseAnchor Status, then use **Windows
Settings → Apps → Installed apps → PoseAnchor → Uninstall**, or **Uninstall
PoseAnchor** in the Start menu. The included uninstaller unregisters the driver
before removing its files.

By default, brief prediction lasts at most 150 ms and is limited to 25 cm / 45°.
After loss of tracking, a stable raw pose must be observed for 80 ms before it
resumes.

The initial settings are in `resources\settings\default.vrsettings`. In the
`driver_pose_anchor` section of SteamVR's user settings, `filterEnabled=false`
disables the pose hook on the next launch; set it to `true` to enable filtering.
`syntheticWatchdogEnabled=false` disables only the synthetic updates sent when
pose callbacks stop. Settings changes take effect after restarting SteamVR.

PoseAnchor uses MinHook rather than a public pose-replacement API, so SteamVR updates may affect
compatibility. See `THIRD_PARTY_NOTICES.md` for the open-source notices.
