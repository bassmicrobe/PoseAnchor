# PoseAnchor 0.1.0 — experimental build

SteamVR / Lighthouse環境のHTC Vive Trackerだけを対象に、短いtracking jumpや
Out-of-Rangeを抑えるWindows x64サーバードライバです。HMD、コントローラー、
Base Stationには触れません。

## インストール

これは実験版です。最初は転倒や衝突につながりにくいTracker 1個で試してください。
SteamVR、OpenVR Space Calibrator、Motion Compensationなど同じpose関数を変更する
ツールをすべて終了／無効化してから、配布された`PoseAnchor-Setup-0.1.0.exe`を
ダブルクリックします。セットアップがSteamVRを検出し、ドライバ登録まで自動で行います。
コマンド操作や管理者権限は不要です。

SteamVRが実行中の場合、セットアップはファイルや登録を変更せずに終了を案内します。
SteamVRを自動終了することはありません。完了画面の案内に従い、必要なら同梱READMEを開いて
からSteamVRを起動してください。

SteamVRを起動し、スタートメニューの「PoseAnchor ステータス」で登録、driver/hook読込、
Tracker識別、Hold/Recovery/Lostを確認します。この画面は必要なときだけ起動し、閉じると
プロセスは残りません。詳しい記録は画面の「ログを開く」から確認できます。
不具合があればSteamVRを終了し、Windowsの「設定 → アプリ → インストールされている
アプリ → PoseAnchor」またはスタートメニューの「PoseAnchor をアンインストールする」を選びます。
同梱アンインストーラーがSteamVR登録解除後にファイルを削除します。

SteamVR本体へファイルはコピーせず、ユーザーごとのPoseAnchorフォルダをexternal driverとして
登録します。アップグレード時も同じインストーラーを実行できます。

## 挙動と限界

- 起動／再捕捉時は8個の整合したposeを確認するまでTrackerを一時的に無効化します。
- 正常時は元poseを無加工で通し、固定平滑化遅延は加えません。
- poseが連続していて報告速度だけ不整合な場合は、poseを止めず速度成分だけを修復します。
- 異常時は最大150 ms、かつ最大25 cm / 45°まで減衰予測します。
- pose callback停止時もwatchdogが同じ上限内で合成poseを送ります。
- 150 msを超えるとOut-of-Rangeにし、安定したraw poseを80 ms確認して再開します。
- Hold軌道から0.75 m / 90°を超える再捕捉は滑らかに接続せず、Lost確認後に再開します。
- 長時間遮蔽中の正しい絶対位置は、独立センサーなしには復元できません。

`resources\settings\default.vrsettings`が初期値です。ユーザー設定の
`driver_pose_anchor`セクションで`filterEnabled=false`にすると、次回起動時は
pose hook自体を導入しません。`syntheticWatchdogEnabled=false`でcallback停止時の
自主送信だけを無効化できます。

このドライバはSteamVRの公開されたpose差し替えAPIではなく、MinHookを使うため、
SteamVR更新で動かなくなる可能性があります。OSS表記は同梱の
`THIRD_PARTY_NOTICES.md`を、ソース、設計、テストはソース配布側のREADMEを参照してください。
