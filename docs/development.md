# ビルド・配布・検証

ソースREADMEから移した、ビルド手順・検証状況・性能方針の詳細です。
配布版の導入は[簡易マニュアル](../MANUAL.md)、フィルタの挙動は[挙動と設定](behavior.md)を参照してください。

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
リンク時の出力は`build\<構成>\pose_anchor\`に分離し、選択した構成を毎回
`build\pose_anchor\`へ配置します。インストーラービルドは`build-info.json`の
バージョンと構成を検査し、`-SkipBuild`でもDebug版などの取り違えを拒否します。

配布用のGUIインストーラーを作る場合は、ビルドPCに
[Inno Setup 6](https://jrsoftware.org/isinfo.php)も入れて次を実行します。

```powershell
.\scripts\build-installer.ps1
```

`dist\PoseAnchor-Setup-0.1.2.exe`と同名の`.exe.sha256`が生成されます。利用者側にVisual Studio、CMake、
Inno Setup、PowerShell操作は不要です。

配布対象はこの実験版インストーラーとチェックサムの1組だけです。旧候補・テストログ・
`build/`全体は公開しないでください。配布用のREADME・マニュアル・OSSライセンス・
アンインストーラーはSetupに含まれます。GitHubで公開する際はPre-releaseにし、
[配布準備記録と未検証項目](experimental-release-2026-10-15.md)も確認してください。

上記は未署名ビルドです。署名なしでも使用・配布できますが、Windowsの警告や端末の
実行ポリシーにより起動が制限される場合があります。署名する場合はコード署名証明書の
SHA-1 thumbprintを指定します。driver DLLと状態画面を先に署名し、Inno SetupがSetup本体と同梱
アンインストーラーを署名してから最終チェックサムを生成します。

```powershell
.\scripts\build-installer.ps1 -CertificateThumbprint '<40桁のthumbprint>'
```

SteamVRへ登録するのは、必ず上記手順でMSVCビルドした`build\pose_anchor\`だけにしてください。

## 開発ビルドの手動登録

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

## 現在の検証状況

0.1.2のロゴ更新と検証範囲は[ロゴ更新記録](branding-update-2026-10-25.md)、
0.1.1の配布準備と実機確認の制限は
[実験版の配布準備記録](experimental-release-2026-10-15.md)に記載しています。
直近の性能・UI監査は[2026-10-15の監査記録](performance-ui-audit-2026-10-15.md)、
過去のリリース監査は[2026-09-26の記録](release-audit-2026-09-26.md)を参照してください。
以下の実機測定・インストール試験は過去版での結果で、今回のバイナリの実機再試験を代替しません。

- GitHub Actions CIはpush毎にMSVCビルド+全テスト、構成切替時の配布物検査、
  インストーラー生成と、コアフィルタ・状態パーサーのclang ASan/UBSanビルド+テストを実行する設定
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

## CPU・メモリ方針

- 通常pose経路は固定長POD状態だけを更新し、ヒープ確保、ファイルI/O、常時ログを行いません。
- フィルタはVive Trackerと確定したデバイスにだけ遅延生成し、最大64デバイス分を先に確保しません。
- 対象外デバイスはフィルタや経路mutexを通しません。対象Trackerはデバイス単位で
  通常poseとwatchdog転送を直列化し、古い補間結果が新しいposeを上書きすることを防ぎます。
- 詳細診断は状態遷移時だけ生成し、同種イベントを1秒単位で抑制します。
- 分類待ちは64個のatomicを毎frame更新せず、単一bit maskが空なら即終了します。
- `RunFrame`のwatchdogは有効時だけ固定64スロットを走査し、Tracker以外はatomic判定だけで終了します。
- リアルタイム経路にAIランタイム、GPU処理、常駐UI、ネットワーク通信は入れません。

以下は過去版で行った参考計測です。2026-10-15の変更前後の計測は
[性能・UI監査記録](performance-ui-audit-2026-10-15.md)を参照してください。
これらは0.1.2の実機ON/OFF比較や、トラッキング改善を示す計測ではありません。

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
