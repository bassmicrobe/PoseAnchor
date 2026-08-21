# PoseAnchor

SteamVR / Lighthouse環境の**Vive Trackerだけ**を対象に、瞬間的な異常poseを隠し、
正常トラッキングへ連続的に戻す実験的なWindows x64サーバードライバです。
Base Station 2〜4台の構成を想定しています。HMD、コントローラー、Base Stationの
poseには触れません。

## 現在のMVP

- SteamVR起動時に`vrserver.exe`内へ自動ロード
- `GenericTracker`かつHTC Vive Tracker / Lighthouseと確認できた機器だけを処理
- 正常時は元の`DriverPose_t`を無加工で通すため、固定平滑化遅延なし
- 起動・時間断絶後は8個の整合したposeを確認してから有効化
- 位置・回転innovationで1〜数フレームのpose外れ値を拒否
- poseが連続しているのに報告速度だけ不整合な場合は、姿勢差分由来の速度へ修復して動きを継続
- 欠測中は最大150 ms、25 cm / 45°まで速度を減衰させて予測
- Lighthouseのpose callback自体が止まった場合もwatchdogから同じ制限で補間
- 同じ軌道へ戻れば即復帰、別位置で安定した場合は120〜350 msでC1連続補正
- 150 msを超えて信頼できない場合はOut-of-Rangeにして、誤った位置を出し続けない
- 切断時は補間せず即座に無効化
- OpenVRに依存しないフィルタ本体と合成テスト

状態遷移は`Tracking → Hold → Recovering → Tracking`です。短時間に再捕捉できなければ
`Lost`へ移り、安定したraw poseを80 ms確認してから再開します。

## 重要な制約

これはSteamVRの公開された「既存デバイスpose差し替えAPI」ではなく、MITライセンスの
[OpenVR Space Calibrator](https://github.com/hyblocker/OpenVR-SpaceCalibrator)で実績のある
MinHook方式を使います。SteamVR更新で壊れる可能性があり、初版ではSpace Calibrator、
OpenVR Motion Compensationなど、同じpose関数をフックするドライバとの同時利用は非対応です。

独立したカメラやセンサーがないため、長い遮蔽中の絶対位置は復元できません。また、
SteamVRから「現在どのBase Stationを何台見ているか」はpose callbackに渡らないため、
2台と4台でアルゴリズムを切り替えることはしません。callback停止時のwatchdogは、最後に
確認したhostとdevice indexへMinHookのoriginal trampoline経由で合成poseを再送します。
これも公開APIではないため、問題があれば`syntheticWatchdogEnabled=false`で無効化できます。
OpenVR Driver API上、更新停止時のSteamVR標準外挿は最大100 msです。

Hold軌道から0.75 m / 90°を超える位置で再捕捉した場合は、誤った場所へ滑らかに吸着するのを
避けるためRecoveryへ入りません。150 msでLostにした後、80 msの整合確認を経てraw poseへ
再開するため、その境界では位置が変わる可能性があります。

実機投入前にSteamVR設定とルームセットアップをバックアップしてください。最初は腰など、
転倒や衝突につながりにくいTracker 1個で確認してください。

## ビルド

必要なものはVisual Studio 2022の「C++によるデスクトップ開発」、CMake、Windows SDK、Gitです。
不足している場合は管理者PowerShellで次を実行します。

```powershell
.\scripts\install-build-tools.ps1
```

その後、通常のPowerShellで:

```powershell
.\scripts\build.ps1
```

CMakeはOpenVR v2.15.6とMinHook v1.3.4をGitのcommit hashで固定取得します。
成功すると`build\pose_anchor\`にSteamVRへ登録できる配布物が生成されます。

配布用のGUIインストーラーを作る場合は、ビルドPCに
[Inno Setup 6](https://jrsoftware.org/isinfo.php)も入れて次を実行します。

```powershell
.\scripts\build-installer.ps1
```

`dist\PoseAnchor-Setup-0.1.0.exe`が生成されます。利用者側にVisual Studio、CMake、
Inno Setup、PowerShell操作は不要です。

### 現在の検証状況

- フィルタの合成回帰テスト、警告をエラー扱いしたビルド、UndefinedBehaviorSanitizerは通過済み
- OpenVR / MinHookの固定ヘッダーに対するWindows x64向け全ドライバ翻訳単位のコンパイルは通過済み
- このソース配布には未検証の代替ABI製DLLを含めていません
- Visual Studio 2022/MSVCによる最終リンク、DLL構造検査、SteamVRへのロードは通過済み
- VIVE Tracker 3.0の認識、非identity DriverFromHead、静止時の連続動作は実機確認済み
- 旧版で108件の誤状態遷移が出た35秒の手動移動・回転を再試験し、Hold/Recovery/Lostゼロ
- 物理遮蔽では150 msまでHoldした後にLostへ移り、安定pose確認後にTrackingへ復帰
- GUIインストール、同一バージョン更新、登録path移行、アンインストール、登録消去を実機確認済み

SteamVRへ登録するのは、必ず上記手順でMSVCビルドした`build\pose_anchor\`だけにしてください。

## 配布版のインストール / 解除

SteamVRを終了し、`PoseAnchor-Setup-0.1.0.exe`をダブルクリックします。セットアップが
SteamVRの場所を検出し、ファイル配置、既存の同名登録確認、`vrpathreg`によるドライバ登録を
自動で行います。既定ではユーザーごとの`%LOCALAPPDATA%\Programs\PoseAnchor`へ入り、
管理者権限やコマンド操作は不要です。

解除はWindowsの「設定 → アプリ → インストールされているアプリ → PoseAnchor」または
スタートメニューの「Uninstall PoseAnchor」から行えます。同梱アンインストーラーが
SteamVR登録解除後にファイルを削除します。

アップグレードやアンインストール時にDLLが使用中なら、インストーラーの案内に従って
SteamVRを終了してください。

### 開発ビルドの手動登録

ソースツリーから実機検証する開発者だけ、SteamVRを完全終了して次のスクリプトを使えます。

```powershell
.\scripts\install.ps1
# 解除
.\scripts\uninstall.ps1
```

ビルドツリーを削除・移動した後でも`uninstall.ps1`は古い登録を解除できます。登録パスが
現在のパスと一致しない場合は`.\scripts\uninstall.ps1 -AllWithName`で`pose_anchor`名の
登録をすべて削除してください。

`vrpathreg adddriver`で外部ドライバとして登録するだけで、SteamVRのインストールフォルダへ
ファイルはコピーしません。SteamVR起動後、`vrserver.txt`で`[PoseAnchor]`を検索すると
ロード、Tracker識別、Hold/Recovery/Lostイベントを確認できます。

## CPU・メモリ方針

- 通常pose経路は固定長POD状態だけを更新し、ヒープ確保、ファイルI/O、常時ログを行いません。
- フィルタはVive Trackerと確定したデバイスにだけ遅延生成し、最大64デバイス分を先に確保しません。
- pose転送先が変わらない通常時はatomic fast pathを使い、経路mutexを取りません。
- 詳細診断は状態遷移時だけ生成し、同種イベントを1秒単位で抑制します。
- `RunFrame`のwatchdogは固定64スロットを走査しますが、Tracker以外はatomic判定だけで終了します。
- リアルタイム経路にAIランタイム、GPU処理、常駐UI、ネットワーク通信は入れません。

MSVC Releaseの手元計測では、1 ms周期の連続運動を100万sample処理したコア部分は
5回の中央値105.50 ns/sample（105.28〜107.23 ns/sample、入力sampleは事前生成し
`PoseFilter::push`だけを計測）、`PoseFilter`は1 Trackerあたり1696 bytesでした。
1 kHz入力でもコア計算はTracker 1台あたり単一CPUコアの約0.011%相当、
4台で約0.042%相当です。再計測は`build\Release\pose_anchor_benchmark.exe`で行えます。

2026-08-21にはRyzen 9 9950X3D / Windows 11 / SteamVR build 23791826 / VIVE Tracker 3.0で、
Trackerを静止させて`PoseAnchorあり → なし → あり`を各30秒×3回測定しました。

| 状態 | `vrserver` CPU（論理1コア比） | PC全体換算（32 threads） | Working set | Private memory | 補正状態遷移 |
| --- | ---: | ---: | ---: | ---: | ---: |
| あり（前） | 16.0065% | 0.50020% | 178.430 MiB | 277.984 MiB | 0 |
| なし | 18.9818% | 0.59431% | 178.520 MiB | 278.672 MiB | - |
| あり（後） | 16.0141% | 0.50044% | 178.012 MiB | 278.000 MiB | 0 |

これは`vrserver.exe`全体の値で、PoseAnchorだけを分離した値ではありません。あり側が約3ポイント
低い結果はPoseAnchorがSteamVRを高速化した意味ではなく、SteamVRセッション間の変動がドライバの
負荷より大きいことを示します。この測定ではPoseAnchorによるCPU・メモリ増加は検出できませんでした。
正確なコア処理費用には、上の決定論的マイクロベンチマークを使います。

SteamVRのドライバABIとMinHookがC/C++境界なので、ドライバ本体をRustへ置き換えてもunsafe FFIと
C++ shimが残り、現在の固定長C++実装より軽くはなりません。将来のログ解析や設定UIを別プロセスで
追加する場合は、クラッシュ分離と単一バイナリ化に利点があればRustを候補にします。

## 初期設定

既定値は`driver/pose_anchor/resources/settings/default.vrsettings`です。調整する場合は
SteamVRのユーザー設定に`driver_pose_anchor`セクションを追加します。最初に触る候補は:

- `basePositionGateMeters`: 既定0.025 m。誤検知があれば少し上げる
- `baseRotationGateDegrees`: 既定8°。高速回転で誤検知があれば上げる
- `velocityConsistencyPositionMeters` / `velocityConsistencyRotationDegrees`: poseと報告速度の整合幅
- `holdSeconds`: 既定0.150秒。安全上0.2秒以上は推奨しない
- `maxHoldTranslationMeters` / `maxHoldRotationDegrees`: 合成移動の絶対上限（既定25 cm / 45°）
- `syntheticWatchdogEnabled`: `false`でcallback停止時の自主送信だけを無効化
- `filterEnabled`: `false`で次回起動時にpose hook自体を導入しない

変更はSteamVR再起動後に反映されます。

## AIについて

リアルタイムpose経路へLLMや重い推論は入れていません。まず決定論的な安全フィルタと
イベントログを使い、実機で「正常な高速運動」と「飛び」のデータを集める方が有効です。
次段階ではログから小型異常検知モデルをオフライン学習し、Trackerごとの閾値提案や
誤検知分類に使えます。推論を追加する場合も別プロセスに置き、失敗時はこの決定論的経路へ
戻す設計にします。

## 参考OSS / 仕様

- [ValveSoftware/openvr](https://github.com/ValveSoftware/openvr)
- [OpenVR Space Calibrator](https://github.com/hyblocker/OpenVR-SpaceCalibrator)
- [MinHook](https://github.com/TsudaKageyu/minhook)
- [OpenVR Driver API documentation](https://github.com/ValveSoftware/openvr/blob/master/docs/Driver_API_Documentation.md)

ライセンスはMITです。依存関係と参考実装の表記は`THIRD_PARTY_NOTICES.md`を参照してください。
