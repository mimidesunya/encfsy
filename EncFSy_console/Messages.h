/**
 * @file Messages.h
 * @brief Internationalized message strings for EncFSy console application
 * 
 * Supports: English (default), Japanese, Korean, Chinese (Simplified/Traditional), Russian, Arabic, German
 * Language is auto-detected from Windows system settings.
 */

#pragma once

#include <windows.h>
#include <string>
#include <fstream>

namespace EncFSMessages {

enum class Language {
    English,
    Japanese,
    Korean,
    ChineseSimplified,
    ChineseTraditional,
    Russian,
    Arabic,
    German
};

// Use static storage for C++14 compatibility (inline variables require C++17)
static Language g_CurrentLanguage = Language::English;
static std::string g_Version = "(dev)";

/**
 * @brief Reads version from Version.txt file
 */
inline void InitVersion() {
    // Get the directory where the executable is located
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH) > 0) {
        std::string path(exePath);
        size_t pos = path.find_last_of("\\/");
        if (pos != std::string::npos) {
            std::string dir = path.substr(0, pos + 1);
            std::string versionFile = dir + "Version.txt";
            
            std::ifstream file(versionFile);
            if (file.is_open()) {
                std::getline(file, g_Version);
                // Trim whitespace
                size_t start = g_Version.find_first_not_of(" \t\r\n");
                size_t end = g_Version.find_last_not_of(" \t\r\n");
                if (start != std::string::npos && end != std::string::npos) {
                    g_Version = g_Version.substr(start, end - start + 1);
                }
                file.close();
            }
        }
    }
}

/**
 * @brief Gets the version string
 */
inline const char* GetVersion() {
    return g_Version.c_str();
}

/**
 * @brief Detects the system language and sets g_CurrentLanguage
 */
inline void InitLanguage() {
    LANGID langId = GetUserDefaultUILanguage();
    WORD primaryLang = PRIMARYLANGID(langId);
    WORD subLang = SUBLANGID(langId);
    
    switch (primaryLang) {
        case LANG_JAPANESE:
            g_CurrentLanguage = Language::Japanese;
            break;
        case LANG_KOREAN:
            g_CurrentLanguage = Language::Korean;
            break;
        case LANG_CHINESE:
            // Distinguish between Simplified and Traditional Chinese
            if (subLang == SUBLANG_CHINESE_TRADITIONAL ||
                subLang == SUBLANG_CHINESE_HONGKONG ||
                subLang == SUBLANG_CHINESE_MACAU) {
                g_CurrentLanguage = Language::ChineseTraditional;
            } else {
                g_CurrentLanguage = Language::ChineseSimplified;
            }
            break;
        case LANG_RUSSIAN:
            g_CurrentLanguage = Language::Russian;
            break;
        case LANG_ARABIC:
            g_CurrentLanguage = Language::Arabic;
            break;
        case LANG_GERMAN:
            g_CurrentLanguage = Language::German;
            break;
        default:
            g_CurrentLanguage = Language::English;
            break;
    }
}

// ============================================================================
// Usage Text
// ============================================================================

inline const char* GetUsageText_EN() {
    return
        "Usage: encfs.exe [options] <rootDir> <mountPoint>\n"
        "\n"
        "Arguments:\n"
        "  rootDir      (e.g., C:\\test)                Directory to be encrypted and mounted.\n"
        "  mountPoint   (e.g., M: or C:\\mount\\dokan)   Mount location - either a drive letter\n"
        "                                               such as M:\\ or an empty NTFS folder.\n"
        "\n"
        "Options:\n"
        "  -u <mountPoint>                              Unmount the specified volume.\n"
        "  -l                                           List currently mounted Dokan volumes.\n"
        "  -v                                           Send debug output to an attached debugger.\n"
        "  -s                                           Send debug output to stderr.\n"
        "  -i <ms>              (default: 120000)       Timeout (in milliseconds) before a running\n"
        "                                               operation is aborted and the volume unmounted.\n"
        "  --use-credential                             Read password from Windows Credential Manager\n"
        "                                               instead of prompting. Password is kept stored.\n"
        "  --use-credential-once                        Read password from Windows Credential Manager\n"
        "                                               and delete it after reading (one-time use).\n"
        "  --dokan-debug                                Enable Dokan debug output.\n"
        "  --dokan-network <UNC>                        UNC path for a network volume (e.g., \\\\host\\myfs).\n"
        "  --dokan-removable                            Present the volume as removable media.\n"
        "  --dokan-write-protect                        Mount the filesystem read-only.\n"
        "  --dokan-mount-manager                        Register the volume with the Windows Mount Manager\n"
        "                                               (enables Recycle Bin support, etc.).\n"
        "  --dokan-current-session                      Make the volume visible only in the current session.\n"
        "  --dokan-filelock-user-mode                   Handle LockFile/UnlockFile in user mode; otherwise\n"
        "                                               Dokan manages them automatically.\n"
        "  --dokan-enable-unmount-network-drive         Allow unmounting network drive via Explorer.\n"
        "  --dokan-dispatch-driver-logs                 Forward kernel driver logs to userland (slow).\n"
        "  --dokan-allow-ipc-batching                   Enable IPC batching for slow filesystems\n"
        "                                               (e.g., remote storage).\n"
        "  --public                                     Impersonate the calling user when opening handles\n"
        "                                               in CreateFile. Requires administrator privileges.\n"
        "  --allocation-unit-size <bytes>               Allocation-unit size reported by the volume.\n"
        "  --sector-size <bytes>                        Sector size reported by the volume.\n"
        "  --volume-name <name>                         Volume name shown in Explorer (default: EncFS).\n"
        "  --volume-serial <hex>                        Volume serial number in hex (default: from underlying).\n"
        "  --paranoia                                   Enable AES-256 encryption, renamed IVs, and external\n"
        "                                               IV chaining.\n"
        "  --alt-stream                                 Enable NTFS alternate data streams.\n"
        "  --case-insensitive                           Perform case-insensitive filename matching.\n"
        "  --reverse                                    Reverse mode: show plaintext rootDir as encrypted\n"
        "                                               at mountPoint.\n"
        "\n"
        "Examples:\n"
        "  encfs.exe C:\\Users M:                                    # Mount C:\\Users as drive M:\\\n"
        "  encfs.exe C:\\Users C:\\mount\\dokan                       # Mount C:\\Users at NTFS folder C:\\mount\\dokan\n"
        "  encfs.exe C:\\Users M: --dokan-network \\\\myfs\\share       # Mount as network drive with UNC \\\\myfs\\share\n"
        "  encfs.exe C:\\Data M: --volume-name \"My Secure Drive\"     # Mount with custom volume name\n"
        "  encfs.exe C:\\Data M: --use-credential                    # Use stored password (keep it stored)\n"
        "  encfs.exe C:\\Data M: --use-credential-once               # Use stored password (delete after use)\n"
        "\n"
        "To unmount, press Ctrl+C in this console or run:\n"
        "  encfs.exe -u <mountPoint>\n";
}

inline const char* GetUsageText_JA() {
    return
        u8"使い方: encfs.exe [オプション] <rootDir> <mountPoint>\n"
        u8"\n"
        u8"引数:\n"
        u8"  rootDir      (例: C:\\test)                暗号化してマウントするディレクトリ\n"
        u8"  mountPoint   (例: M: または C:\\mount\\dokan)  ドライブ文字または空の NTFS フォルダー\n"
        u8"\n"
        u8"オプション:\n"
        u8"  -u <mountPoint>                              指定したボリュームをアンマウント\n"
        u8"  -l                                           マウント中の Dokan ボリューム一覧を表示\n"
        u8"  -v                                           デバッグ出力をデバッガへ送信\n"
        u8"  -s                                           デバッグ出力を stderr へ送信\n"
        u8"  -i <ms>              (既定: 120000)          操作タイムアウト（ミリ秒）経過で自動アンマウント\n"
        u8"  --use-credential                             Windows Credential Manager からパスワードを取得\n"
        u8"                                               （パスワードは保持される）\n"
        u8"  --use-credential-once                        Windows Credential Manager からパスワードを取得\n"
        u8"                                               （読み取り後に削除、一回限りの使用）\n"
        u8"  --dokan-debug                                Dokan のデバッグ出力を有効化\n"
        u8"  --dokan-network <UNC>                        ネットワークボリュームの UNC パス (例: \\\\host\\myfs)\n"
        u8"  --dokan-removable                            リムーバブルメディアとしてマウント\n"
        u8"  --dokan-write-protect                        読み取り専用でマウント\n"
        u8"  --dokan-mount-manager                        Windows Mount Manager に登録（ごみ箱などを有効化）\n"
        u8"  --dokan-current-session                      現在のセッションのみにボリュームを公開\n"
        u8"  --dokan-filelock-user-mode                   LockFile/UnlockFile をユーザーモードで処理\n"
        u8"  --dokan-enable-unmount-network-drive         エクスプローラーからネットワークドライブをアンマウント可能にする\n"
        u8"  --dokan-dispatch-driver-logs                 カーネルドライバーのログをユーザーランドに転送（低速）\n"
        u8"  --dokan-allow-ipc-batching                   低速なファイルシステム向けに IPC バッチ処理を有効化\n"
        u8"  --public                                     CreateFile 時に呼び出しユーザーを偽装（管理者権限要）\n"
        u8"  --allocation-unit-size <bytes>               ボリュームのアロケーションユニットサイズを指定\n"
        u8"  --sector-size <bytes>                        セクタサイズを指定\n"
        u8"  --volume-name <name>                         エクスプローラーに表示するボリューム名（既定: EncFS）\n"
        u8"  --volume-serial <hex>                        ボリュームシリアル番号（16進数、既定: 下層から取得）\n"
        u8"  --paranoia                                   AES-256、リネーム IV、外部 IV チェーンを有効化\n"
        u8"  --alt-stream                                 NTFS 代替データストリームを有効化\n"
        u8"  --case-insensitive                           大文字小文字を区別しない名前照合\n"
        u8"  --reverse                                    逆モード: 平文の rootDir を暗号化して mountPoint に表示\n"
        u8"\n"
        u8"例:\n"
        u8"  encfs.exe C:\\Users M:                                   # C:\\Users を M:\\ としてマウント\n"
        u8"  encfs.exe C:\\Users C:\\mount\\dokan                       # C:\\mount\\dokan にマウント\n"
        u8"  encfs.exe C:\\Users M: --dokan-network \\\\myfs\\share      # UNC \\\\myfs\\share でネットワークドライブ\n"
        u8"  encfs.exe C:\\Data M: --volume-name \"セキュアドライブ\"   # カスタムボリューム名でマウント\n"
        u8"  encfs.exe C:\\Data M: --use-credential                   # 保存済みパスワードを使用（保持）\n"
        u8"  encfs.exe C:\\Data M: --use-credential-once              # 保存済みパスワードを使用（削除）\n"
        u8"\n"
        u8"アンマウントするには Ctrl+C（このコンソール）または:\n"
        u8"  encfs.exe -u <mountPoint>\n";
}

inline const char* GetUsageText_KO() {
    return
        u8"사용법: encfs.exe [옵션] <rootDir> <mountPoint>\n"
        u8"\n"
        u8"인수:\n"
        u8"  rootDir      (예: C:\\test)                암호화하여 마운트할 디렉토리\n"
        u8"  mountPoint   (예: M: 또는 C:\\mount\\dokan)   드라이브 문자 또는 빈 NTFS 폴더\n"
        u8"\n"
        u8"옵션:\n"
        u8"  -u <mountPoint>                              지정된 볼륨 언마운트\n"
        u8"  -l                                           현재 마운트된 Dokan 볼륨 목록 표시\n"
        u8"  -v                                           연결된 디버거로 디버그 출력 전송\n"
        u8"  -s                                           stderr로 디버그 출력 전송\n"
        u8"  -i <ms>              (기본값: 120000)        타임아웃(밀리초) 경과 시 자동 언마운트\n"
        u8"  --use-credential                             Windows Credential Manager에서 비밀번호 읽기\n"
        u8"                                               (비밀번호는 저장된 상태로 유지)\n"
        u8"  --use-credential-once                        Windows Credential Manager에서 비밀번호를 읽고\n"
        u8"                                               읽은 후 삭제 (일회용)\n"
        u8"  --dokan-debug                                Dokan 디버그 출력 활성화\n"
        u8"  --dokan-network <UNC>                        네트워크 볼륨의 UNC 경로 (예: \\\\host\\myfs)\n"
        u8"  --dokan-removable                            이동식 미디어로 볼륨 표시\n"
        u8"  --dokan-write-protect                        읽기 전용으로 파일 시스템 마운트\n"
        u8"  --dokan-mount-manager                        Windows Mount Manager에 볼륨 등록\n"
        u8"                                               (휴지통 지원 등 활성화)\n"
        u8"  --dokan-current-session                      현재 세션에서만 볼륨 표시\n"
        u8"  --dokan-filelock-user-mode                   사용자 모드에서 LockFile/UnlockFile 처리\n"
        u8"  --dokan-enable-unmount-network-drive         탐색기에서 네트워크 드라이브 언마운트 허용\n"
        u8"  --dokan-dispatch-driver-logs                 커널 드라이버 로그를 사용자 영역으로 전달 (느림)\n"
        u8"  --dokan-allow-ipc-batching                   느린 파일 시스템을 위한 IPC 배칭 활성화\n"
        u8"  --public                                     CreateFile에서 호출 사용자를 가장 (관리자 권한 필요)\n"
        u8"  --allocation-unit-size <bytes>               볼륨에서 보고하는 할당 단위 크기\n"
        u8"  --sector-size <bytes>                        볼륨에서 보고하는 섹터 크기\n"
        u8"  --volume-name <name>                         탐색기에 표시되는 볼륨 이름 (기본값: EncFS)\n"
        u8"  --volume-serial <hex>                        16진수 볼륨 일련 번호 (기본값: 하위 항목에서 가져옴)\n"
        u8"  --paranoia                                   AES-256 암호화, 이름 변경 IV 및 외부 IV 체인 활성화\n"
        u8"  --alt-stream                                 NTFS 대체 데이터 스트림 활성화\n"
        u8"  --case-insensitive                           대소문자를 구분하지 않는 파일 이름 매칭 수행\n"
        u8"  --reverse                                    역방향 모드: 평문 rootDir을 mountPoint에\n"
        u8"                                               암호화하여 표시\n"
        u8"\n"
        u8"예:\n"
        u8"  encfs.exe C:\\Users M:                                    # C:\\Users를 M:\\로 마운트\n"
        u8"  encfs.exe C:\\Users C:\\mount\\dokan                       # NTFS 폴더 C:\\mount\\dokan에 마운트\n"
        u8"  encfs.exe C:\\Users M: --dokan-network \\\\myfs\\share       # UNC \\\\myfs\\share로 네트워크 드라이브\n"
        u8"  encfs.exe C:\\Data M: --volume-name \"보안 드라이브\"        # 사용자 정의 볼륨 이름으로 마운트\n"
        u8"  encfs.exe C:\\Data M: --use-credential                    # 저장된 비밀번호 사용 (유지)\n"
        u8"  encfs.exe C:\\Data M: --use-credential-once               # 저장된 비밀번호 사용 (삭제)\n"
        u8"\n"
        u8"언마운트하려면 이 콘솔에서 Ctrl+C를 누르거나 다음을 실행하세요:\n"
        u8"  encfs.exe -u <mountPoint>\n";
}

inline const char* GetUsageText_ZH() {
    return
        u8"用法: encfs.exe [选项] <rootDir> <mountPoint>\n"
        u8"\n"
        u8"参数:\n"
        u8"  rootDir      (例: C:\\test)                要加密并挂载的目录\n"
        u8"  mountPoint   (例: M: 或 C:\\mount\\dokan)    挂载位置 - 驱动器号或空的 NTFS 文件夹\n"
        "\n"
        u8"选项:\n"
        u8"  -u <mountPoint>                              卸载指定的卷\n"
        u8"  -l                                           列出当前挂载的 Dokan 卷\n"
        u8"  -v                                           将调试输出发送到调试器\n"
        u8"  -s                                           将调试输出发送到 stderr\n"
        u8"  -i <ms>              (默认: 120000)          超时时间（毫秒），超时后操作中止并卸载卷\n"
        u8"  --use-credential                             从 Windows 凭据管理器读取密码（保留存储）\n"
        u8"  --use-credential-once                        从 Windows 凭据管理器读取密码（读取后删除）\n"
        u8"  --dokan-debug                                启用 Dokan 调试输出\n"
        u8"  --dokan-network <UNC>                        网络卷的 UNC 路径 (例: \\\\host\\myfs)\n"
        u8"  --dokan-removable                            将卷显示为可移动媒体\n"
        u8"  --dokan-write-protect                        以只读方式挂载文件系统\n"
        u8"  --dokan-mount-manager                        向 Windows 挂载管理器注册卷（启用回收站等）\n"
        u8"  --dokan-current-session                      仅在当前会话中显示卷\n"
        u8"  --dokan-filelock-user-mode                   在用户模式下处理 LockFile/UnlockFile\n"
        u8"  --dokan-enable-unmount-network-drive         允许通过资源管理器卸载网络驱动器\n"
        u8"  --dokan-dispatch-driver-logs                 将内核驱动程序日志转发到用户空间（较慢）\n"
        u8"  --dokan-allow-ipc-batching                   为慢速文件系统启用 IPC 批处理\n"
        u8"  --public                                     在 CreateFile 中模拟调用用户（需要管理员权限）\n"
        u8"  --allocation-unit-size <bytes>               卷报告的分配单元大小\n"
        u8"  --sector-size <bytes>                        卷报告的扇区大小\n"
        u8"  --volume-name <name>                         资源管理器中显示的卷名（默认: EncFS）\n"
        "  --volume-serial <hex>                        十六进制卷序列号（默认: 从底层获取）\n"
        u8"  --paranoia                                   启用 AES-256 加密、重命名 IV 和外部 IV 链\n"
        u8"  --alt-stream                                 启用 NTFS 备用数据流\n"
        u8"  --case-insensitive                           执行不区分大小写的文件名匹配\n"
        u8"  --reverse                                    反向模式: 将明文 rootDir 加密显示在 mountPoint\n"
        u8"\n"
        u8"示例:\n"
        u8"  encfs.exe C:\\Users M:                                    # 将 C:\\Users 挂载为 M:\\\n"
        u8"  encfs.exe C:\\Users C:\\mount\\dokan                       # 挂载到 NTFS 文件夹 C:\\mount\\dokan\n"
        u8"  encfs.exe C:\\Users M: --dokan-network \\\\myfs\\share       # 以 UNC \\\\myfs\\share 挂载为网络驱动器\n"
        u8"  encfs.exe C:\\Data M: --volume-name \"安全驱动器\"          # 使用自定义卷名挂载\n"
        u8"  encfs.exe C:\\Data M: --use-credential                    # 使用存储的密码（保留）\n"
        u8"  encfs.exe C:\\Data M: --use-credential-once               # 使用存储的密码（删除）\n"
        u8"\n"
        u8"要卸载，请在此控制台按 Ctrl+C 或运行:\n"
        u8"  encfs.exe -u <mountPoint>\n";
}

inline const char* GetUsageText_ZH_TW() {
    return
        u8"用法: encfs.exe [選項] <rootDir> <mountPoint>\n"
        u8"\n"
        u8"參數:\n"
        u8"  rootDir      (例: C:\\test)                要加密並掛載的目錄\n"
        u8"  mountPoint   (例: M: 或 C:\\mount\\dokan)    掛載位置 - 磁碟機代號或空的 NTFS 資料夾\n"
        "\n"
        u8"選項:\n"
        u8"  -u <mountPoint>                              卸載指定的磁碟區\n"
        u8"  -l                                           列出目前掛載的 Dokan 磁碟區\n"
        u8"  -v                                           將除錯輸出傳送到偵錯工具\n"
        u8"  -s                                           將除錯輸出傳送到 stderr\n"
        u8"  -i <ms>              (預設: 120000)          逾時時間（毫秒），逾時後操作中止並卸載磁碟區\n"
        u8"  --use-credential                             從 Windows 認證管理員讀取密碼（保留儲存）\n"
        u8"  --use-credential-once                        從 Windows 認證管理員讀取密碼（讀取後刪除）\n"
        u8"  --dokan-debug                                啟用 Dokan 除錯輸出\n"
        u8"  --dokan-network <UNC>                        網路磁碟區的 UNC 路徑 (例: \\\\host\\myfs)\n"
        u8"  --dokan-removable                            將磁碟區顯示為卸除式媒體\n"
        u8"  --dokan-write-protect                        以唯讀方式掛載檔案系統\n"
        u8"  --dokan-mount-manager                        向 Windows 掛載管理員註冊磁碟區（啟用資源回收筒等）\n"
        u8"  --dokan-current-session                      僅在目前工作階段中顯示磁碟區\n"
        u8"  --dokan-filelock-user-mode                   在使用者模式下處理 LockFile/UnlockFile\n"
        u8"  --dokan-enable-unmount-network-drive         允許透過檔案總管卸載網路磁碟機\n"
        u8"  --dokan-dispatch-driver-logs                 將核心驅動程式記錄轉送到使用者空間（較慢）\n"
        u8"  --dokan-allow-ipc-batching                   為慢速檔案系統啟用 IPC 批次處理\n"
        u8"  --public                                     在 CreateFile 中模擬呼叫使用者（需要系統管理員權限）\n"
        u8"  --allocation-unit-size <bytes>               磁碟區報告的配置單位大小\n"
        u8"  --sector-size <bytes>                        磁碟區報告的磁區大小\n"
        u8"  --volume-name <name>                         檔案總管中顯示的磁碟區名稱（預設: EncFS）\n"
        u8"  --volume-serial <hex>                        十六進位磁碟區序號（預設: 從底層取得）\n"
        u8"  --paranoia                                   啟用 AES-256 加密、重新命名 IV 和外部 IV 鏈\n"
        u8"  --alt-stream                                 啟用 NTFS 替代資料流\n"
        u8"  --case-insensitive                           執行不區分大小寫的檔案名稱比對\n"
        u8"  --reverse                                    反向模式: 將明文 rootDir 加密顯示在 mountPoint\n"
        u8"\n"
        u8"範例:\n"
        u8"  encfs.exe C:\\Users M:                                    # 將 C:\\Users 掛載為 M:\\\n"
        u8"  encfs.exe C:\\Users C:\\mount\\dokan                       # 掛載到 NTFS 資料夾 C:\\mount\\dokan\n"
        u8"  encfs.exe C:\\Users M: --dokan-network \\\\myfs\\share       # 以 UNC \\\\myfs\\share 掛載為網路磁碟機\n"
        u8"  encfs.exe C:\\Data M: --volume-name \"安全磁碟機\"          # 使用自訂磁碟區名稱掛載\n"
        u8"  encfs.exe C:\\Data M: --use-credential                    # 使用儲存的密碼（保留）\n"
        u8"  encfs.exe C:\\Data M: --use-credential-once               # 使用儲存的密碼（刪除）\n"
        u8"\n"
        u8"要卸載，請在此主控台按 Ctrl+C 或執行:\n"
        u8"  encfs.exe -u <mountPoint>\n";
}

inline const char* GetUsageText_RU() {
    return
        u8"Использование: encfs.exe [опции] <rootDir> <mountPoint>\n"
        u8"\n"
        u8"Аргументы:\n"
        u8"  rootDir      (напр., C:\\test)              Каталог для шифрования и монтирования\n"
        u8"  mountPoint   (напр., M: или C:\\mount\\dokan) Точка монтирования - буква диска\n"
        u8"                                               или пустая папка NTFS\n"
        u8"\n"
        u8"Опции:\n"
        u8"  -u <mountPoint>                              Размонтировать указанный том\n"
        u8"  -l                                           Показать смонтированные тома Dokan\n"
        u8"  -v                                           Отправить отладочный вывод в отладчик\n"
        u8"  -s                                           Отправить отладочный вывод в stderr\n"
        u8"  -i <мс>              (по умолч.: 120000)     Таймаут (мс) до отмены операции и размонтирования\n"
        u8"  --use-credential                             Читать пароль из Диспетчера учётных данных Windows\n"
        u8"                                               (пароль сохраняется)\n"
        u8"  --use-credential-once                        Читать пароль из Диспетчера учётных данных Windows\n"
        u8"                                               (удалить после чтения)\n"
        u8"  --dokan-debug                                Включить отладочный вывод Dokan\n"
        u8"  --dokan-network <UNC>                        UNC-путь для сетевого тома (напр., \\\\host\\myfs)\n"
        u8"  --dokan-removable                            Представить том как съёмный носитель\n"
        u8"  --dokan-write-protect                        Монтировать файловую систему только для чтения\n"
        u8"  --dokan-mount-manager                        Зарегистрировать том в Диспетчере монтирования Windows\n"
        u8"                                               (включает поддержку Корзины и т.д.)\n"
        u8"  --dokan-current-session                      Показывать том только в текущем сеансе\n"
        u8"  --dokan-filelock-user-mode                   Обрабатывать LockFile/UnlockFile в пользовательском режиме\n"
        u8"  --dokan-enable-unmount-network-drive         Разрешить размонтирование сетевого диска через Проводник\n"
        u8"  --dokan-dispatch-driver-logs                 Пересылать логи драйвера ядра в userland (медленно)\n"
        u8"  --dokan-allow-ipc-batching                   Включить пакетную обработку IPC для медленных ФС\n"
        u8"  --public                                     Имперсонировать вызывающего пользователя в CreateFile\n"
        u8"                                               (требуются права администратора)\n"
        u8"  --allocation-unit-size <байт>                Размер единицы распределения тома\n"
        u8"  --sector-size <байт>                         Размер сектора тома\n"
        u8"  --volume-name <имя>                          Имя тома в Проводнике (по умолч.: EncFS)\n"
        u8"  --volume-serial <hex>                        Серийный номер тома в hex (по умолч.: из базового)\n"
        u8"  --paranoia                                   Включить AES-256, переименование IV и внешнюю цепочку IV\n"
        u8"  --alt-stream                                 Включить альтернативные потоки данных NTFS\n"
        u8"  --case-insensitive                           Сопоставление имён файлов без учёта регистра\n"
        u8"  --reverse                                    Обратный режим: показать plaintext rootDir\n"
        u8"                                               зашифрованным в mountPoint\n"
        u8"\n"
        u8"Примеры:\n"
        u8"  encfs.exe C:\\Users M:                                    # Смонтировать C:\\Users как диск M:\\\n"
        u8"  encfs.exe C:\\Users C:\\mount\\dokan                       # Смонтировать в папку NTFS\n"
        u8"  encfs.exe C:\\Users M: --dokan-network \\\\myfs\\share       # Как сетевой диск с UNC\n"
        u8"  encfs.exe C:\\Data M: --volume-name \"Безопасный диск\"     # С пользовательским именем тома\n"
        u8"  encfs.exe C:\\Data M: --use-credential                    # Использовать сохранённый пароль\n"
        u8"  encfs.exe C:\\Data M: --use-credential-once               # Использовать пароль (удалить после)\n"
        u8"\n"
        u8"Для размонтирования нажмите Ctrl+C в этой консоли или выполните:\n"
        u8"  encfs.exe -u <mountPoint>\n";
}

inline const char* GetUsageText_AR() {
    return
        u8"الاستخدام: encfs.exe [خيارات] <rootDir> <mountPoint>\n"
        u8"\n"
        u8"المعاملات:\n"
        u8"  rootDir      (مثال: C:\\test)              المجلد المراد تشفيره وتركيبه\n"
        u8"  mountPoint   (مثال: M: أو C:\\mount\\dokan)  نقطة التركيب - حرف محرك أقراص\n"
        u8"                                               أو مجلد NTFS فارغ\n"
        u8"\n"
        u8"الخيارات:\n"
        u8"  -u <mountPoint>                              إلغاء تركيب وحدة التخزين المحددة\n"
        u8"  -l                                           عرض وحدات تخزين Dokan المركبة حالياً\n"
        u8"  -v                                           إرسال مخرجات التصحيح إلى المصحح\n"
        u8"  -s                                           إرسال مخرجات التصحيح إلى stderr\n"
        u8"  -i <مللي ثانية>       (افتراضي: 120000)      مهلة (بالمللي ثانية) قبل إلغاء العملية\n"
        u8"  --use-credential                             قراءة كلمة المرور من مدير بيانات الاعتماد\n"
        u8"                                               (الاحتفاظ بكلمة المرور)\n"
        u8"  --use-credential-once                        قراءة كلمة المرور من مدير بيانات الاعتماد\n"
        u8"                                               (حذفها بعد القراءة)\n"
        u8"  --dokan-debug                                تمكين مخرجات تصحيح Dokan\n"
        u8"  --dokan-network <UNC>                        مسار UNC لوحدة تخزين الشبكة\n"
        u8"  --dokan-removable                            عرض وحدة التخزين كوسائط قابلة للإزالة\n"
        u8"  --dokan-write-protect                        تركيب نظام الملفات للقراءة فقط\n"
        u8"  --dokan-mount-manager                        تسجيل وحدة التخزين مع مدير التركيب\n"
        u8"                                               (تمكين دعم سلة المهملات وما إلى ذلك).\n"
        u8"  --dokan-current-session                      إظهار وحدة التخزين في الجلسة الحالية فقط\n"
        u8"  --dokan-filelock-user-mode                   معالجة LockFile/UnlockFile في وضع المستخدم\n"
        u8"  --dokan-enable-unmount-network-drive         السماح بإلغاء تركيب محرك الشبكة عبر المستكشف\n"
        u8"  --dokan-dispatch-driver-logs                 إعادة توجيه سجلات برنامج التشغيل (بطيء).\n"
        u8"  --dokan-allow-ipc-batching                   تمكين تجميع IPC لأنظمة الملفات البطيئة\n"
        u8"  --public                                     انتحال هوية المستخدم المتصل (يتطلب صلاحيات المسؤول)\n"
        u8"  --allocation-unit-size <بايت>                حجم وحدة التخصيص\n"
        u8"  --sector-size <بايت>                         حجم القطاع\n"
        u8"  --volume-name <اسم>                          اسم وحدة التخزين في المستكشف (افتراضي: EncFS)\n"
        u8"  --volume-serial <hex>                        الرقم التسلسلي لوحدة التخزين بالست عشري\n"
        u8"  --paranoia                                   تمكين تشفير AES-256 وتسلسل IV الخارجي\n"
        u8"  --alt-stream                                 تمكين تدفقات البيانات البديلة NTFS\n"
        u8"  --case-insensitive                           مطابقة أسماء الملفات بدون تمييز حالة الأحرف\n"
        u8"  --reverse                                    الوضع العكسي: عرض rootDir النص العادي مشفراً\n"
        u8"\n"
        u8"أمثلة:\n"
        u8"  encfs.exe C:\\Users M:                                    # تركيب C:\\Users كمحرك M:\\\n"
        u8"  encfs.exe C:\\Users C:\\mount\\dokan                       # تركيب في مجلد NTFS\n"
        u8"  encfs.exe C:\\Users M: --dokan-network \\\\myfs\\share       # كمحرك شبكة مع UNC\n"
        u8"  encfs.exe C:\\Data M: --volume-name \"محرك آمن\"            # تركيب باسم مخصص\n"
        u8"  encfs.exe C:\\Data M: --use-credential                    # استخدام كلمة المرور المحفوظة\n"
        u8"  encfs.exe C:\\Data M: --use-credential-once               # استخدام كلمة المرور (حذف بعد)\n"
        u8"\n"
        u8"لإلغاء التركيب، اضغط Ctrl+C في هذه الوحدة الطرفية أو نفذ:\n"
        u8"  encfs.exe -u <mountPoint>\n";
}

inline const char* GetUsageText_DE() {
    return
        u8"Verwendung: encfs.exe [Optionen] <rootDir> <mountPoint>\n"
        u8"\n"
        u8"Argumente:\n"
        u8"  rootDir      (z.B. C:\\test)               Zu verschlüsselndes und einzuhängendes Verzeichnis\n"
        u8"  mountPoint   (z.B. M: oder C:\\mount\\dokan) Einhängepunkt - Laufwerksbuchstabe\n"
        u8"                                               oder leerer NTFS-Ordner\n"
        u8"\n"
        u8"Optionen:\n"
        u8"  -u <mountPoint>                              Angegebenes Volume aushängen\n"
        u8"  -l                                           Aktuell eingehte Dokan-Volumes auflisten\n"
        u8"  -v                                           Debug-Ausgabe an Debugger senden\n"
        u8"  -s                                           Debug-Ausgabe an stderr senden\n"
        u8"  -i <ms>              (Standard: 120000)      Zeitüberschreitung (ms) bis zum Abbruch\n"
        u8"  --use-credential                             Passwort aus Windows-Anmeldeinformationsverwaltung\n"
        u8"                                               lesen (Passwort wird gespeichert)\n"
        u8"  --use-credential-once                        Passwort aus Windows-Anmeldeinformationsverwaltung\n"
        u8"                                               lesen (nach dem Lesen löschen)\n"
        u8"  --dokan-debug                                Dokan-Debug-Ausgabe aktivieren\n"
        u8"  --dokan-network <UNC>                        UNC-Pfad für Netzwerk-Volume (z.B. \\\\host\\myfs)\n"
        u8"  --dokan-removable                            Volume als Wechselmedium anzeigen\n"
        u8"  --dokan-write-protect                        Dateisystem schreibgeschützt einhängen\n"
        u8"  --dokan-mount-manager                        Volume beim Windows-Bereitstellungs-Manager\n"
        u8"                                               registrieren (aktiviert Papierkorb usw.)\n"
        u8"  --dokan-current-session                      Volume nur in aktueller Sitzung sichtbar machen\n"
        u8"  --dokan-filelock-user-mode                   LockFile/UnlockFile im Benutzermodus behandeln\n"
        u8"  --dokan-enable-unmount-network-drive         Aushängen von Netzlaufwerken über Explorer erlauben\n"
        u8"  --dokan-dispatch-driver-logs                 Kernel-Treiber-Logs an Userland weiterleiten (langsam)\n"
        u8"  --dokan-allow-ipc-batching                   IPC-Batching für langsame Dateisysteme aktivieren\n"
        u8"  --public                                     Aufrufenden Benutzer in CreateFile imitieren\n"
        u8"                                               (erfordert Administratorrechte)\n"
        u8"  --allocation-unit-size <Bytes>               Zuordnungseinheitsgröße des Volumes\n"
        u8"  --sector-size <Bytes>                        Sektorgröße des Volumes\n"
        u8"  --volume-name <Name>                         Volume-Name im Explorer (Standard: EncFS)\n"
        u8"  --volume-serial <hex>                        Volume-Seriennummer in Hex (Standard: vom Basis)\n"
        u8"  --paranoia                                   AES-256-Verschlüsselung, umbenannte IVs und\n"
        u8"                                               externe IV-Verkettung aktivieren\n"
        u8"  --alt-stream                                 NTFS-Alternative Datenströme aktivieren\n"
        u8"  --case-insensitive                           Dateinamen ohne Groß-/Kleinschreibung abgleichen\n"
        u8"  --reverse                                    Umkehrmodus: Klartext-rootDir verschlüsselt\n"
        u8"                                               am mountPoint anzeigen\n"
        u8"\n"
        u8"Beispiele:\n"
        u8"  encfs.exe C:\\Users M:                                    # C:\\Users als Laufwerk M:\\ einhängen\n"
        u8"  encfs.exe C:\\Users C:\\mount\\dokan                       # In NTFS-Ordner einhängen\n"
        u8"  encfs.exe C:\\Users M: --dokan-network \\\\myfs\\share       # Als Netzlaufwerk mit UNC\n"
        u8"  encfs.exe C:\\Data M: --volume-name \"Sicheres Laufwerk\"   # Mit benutzerdefiniertem Namen\n"
        u8"  encfs.exe C:\\Data M: --use-credential                    # Gespeichertes Passwort verwenden\n"
        u8"  encfs.exe C:\\Data M: --use-credential-once               # Passwort verwenden (danach löschen)\n"
        u8"\n"
        u8"Zum Aushängen drücken Sie Ctrl+C in dieser Konsole oder führen Sie aus:\n"
        u8"  encfs.exe -u <mountPoint>\n";
}

inline const char* GetUsageText() {
    switch (g_CurrentLanguage) {
        case Language::Japanese:
            return GetUsageText_JA();
        case Language::Korean:
            return GetUsageText_KO();
        case Language::ChineseSimplified:
            return GetUsageText_ZH();
        case Language::ChineseTraditional:
            return GetUsageText_ZH_TW();
        case Language::Russian:
            return GetUsageText_RU();
        case Language::Arabic:
            return GetUsageText_AR();
        case Language::German:
            return GetUsageText_DE();
        default:
            return GetUsageText_EN();
    }
}

// ============================================================================
// Usage Text with Version Header
// ============================================================================

// Storage for formatted usage text with version
static std::string g_FormattedUsageText;

/**
 * @brief Gets usage text with version header prepended
 */
inline const char* GetUsageTextWithVersion() {
    std::string header = "EncFSy " + g_Version + "\n\n";
    g_FormattedUsageText = header + GetUsageText();
    return g_FormattedUsageText.c_str();
}

// ============================================================================
// Individual Messages
// ============================================================================

inline const char* MSG_UNKNOWN_COMMAND() {
    switch (g_CurrentLanguage) {
        case Language::Japanese:
            return u8"不明なコマンド: %s\n";
        case Language::Korean:
            return u8"알 수 없는 명령: %s\n";
        case Language::ChineseSimplified:
            return u8"未知命令: %s\n";
        case Language::ChineseTraditional:
            return u8"未知命令: %s\n";
        case Language::Russian:
            return u8"Неизвестная команда: %s\n";
        case Language::Arabic:
            return u8"أمر غير معروف: %s\n";
        case Language::German:
            return u8"Unbekannter Befehl: %s\n";
        default:
            return "unknown command: %s\n";
    }
}

inline const char* MSG_CONFIG_NOT_EXIST() {
    switch (g_CurrentLanguage) {
        case Language::Japanese:
            return u8"EncFS 設定ファイルが存在しません。\n";
        case Language::Korean:
            return u8"EncFS 설정 파일이 존재하지 않습니다.\n";
        case Language::ChineseSimplified:
            return u8"EncFS 配置文件不存在。\n";
        case Language::ChineseTraditional:
            return u8"EncFS 設定檔不存在。\n";
        case Language::Russian:
            return u8"Файл конфигурации EncFS не существует.\n";
        case Language::Arabic:
            return u8"ملف تكوين EncFS غير موجود.\n";
        case Language::German:
            return u8"EncFS-Konfigurationsdatei existiert nicht.\n";
        default:
            return "EncFS configuration file doesn't exist.\n";
    }
}

inline const char* MSG_ENTER_NEW_PASSWORD() {
    switch (g_CurrentLanguage) {
        case Language::Japanese:
            return u8"新しいパスワードを入力: ";
        case Language::Korean:
            return u8"새 비밀번호 입력: ";
        case Language::ChineseSimplified:
            return u8"输入新密码: ";
        case Language::ChineseTraditional:
            return u8"輸入新密碼: ";
        case Language::Russian:
            return u8"Введите новый пароль: ";
        case Language::Arabic:
            return u8"أدخل كلمة مرور جديدة: ";
        case Language::German:
            return u8"Neues Passwort eingeben: ";
        default:
            return "Enter new password: ";
    }
}

inline const char* MSG_ENTER_PASSWORD() {
    switch (g_CurrentLanguage) {
        case Language::Japanese:
            return u8"パスワードを入力: ";
        case Language::Korean:
            return u8"비밀번호 입력: ";
        case Language::ChineseSimplified:
            return u8"输入密码: ";
        case Language::ChineseTraditional:
            return u8"輸入密碼: ";
        case Language::Russian:
            return u8"Введите пароль: ";
        case Language::Arabic:
            return u8"أدخل كلمة المرور: ";
        case Language::German:
            return u8"Passwort eingeben: ";
        default:
            return "Enter password: ";
    }
}

inline const char* MSG_CREDENTIAL_NOT_FOUND() {
    switch (g_CurrentLanguage) {
        case Language::Japanese:
            return u8"エラー: このボリュームの保存済みパスワードが Credential Manager に見つかりません。\n"
                   u8"まず GUI で「Remember Password」をチェックしてパスワードを保存するか、\n"
                   u8"--use-credential オプションなしで実行してください。\n";
        case Language::Korean:
            return u8"오류: 이 볼륨에 대해 Credential Manager에 저장된 비밀번호를 찾을 수 없습니다.\n"
                   u8"먼저 GUI에서 \"Remember Password\"를 체크하여 비밀번호를 저장하거나,\n"
                   u8"--use-credential 옵션 없이 실행하세요.\n";
        case Language::ChineseSimplified:
            return u8"错误: 在凭据管理器中找不到此卷的已保存密码。\n"
                   u8"请先使用 GUI 勾选「记住密码」来保存密码，\n"
                   u8"或不使用 --use-credential 选项运行。\n";
        case Language::ChineseTraditional:
            return u8"錯誤: 在認證管理員中找不到此磁碟區的已儲存密碼。\n"
                   u8"請先使用 GUI 勾選「記住密碼」來儲存密碼，\n"
                   u8"或不使用 --use-credential 選項執行。\n";
        case Language::Russian:
            return u8"Ошибка: Сохранённый пароль для этого тома не найден в Диспетчере учётных данных.\n"
                   u8"Сначала сохраните пароль через GUI (отметьте «Remember Password»),\n"
                   u8"или запустите без опции --use-credential.\n";
        case Language::Arabic:
            return u8"خطأ: لم يتم العثور على كلمة مرور محفوظة لهذا المجلد في مدير بيانات الاعتماد.\n"
                   u8"يرجى استخدام واجهة المستخدم الرسومية لحفظ كلمة المرور أولاً،\n"
                   u8"أو التشغيل بدون خيار --use-credential.\n";
        case Language::German:
            return u8"Fehler: Kein gespeichertes Passwort für dieses Volume in der Anmeldeinformationsverwaltung gefunden.\n"
                   u8"Bitte speichern Sie zuerst ein Passwort über die GUI (\"Remember Password\" aktivieren),\n"
                   u8"oder starten Sie ohne die Option --use-credential.\n";
        default:
            return "Error: No stored password found in Credential Manager for this volume.\n"
                   "Please use the GUI to save a password first, or run without --use-credential.\n";
    }
}

} // namespace EncFSMessages
