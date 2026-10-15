# 公開・再配布前チェックリスト

利用者向けの手順は[簡易マニュアル](../MANUAL.md)、ライセンス一覧は
[README](../README.md#ライセンスと再配布)を参照してください。
このチェックリストは公開安全性やライセンス適合性を自動保証するものではありません。

## リポジトリと履歴

- 現行ファイルだけでなく、公開する全ブランチ・タグの過去コミットと削除済みファイルも確認する。
- 作者・コミッターのメール、共同作者の記載、コミットメッセージも公開されることを確認する。
  個人メールを出したくない場合は、GitHubのメール設定で表示される自分のnoreplyアドレスを使う。
  Git設定の変更や`.mailmap`の追加だけでは、過去コミットの生データは消えない。
- `.env`、APIキー、秘密鍵、署名用PFX/P12、Windowsユーザー名を含むローカルパス、
  実機シリアル、設定ファイル、ダンプ・ログを含めない。テストは架空の値だけを使う。
- `.gitignore`は誤追加を防ぐ補助であり、追跡済みファイルや履歴を消す仕組みではない。
  `git diff --cached`でコミット対象を確認し、利用可能ならGitHubのsecret scanning / push protectionも確認する。
- GitHubのIssue・PR・Actionsの実行メタデータ/ログ/成果物・Release添付・Wiki・forkは
  Git作業ツリーとは別に確認する。過去のActions実行にも当時のコミット作者情報が残る場合がある。

認証情報が見つかった場合は、削除より先に失効・再発行する。履歴の書き換えではコミットIDが
変わり、他のcloneとの調整や強制pushが必要になるため、所有者・共同作業者と合意して行う。
公開済みデータは、書き換えても他者のclone・fork・キャッシュから回収できるとは限らない。
詳細は[GitHubの削除手順](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/removing-sensitive-data-from-a-repository)と
[コミットメール設定](https://docs.github.com/en/account-and-profile/how-tos/email-preferences/setting-your-commit-email-address)を参照。

## 配布物

- 実験版はGitHubのPre-releaseとして扱い、紹介文にも効果未実証・誤補正の可能性・
  実機未検証の範囲を明記する。安定版や効果保証のある製品と表現しない。
- 検証済みの構成から再ビルドする。古い`dist/`内の候補版を混ぜない。
- ローカルの`dist/`には公開候補のSetup EXEと同名のSHA-256ファイル1組だけを置く。
  旧候補やログはリポジトリ外の非公開バックアップへ退避する。削除や上書きの前に対象を確認する。
- `MANUAL.md`、`README.md`、`LICENSE`、`THIRD_PARTY_NOTICES.md`、`licenses/`が揃っていることを確認する。
- テストEXE、PDB、ビルドキャッシュ、個人設定、実機ログ、秘密鍵が入っていないことを確認する。
  同梱DLL/EXEの文字列にも、ローカルの個人パスなどが埋め込まれていないか確認する。
- Authenticode署名を行う場合、秘密鍵をGitや配布物に入れない。証明書の公開情報として
  署名者名等が表示される点も把握する。
- インストーラーと同名のSHA-256ファイルを生成し、配布する実ファイルと一致することを確認する。
- 別環境で新規導入・更新・解除、SteamVR起動終了、実Trackerでの動作を確認する。
  自動テストやマイクロベンチマークだけでトラッキング改善を保証しない。
  未実施の場合は試験済みと表記せず、制限を明示したテスター向けの実験版にとどめる。
- 実機ON/OFF比較は同じ動作・環境で繰り返し、飛び・誤補正・復帰時間・追加遅延を評価する。
  真の位置精度を評価するには独立した基準が必要。ログの状態遷移数を改善率に置き換えない。

```powershell
.\scripts\build.ps1 -Configuration Release
.\tests\package_staging_tests.ps1
.\scripts\build-installer.ps1 -SkipBuild
```

インストーラーは公開・アップロードするまでローカルの生成物です。公開設定や配布先の変更は
ソース修正・ビルドとは別に、対象を確認して行ってください。
