encfsy
======

🌐 **言語**: [English](README.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [Русский](README.ru.md) | [العربية](README.ar.md) | [Deutsch](README.de.md)

---

## encfsy について
encfsy は **Dokany** と **Crypto++** をバックエンドに採用した Windows 向けの EncFS 実装です。**64 bit 環境専用**。

ディレクトリ・ファイルのツリー構造はそのままに、ファイル名と内容を暗号化します。ドライブ全体を暗号化するのに比べて、Dropbox や Google Drive あるいは rsync でリモートのドライブと同期するのに有用です。暗号化された状態でリモートとファイルを共有するため、ドライブの管理者にファイルの中身を見られることがありません。

## セキュリティ機能
encfsy は **Windows Credential Manager** を使用してパスワードを安全に管理します。

- パスワードは **DPAPI** (Data Protection API) で暗号化され、現在のユーザーアカウントに紐付けられます
- GUI と encfs.exe 間でパスワードを標準入力で渡す必要がなく、プロセス間通信での傍受リスクを排除
- 「パスワードを記憶する」オプションでパスワードを保存、次回起動時に自動入力
- パスワードは**暗号化ディレクトリ（rootDir）ごとに個別に保存**されます

### パスワードの保存場所
保存されたパスワードは、コントロールパネル → 資格情報マネージャー → Windows 資格情報 で確認できます。
`EncFSy:c:\path\to\encrypted` のような名前で表示されます。

## GUI の使い方
**EncFSy_gui.exe** を使用すると、コマンドラインを使わずに簡単にボリュームをマウント・アンマウントできます。

1. 暗号化ディレクトリ (rootDir) を選択
2. マウント先のドライブ文字を選択
3. パスワードを入力（「Remember Password」で記憶可能）
4. 「Mount」をクリック

「Show Advanced Options」でコマンドライン版と同等の詳細設定も可能です。

## コマンドラインでの Credential Manager 利用
GUI で「Remember Password」をチェックしてマウントすると、パスワードが Windows Credential Manager に保存されます。
その後、コマンドラインから `--use-credential` オプションでパスワード入力なしにマウントできます。

```bash
# 1. まず GUI で「Remember Password」をチェックしてマウント
#    → パスワードが Credential Manager に保存される

# 2. 次回以降、コマンドラインからパスワード入力なしでマウント可能
encfs.exe C:\Data M: --use-credential
```

## ファイル名の長さ制限
encfsy は新しい「ロングパス」API を利用しているため、フルパス全体の 260 文字（MAX_PATH）制限は適用されません。

しかし NTFS では各パス要素（フォルダー名・ファイル名）に **255 UTF‑16 文字**の上限があります。暗号化により名前はおよそ 3 割程度長くなるため、ロングパス非対応のツールとの互換性を保つには **各ファイル名を 175 文字以内**に抑えてください。

## 使い方

```
Usage: encfs.exe [options] <rootDir> <mountPoint>

引数:
  rootDir      (例: C:\test)                暗号化してマウントするディレクトリ
  mountPoint   (例: M: または C:\mount\dokan)  ドライブ文字または空の NTFS フォルダー

オプション:
  -u <mountPoint>                              指定したボリュームをアンマウント
  -l                                           マウント中の Dokan ボリューム一覧を表示
  -v                                           デバッグ出力をデバッガへ送信
  -s                                           デバッグ出力を stderr へ送信
  -i <ms>              (既定: 120000)          操作タイムアウト（ミリ秒）経過で自動アンマウント
  --use-credential                             Windows Credential Manager からパスワードを取得
                                               （パスワードは保持される）
                                               ※事前に GUI で「Remember Password」をチェックして
                                               パスワードを保存しておく必要があります
  --use-credential-once                        Windows Credential Manager からパスワードを取得
                                               （読み取り後に削除、一回限りの使用）
  --dokan-debug                                Dokan のデバッグ出力を有効化
  --dokan-network <UNC>                        ネットワークボリュームの UNC パス (例: \\host\myfs)
  --dokan-removable                            リムーバブルメディアとしてマウント
  --dokan-write-protect                        読み取り専用でマウント
  --dokan-mount-manager                        Windows Mount Manager に登録（ごみ箱などを有効化）
  --dokan-current-session                      現在のセッションのみにボリュームを公開
  --dokan-filelock-user-mode                   LockFile/UnlockFile をユーザーモードで処理
  --dokan-enable-unmount-network-drive         エクスプローラーからネットワークドライブをアンマウント可能にする
  --dokan-dispatch-driver-logs                 カーネルドライバーのログをユーザーランドに転送（低速）
  --dokan-allow-ipc-batching                   低速なファイルシステム向けに IPC バッチ処理を有効化
  --public                                     CreateFile 時に呼び出しユーザーを偽装（管理者権限要）
  --allocation-unit-size <bytes>               ボリュームのアロケーションユニットサイズを指定
  --sector-size <bytes>                        セクタサイズを指定
  --volume-name <name>                         エクスプローラーに表示するボリューム名（既定: EncFS）
  --volume-serial <hex>                        ボリュームシリアル番号（16進数、既定: 下層から取得）
  --paranoia                                   AES-256、リネーム IV、外部 IV チェーンを有効化
  --alt-stream                                 NTFS 代替データストリームを有効化
  --case-insensitive                           大文字小文字を区別しない名前照合
  --reverse                                    逆モード: 平文の rootDir を暗号化して mountPoint に表示

例:
  encfs.exe C:\Users M:                                   # C:\Users を M:\ としてマウント
  encfs.exe C:\Users C:\mount\dokan                       # C:\mount\dokan にマウント
  encfs.exe C:\Users M: --dokan-network \\myfs\share      # UNC \\myfs\share でネットワークドライブ
  encfs.exe C:\Data M: --volume-name "セキュアドライブ"   # カスタムボリューム名でマウント
  encfs.exe C:\Data M: --use-credential                   # Credential Manager から保存済みパスワードを使用

アンマウントするには Ctrl+C（このコンソール）または:
  encfs.exe -u <mountPoint>
```

## インストール
1. **Dokany** (バージョン 2.0 以上) を [公式リリース](https://github.com/dokan-dev/dokany/releases) からインストール
2. [Releases ページ](https://github.com/mimidesunya/encfsy/releases) から最新版の **encfsy インストーラ** をダウンロードし、ウィザードに従ってセットアップ

## ライセンス
[LGPL-3.0](https://www.gnu.org/licenses/lgpl-3.0.en.html)

## 作者
[Mimi](https://github.com/mimidesunya) ｜ [X @mimidesunya](https://twitter.com/mimidesunya)
