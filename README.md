```markdown
encfsy
======

## encfsy について
EncFS for Windows は **Dokany** と **Crypto++** をバックエンドに採用した  
Windows 向けの EncFS 実装です。**64 bit 環境専用**。

## ファイル名の長さ制限
encfsy は新しい「ロングパス」API を利用しているため、  
フルパス全体の 260 文字（MAX_PATH）制限は適用されません。  
しかし NTFS では各パス要素（フォルダー名・ファイル名）に **255 UTF‑16 文字**の上限があります。  
暗号化により名前はおよそ 3 割程度長くなるため、  
ロングパス非対応のツールとの互換性を保つには **各ファイル名を 175 文字未満**に抑えてください。

## 使い方
Usage: encfs.exe [options] <rootDir> <mountPoint>

引数:
  rootDir      (例: C:\test)                暗号化してマウントするディレクトリ
  mountPoint   (例: M: または C:\mount\dokan)  ドライブ文字または空の NTFS フォルダー

オプション:
  -u <mountPoint>                              指定したボリュームをアンマウント
  -l                                           マウント中の EncFS ボリューム一覧を表示
  -v                                           デバッグ出力をデバッガへ送信
  -s                                           デバッグ出力を stderr へ送信
  -i <ms>             (既定: 120000)          操作タイムアウト（ミリ秒）経過で自動アンマウント
  -t <数>              (既定: 5)              Dokan のワーカースレッド数
  --dokan-debug                                Dokan のデバッグ出力を有効化
  --dokan-network <UNC>                        ネットワークボリュームの UNC パス (例: \\host\myfs)
  --dokan-removable                            リムーバブルメディアとしてマウント
  --dokan-write-protect                        読み取り専用でマウント
  --dokan-mount-manager                        Windows Mount Manager に登録（ごみ箱などを有効化）
  --dokan-current-session                      現在のセッションのみにボリュームを公開
  --dokan-filelock-user-mode                   LockFile/UnlockFile をユーザーモードで処理
  --public                                     CreateFile 時に呼び出しユーザーを偽装（管理者権限要）
  --allocation-unit-size <bytes>               ボリュームのアロケーションユニットサイズを指定
  --sector-size <bytes>                        セクタサイズを指定
  --paranoia                                   AES‑256、リネーム IV、外部 IV チェーンを有効化
  --alt-stream                                 NTFS 代替データストリームを有効化
  --case-insensitive                           大文字小文字を区別しない名前照合
  --reverse                                    逆モード: <rootDir>→<mountPoint> へ暗号化

例:
  encfs.exe C:\Users M:                                   # C:\Users を M:\ としてマウント
  encfs.exe C:\Users C:\mount\dokan                       # C:\mount\dokan にマウント
  encfs.exe C:\Users M: --dokan-network \\myfs\share      # UNC \\myfs\share でネットワークドライブ

アンマウントするには Ctrl+C（このコンソール）または:
  encfs.exe -u <mountPoint>

## インストール
1. **Dokany** (バージョン 2.0 以上) を [公式リリース](https://github.com/dokan-dev/dokany/releases) からインストール  
2. [Releases ページ](https://github.com/mimidesunya/encfsy/releases) から最新版の **encfsy インストーラ** をダウンロードし、ウィザードに従ってセットアップ

## Abount
EncFS for Windows, backed by **Dokany** and **Crypto++**.  
Designed **exclusively for 64-bit environments**.

## Filename Length Limit
encfsy uses the modern “long-path” API, so the 260-character MAX_PATH limit on full
paths no longer applies. NTFS still caps each *individual* name at 255 UTF-16
characters, and encryption expands names by roughly one-third, so keep every
filename under **175 characters** to stay safely below that per-component limit and
retain compatibility with tools that are not long-path–aware.

## Usage
		Usage: encfs.exe [options] <rootDir> <mountPoint>
		
		Arguments:
		  rootDir      (e.g., C:\test)                Directory to be encrypted and mounted.
		  mountPoint   (e.g., M: or C:\mount\dokan)   Mount location — either a drive letter
		                                               such as M:\ or an empty NTFS folder.
		
		Options:
		  -u <mountPoint>                              Unmount the specified volume.
		  -l                                           List currently mounted EncFS volumes.
		  -v                                           Send debug output to an attached debugger.
		  -s                                           Send debug output to stderr.
		  -i <ms>              (default: 120000)       Timeout (in milliseconds) before a running
		                                               operation is aborted and the volume unmounted.
		  -t <count>           (default: 5)            Number of worker threads for the Dokan library.
		  --dokan-debug                                Enable Dokan debug output.
		  --dokan-network <UNC>                        UNC path for a network volume (e.g., \\host\myfs).
		  --dokan-removable                            Present the volume as removable media.
		  --dokan-write-protect                        Mount the filesystem read-only.
		  --dokan-mount-manager                        Register the volume with the Windows Mount Manager
		                                               (enables Recycle Bin support, etc.).
		  --dokan-current-session                      Make the volume visible only in the current session.
		  --dokan-filelock-user-mode                   Handle LockFile/UnlockFile in user mode; otherwise
		                                               Dokan manages them automatically.
		  --public                                     Impersonate the calling user when opening handles
		                                               in CreateFile. Requires administrator privileges.
		  --allocation-unit-size <bytes>               Allocation-unit size reported by the volume.
		  --sector-size <bytes>                        Sector size reported by the volume.
		  --paranoia                                   Enable AES-256 encryption, renamed IVs, and external
		                                               IV chaining.
		  --alt-stream                                 Enable NTFS alternate data streams.
		  --case-insensitive                           Perform case-insensitive filename matching.
		  --reverse                                    Reverse mode: encrypt from <rootDir> to <mountPoint>.
		
		Examples:
		  encfs.exe C:\Users M:                                    # Mount C:\Users as drive M:\
		  encfs.exe C:\Users C:\mount\dokan                       # Mount C:\Users at NTFS folder C:\mount\dokan
		  encfs.exe C:\Users M: --dokan-network \\myfs\share        # Mount C:\Users as network drive M:\ with UNC \\myfs\share
		
		To unmount, press Ctrl+C in this console or run:
		  encfs.exe -u <mountPoint>

## Installation
1. Install **Dokany** (≥ 2.0) — download from the [official releases](https://github.com/dokan-dev/dokany/releases).  
2. Download the latest **encfsy installer** from the [Releases page](https://github.com/mimidesunya/encfsy/releases) and follow the setup wizard.

## License
[LGPL-3.0](https://www.gnu.org/licenses/lgpl-3.0.en.html)

## Author
[Mimi](https://github.com/mimidesunya) ｜ [Twitter @ mimidesunya](https://twitter.com/mimidesunya)
```