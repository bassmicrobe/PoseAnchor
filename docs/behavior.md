# フィルタの挙動と設定

ソースREADMEから移した、pose処理・制約・設定・診断の詳細です。
利用者向けの短い手順は[簡易マニュアル](../MANUAL.md)を参照してください。

## pose処理パイプライン

PoseAnchorは`vrserver.exe`内にロードされるフィルタ層です。Lighthouseドライバが
SteamVRへ報告する`DriverPose_t`をMinHookで横取りし、Vive Trackerのものだけを
検査・補正してから本来の受け口へ渡します。SteamVR起動時に自動ロードされ、
利用者側の常駐アプリや操作は不要です。

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

## 状態遷移

| 状態 | 意味 | 出力 |
| --- | --- | --- |
| Cold | 起動・時間断絶後、8個の整合poseを確認するまで | 無効pose |
| Tracking | 正常。元poseを無加工で通す（固定平滑化遅延なし） | raw |
| Hold | 異常・欠測を減衰予測で隠蔽（最大150 ms） | 合成pose |
| Recovering | 検証済みの新しい軌道へC1連続で補正中 | raw + 補正 |
| Lost | 信頼できない。安定raw poseを80 ms確認して再開 | 無効pose |

## 主要コンポーネント

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

## 安全設計

フィルタ例外、未識別デバイス、不正なtransformなど判断できない状況では、必ず
元のposeを素通しします（fail-open）。欠測時の予測には時間・移動量・回転量の上限が
あります。ただし独立した位置基準がないため、予測が実際の動きと一致する保証はありません。

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

## プライバシーと問題報告

現在のドライバー・状態画面に、独自のテレメトリ送信・クラウドアップロード機能はありません。
診断はローカルのSteamVRログや登録情報を利用します（SteamVR自身の通信は別です）。
ソースからのビルドでは依存物の取得、署名を指定したビルドではタイムスタンプ取得のために通信します。

ログやステータス画面の詳細には、Trackerのシリアル番号、Windowsユーザー名を含むパス、
他のVRアプリ・ドライバー情報が含まれる場合があります。公開Issueにはログ全体や画面全体を
そのまま添付せず、必要な部分だけを取り出し、ユーザー名・パス・シリアル等を伏せてください。
認証情報、秘密鍵、署名用証明書の秘密鍵、`.env`は投稿・コミットしないでください。

開発者向けの公開前チェックは[公開準備チェックリスト](public-release-checklist.md)を参照してください。
