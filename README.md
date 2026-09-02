# PoseAnchor

[![CI](https://github.com/bassmicrobe/PoseAnchor/actions/workflows/ci.yml/badge.svg)](https://github.com/bassmicrobe/PoseAnchor/actions/workflows/ci.yml)

SteamVR / Lighthouse環境の**Vive Trackerだけ**を対象に、瞬間的な異常poseを隠し、
正常トラッキングへ連続的に戻す実験的なWindows x64サーバードライバです。
Base Station 2〜4台の構成を想定しています。HMD、コントローラー、Base Stationの
poseには触れません。

## 機能概要

PoseAnchorは`vrserver.exe`内にロードされるフィルタ層です。Lighthouseドライバが
SteamVRへ報告する`DriverPose_t`をMinHookで横取りし、Vive Trackerのものだけを
検査・補正してから本来の受け口へ渡します。SteamVR起動時に自動ロードされ、
利用者側の常駐アプリや操作は不要です。

### pose処理パイプライン

1. **識別** — `GenericTracker`かつHTC Vive Tracker / Lighthouse構成と確認できた
   デバイスだけを処理対象にし、それ以外（HMD、コントローラー、SlimeVR等の
   他社Tracker）は無加工で素通しします
2. **検査** — 位置・回転innovation、速度整合、ハード速度上限の8ゲートで毎poseを
   判定し、1〜数フレームの外れ値を拒否します
3. **修復** — poseが連続しているのに報告速度だけ異常な場合は、姿勢差分由来の
   速度へ置換して動きを継続します（修復で速度を増やすことはしません）
4. **補間** — 異常・欠測中は最大150 ms、25 cm / 45°を上限に速度を減衰させて
   予測します。Lighthouseのpose callback自体が停止した場合もwatchdogが同じ制限で
   補間します
5. **復帰** — 元の軌道へ戻れば即復帰、別位置で安定した場合は120〜350 msの
   C1連続補正で滑らかに吸着します
6. **放棄** — 150 msを超えて信頼できない場合はOut-of-Rangeを報告し、誤った位置を
   出し続けません。切断時は補間せず即座に無効化します

### 状態遷移

| 状態 | 意味 | 出力 |
| --- | --- | --- |
| Cold | 起動・時間断絶後、8個の整合poseを確認するまで | 無効pose |
| Tracking | 正常。元poseを無加工で通す（固定平滑化遅延なし） | raw |
| Hold | 異常・欠測を減衰予測で隠蔽（最大150 ms） | 合成pose |
| Recovering | 検証済みの新しい軌道へC1連続で補正中 | raw + 補正 |
| Lost | 信頼できない。安定raw poseを80 ms確認して再開 | 無効pose |

### 主要コンポーネント

- `src/core/tracker_filter.*` — OpenVR非依存の決定論的フィルタ本体。
  単体テスト・サニタイザ・マイクロベンチマークの対象
- `src/driver/hooks.*` — MinHookによるpose callbackフック
  （`IVRServerDriverHost` 005/006両対応、二相ランダウンで安全に解除）
- `src/driver/pose_adapter.*` — `DriverPose_t`とワールド座標系の相互変換。
  `poseTimeOffset`を含む運動時刻とsteady clock到着時刻を分離して扱う
- `src/driver/device_registry.*` — Vive Tracker識別。プロパティの遅延到着に
  fail-openで対応
- `src/driver/server_provider.*` — 設定読込、デバイス毎のフィルタ管理、
  callback停止watchdog、レート制限付き診断ログ
- `installer/` + `scripts/` — Inno Setup製GUIインストーラー（日英対応、
  SteamVR実行中ガード付き）とPowerShell開発スクリプト

### 安全設計

フィルタ例外、未識別デバイス、不正なtransformなど判断できない状況では、必ず
元のposeを素通しします（fail-open）。補正はすべて絶対上限付きで、修復・補間が
実際の動きより大きな動きを合成することはありません。

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

上記はローカル検証用の未署名ビルドです。公開配布ではコード署名証明書のSHA-1 thumbprintを
指定します。driver DLLと状態画面を先に署名し、Inno SetupがSetup本体と同梱
アンインストーラーを署名してから最終チェックサムを生成します。

```powershell
.\scripts\build-installer.ps1 -CertificateThumbprint '<40桁のthumbprint>'
```

### 現在の検証状況

- GitHub Actions CIがpush毎にMSVCビルド+全テストと、コアフィルタのclang
  ASan/UBSanビルド+テストを実行
- フィルタの合成回帰テスト、警告をエラー扱いしたビルド、UndefinedBehaviorSanitizerは通過済み
- OpenVR / MinHookの固定ヘッダーに対するWindows x64向け全ドライバ翻訳単位のコンパイルは通過済み
- このソース配布には未検証の代替ABI製DLLを含めていません
- Visual Studio 2022/MSVCによる最終リンク、DLL構造検査、SteamVRへのロードは通過済み
- VIVE Tracker 3.0の認識、非identity DriverFromHead、静止時の連続動作は実機確認済み
- 旧版で108件の誤状態遷移が出た35秒の手動移動・回転を再試験し、Hold/Recovery/Lostゼロ
- 物理遮蔽では150 msまでHoldした後にLostへ移り、安定pose確認後にTrackingへ復帰
- GUIインストール、同一バージョン更新、登録path移行、アンインストール、登録消去を実機確認済み
- オンデマンドの状態画面で登録、現SteamVRセッションのdriver/hook、Tracker、
  Hold/Recovery/Lostを確認でき、古いセッションを有効状態として表示しないことを確認済み

SteamVRへ登録するのは、必ず上記手順でMSVCビルドした`build\pose_anchor\`だけにしてください。

## 配布版のインストール / 解除

SteamVRを終了し、`PoseAnchor-Setup-0.1.0.exe`をダブルクリックします。セットアップが
SteamVRの場所を検出し、ファイル配置、既存の同名登録確認、`vrpathreg`によるドライバ登録を
自動で行います。既定ではユーザーごとの`%LOCALAPPDATA%\Programs\PoseAnchor`へ入り、
管理者権限やコマンド操作は不要です。

解除はWindowsの「設定 → アプリ → インストールされているアプリ → PoseAnchor」または
スタートメニューの「PoseAnchor をアンインストールする」から行えます。同梱アンインストーラーが
SteamVR登録解除後にファイルを削除します。

アップグレードやアンインストール時にSteamVRが実行中なら、ファイルや登録を変更せずに
終了を案内します。インストーラーがSteamVRを自動終了することはありません。

インストール後はスタートメニューの「PoseAnchor ステータス」で、登録状態、SteamVRの
実行状態、現セッションでのdriver/hook読込、認識したVive Tracker、直近の
Hold/Recovery/Lostを確認できます。この画面は必要なときだけ起動し、閉じるとプロセスは
残りません。判定には実行中のSteamVRセッションだけを使い、以前のログを稼働中の証拠には
しません。

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
- 分類待ちは64個のatomicを毎frame更新せず、単一bit maskが空なら即終了します。
- `RunFrame`のwatchdogは有効時だけ固定64スロットを走査し、Tracker以外はatomic判定だけで終了します。
- リアルタイム経路にAIランタイム、GPU処理、常駐UI、ネットワーク通信は入れません。

オンデマンド状態画面は20回の実機計測で起動中央値75.25 ms、表示中のPrivateメモリ中央値
3.01 MiB（Working set 16.86 MiB）でした。全20回で画面を閉じた後の残存プロセスは0です。

MSVC Releaseの手元計測（入力sampleは事前生成し`PoseFilter::push`だけを計測、
各フェーズ100万sample×7回の中央値）では、1 ms周期の連続運動が
94.30 ns/sample（93.96〜95.09）、外れ値・Hold・Recoveryを繰り返す外乱系列でも
125.55 ns/sample（125.48〜127.03）、`PoseFilter`は1 Trackerあたり1696 bytesでした。
同じMSVC ABIでdriverの固定管理状態は`ServerProvider` 43,352 bytes + hook 6,776 bytes
（合計約49 KiB）で、ここに識別済みTrackerごとのfilterが加わります。
1 kHz入力でもコア計算はTracker 1台あたり単一CPUコアの約0.009%相当（外乱時でも
約0.013%）、4台で約0.038%相当（外乱時約0.050%）です。通常・外乱の計測区間では
ヒープ確保0件も検証します。

同じ計測で、分類待ちなしの処理は1.66 ns/call（1.65〜1.66）、watchdogの
64-slot走査は51.10 ns/frame（50.97〜53.91、90 Hzで約4.6 µs/秒）でした。
また、非対象デバイス経路から削除した280-byte `DriverPose_t`コピー単体は
2.84 ns/call（2.84〜2.86）でした。残した安全機構の単体費用はcallback rundownの
atomic加減算pairが7.08 ns、Tracker mutexの非競合lock/unlockが11.03 ns、
`steady_clock::now`が15.38 nsでした。再構成・Releaseビルド・7回のmedian/min/max集計は
次の1コマンドで再現できます（共有CIでは時間値を合否判定に使いません）。

```powershell
.\scripts\benchmark.ps1
```

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
