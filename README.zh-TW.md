encfsy
======

🌐 **語言**: [English](README.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [Русский](README.ru.md) | [العربية](README.ar.md) | [Deutsch](README.de.md)

---

## 關於 encfsy
encfsy 是基於 **Dokany** 和 **Crypto++** 的 Windows 版 EncFS 實作。
**僅支援 64 位元系統**。

該程式在保持目錄樹結構不變的情況下加密檔案名稱和檔案內容。
這使其非常適合與 Dropbox、Google Drive、rsync 或其他遠端儲存同步加密資料：檔案保持端對端加密，儲存管理員無法查看其內容。

## ⚠️ 雙向雲端同步時請不要使用 `--paranoia`
啟用 `--paranoia` 模式（外部 IV 鏈接）時，如果同步工具變更了檔名，即使手動改回原名，**內容仍會變成無法解密的垃圾資料**。
- 在 Dropbox/OneDrive/Google Drive 等雙向同步情境，請 **不要啟用 `--paranoia`**。
- 若必須使用，請限定在檔名不會變更的單向備份用途。

## 安全功能
encfsy 使用 **Windows 認證管理員** 進行安全的密碼管理。

- 密碼使用 **DPAPI**（資料保護 API）加密，並與目前使用者帳戶繫結
- 無需在 GUI 和 encfs.exe 之間透過標準輸入傳遞密碼，消除了攔截風險
- 「記住密碼」選項可儲存密碼，下次啟動時自動輸入
- 密碼**按每個加密目錄（rootDir）單獨儲存**

### 密碼儲存位置
儲存的密碼可以在控制台 → 認證管理員 → Windows 認證中檢視。
它們以 `EncFSy:c:\path\to\encrypted` 這樣的名稱顯示。

## GUI 使用方法
使用 **encfsw.exe** 可以輕鬆地掛載和卸載磁碟區，無需使用命令列。

1. 選擇加密目錄（rootDir）
2. 選擇要掛載的磁碟機代號
3. 輸入密碼（勾選「Remember Password」可儲存）
4. 點擊「Mount」

「Show Advanced Options」可存取與命令列版本相同的詳細設定。

## 從命令列使用認證管理員
在 GUI 中勾選「Remember Password」進行掛載時，密碼會儲存到 Windows 認證管理員。
之後，您可以使用 `--use-credential` 選項從命令列無需輸入密碼即可掛載。

```bash
# 1. 首先在 GUI 中勾選「Remember Password」進行掛載
#    → 密碼儲存到認證管理員

# 2. 之後可以從命令列無需輸入密碼進行掛載
encfs.exe C:\Data M: --use-credential
```

## 檔案名稱長度限制
encfsy 使用現代的*長路徑* API，因此完整路徑不受傳統的 260 字元 **MAX_PATH** 限制。

但是 NTFS 仍然將每個路徑元件（資料夾或檔案名稱）限制在 **255 個 UTF-16 字元**。
由於加密會使名稱增長約 30%，為了保持在該元件限制內並與不支援長路徑的工具相容，請將**每個檔案名稱保持在 175 個字元以內**。

## 使用方法

```
用法: encfs.exe [選項] <rootDir> <mountPoint>

參數:
  rootDir      (例: C:\test)                要加密並掛載的目錄
  mountPoint   (例: M: 或 C:\mount\dokan)    掛載位置 - 磁碟機代號（如 M:\）
                                             或空的 NTFS 資料夾

選項:
  -u <mountPoint>                              卸載指定的磁碟區
  -l                                           列出目前掛載的 Dokan 磁碟區
  -v                                           將除錯輸出傳送到偵錯工具
  -s                                           將除錯輸出傳送到 stderr
  -i <ms>              (預設: 120000)          逾時時間（毫秒），逾時後操作中止並卸載磁碟區
  --use-credential                             從 Windows 認證管理員讀取密碼
                                               （密碼保持儲存狀態）
                                               注意：必須先在 GUI 中勾選「Remember Password」
                                               儲存密碼
  --use-credential-once                        從 Windows 認證管理員讀取密碼
                                               （讀取後刪除，一次性使用）
  --dokan-debug                                啟用 Dokan 除錯輸出
  --dokan-network <UNC>                        網路磁碟區的 UNC 路徑 (例: \\host\myfs)
  --dokan-removable                            將磁碟區顯示為卸除式媒體
  --dokan-write-protect                        以唯讀方式掛載檔案系統
  --dokan-mount-manager                        向 Windows 掛載管理員註冊磁碟區
                                               （啟用資源回收筒支援等）
  --dokan-current-session                      僅在目前工作階段中顯示磁碟區
  --dokan-filelock-user-mode                   在使用者模式下處理 LockFile/UnlockFile；
                                               否則 Dokan 自動管理
  --dokan-enable-unmount-network-drive         允許透過檔案總管卸載網路磁碟機
  --dokan-dispatch-driver-logs                 將核心驅動程式記錄轉送到使用者空間（較慢）
  --dokan-allow-ipc-batching                   為慢速檔案系統（如遠端儲存）啟用 IPC 批次處理
  --public                                     在 CreateFile 中開啟控制代碼時模擬呼叫使用者
                                               需要系統管理員權限
  --allocation-unit-size <bytes>               磁碟區報告的配置單位大小
  --sector-size <bytes>                        磁碟區報告的磁區大小
  --volume-name <name>                         檔案總管中顯示的磁碟區名稱（預設: EncFS）
  --volume-serial <hex>                        十六進位磁碟區序號（預設: 從底層取得）
  --paranoia                                   啟用 AES-256 加密、重新命名 IV 和外部 IV 鏈
  --alt-stream                                 啟用 NTFS 替代資料流
  --case-insensitive                           執行不區分大小寫的檔案名稱比對
  --reverse                                    反向模式: 將明文 rootDir 加密顯示在 mountPoint

範例:
  encfs.exe C:\Users M:                                    # 將 C:\Users 掛載為 M:\
  encfs.exe C:\Users C:\mount\dokan                        # 掛載到 NTFS 資料夾 C:\mount\dokan
  encfs.exe C:\Users M: --dokan-network \\myfs\share       # 以 UNC \\myfs\share 掛載為網路磁碟機
  encfs.exe C:\Data M: --volume-name "安全磁碟機"          # 使用自訂磁碟區名稱掛載
  encfs.exe C:\Data M: --use-credential                    # 使用認證管理員中儲存的密碼

要卸載，請在此主控台按 Ctrl+C 或執行:
  encfs.exe -u <mountPoint>
```

## 安裝
1. 安裝 **Dokany**（≥ 2.0）— 從[官方發布頁面](https://github.com/dokan-dev/dokany/releases)下載
2. 從 [Releases 頁面](https://github.com/mimidesunya/encfsy/releases)下載最新的 **encfsy 安裝程式**，按照安裝精靈進行設定

## 授權條款
[LGPL-3.0](https://www.gnu.org/licenses/lgpl-3.0.en.html)

## 作者
[Mimi](https://github.com/mimidesunya) ｜ [X @mimidesunya](https://twitter.com/mimidesunya)
