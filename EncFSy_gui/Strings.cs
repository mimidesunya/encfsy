using System;
using System.Collections.Generic;
using System.Globalization;

namespace EncFSy_gui
{
    public static class Strings
    {
        private static string _currentLanguage = "en";

        // Language display names and codes
        public static readonly Dictionary<string, string> AvailableLanguages = new Dictionary<string, string>
        {
            { "en", "English" },
            { "ja", "日本語" },
            { "ko", "한국어" },
            { "zh-CN", "简体中文" },
            { "zh-TW", "繁體中文" },
            { "ru", "Русский" },
            { "ar", "العربية" },
            { "de", "Deutsch" }
        };

        public static string CurrentLanguage { get { return _currentLanguage; } }

        public static void InitLanguage()
        {
            var culture = CultureInfo.CurrentUICulture;
            string langCode = culture.TwoLetterISOLanguageName.ToLower();

            switch (langCode)
            {
                case "ja": _currentLanguage = "ja"; break;
                case "ko": _currentLanguage = "ko"; break;
                case "zh":
                    if (culture.Name.Contains("TW") || culture.Name.Contains("HK") || culture.Name.Contains("MO"))
                        _currentLanguage = "zh-TW";
                    else
                        _currentLanguage = "zh-CN";
                    break;
                case "ru": _currentLanguage = "ru"; break;
                case "ar": _currentLanguage = "ar"; break;
                case "de": _currentLanguage = "de"; break;
                default: _currentLanguage = "en"; break;
            }
        }

        public static void SetLanguage(string lang)
        {
            if (AvailableLanguages.ContainsKey(lang))
                _currentLanguage = lang;
        }

        private static string L(string en, string ja, string ko, string zhCN, string zhTW, string ru, string ar, string de)
        {
            switch (_currentLanguage)
            {
                case "ja": return ja;
                case "ko": return ko;
                case "zh-CN": return zhCN;
                case "zh-TW": return zhTW;
                case "ru": return ru;
                case "ar": return ar;
                case "de": return de;
                default: return en;
            }
        }

        // Window Titles
        public static string AppTitle { get { return "EncFSy"; } }
        public static string PasswordFormTitle { get { return L("Enter Password", "パスワード入力", "비밀번호 입력", "输入密码", "輸入密碼", "Введите пароль", "أدخل كلمة المرور", "Passwort eingeben"); } }

        // Group Box Titles
        public static string DriveSelection { get { return L("Drive Selection", "ドライブ選択", "드라이브 선택", "驱动器选择", "磁碟機選擇", "Выбор диска", "اختيار محرك الأقراص", "Laufwerksauswahl"); } }
        public static string EncryptedDirectory { get { return L("Encrypted Directory (rootDir)", "暗号化ディレクトリ (rootDir)", "암호화 디렉토리 (rootDir)", "加密目录 (rootDir)", "加密目錄 (rootDir)", "Зашифрованный каталог (rootDir)", "المجلد المشفر (rootDir)", "Verschlüsseltes Verzeichnis (rootDir)"); } }
        public static string Options { get { return L("Options", "オプション", "옵션", "选项", "選項", "Параметры", "الخيارات", "Optionen"); } }
        public static string AdvancedOptions { get { return L("Advanced Options", "詳細オプション", "고급 옵션", "高级选项", "進階選項", "Дополнительные параметры", "خيارات متقدمة", "Erweiterte Optionen"); } }
        public static string Settings { get { return L("Settings", "設定", "설정", "设置", "設定", "Настройки", "الإعدادات", "Einstellungen"); } }
        public static string CommandPreview { get { return L("Command Preview", "コマンドプレビュー", "명령 미리보기", "命令预览", "命令預覽", "Предпросмотр команды", "معاينة الأمر", "Befehlsvorschau"); } }

        // Buttons
        public static string Mount { get { return L("Mount", "マウント", "마운트", "挂载", "掛載", "Монтировать", "تركيب", "Einhängen"); } }
        public static string Unmount { get { return L("Unmount", "アンマウント", "언마운트", "卸载", "卸載", "Размонтировать", "إلغاء التركيب", "Aushängen"); } }
        public static string Browse { get { return L("Browse...", "参照...", "찾아보기...", "浏览...", "瀏覽...", "Обзор...", "استعراض...", "Durchsuchen..."); } }
        public static string Refresh { get { return L("Refresh", "更新", "새로고침", "刷新", "重新整理", "Обновить", "تحديث", "Aktualisieren"); } }
        public static string Copy { get { return L("Copy", "コピー", "복사", "复制", "複製", "Копировать", "نسخ", "Kopieren"); } }
        public static string OK { get { return L("OK", "OK", "확인", "确定", "確定", "ОК", "موافق", "OK"); } }
        public static string Cancel { get { return L("Cancel", "キャンセル", "취소", "取消", "取消", "Отмена", "إلغاء", "Abbrechen"); } }

        // Checkboxes and Options
        public static string ShowAdvancedOptions { get { return L("Show Advanced Options ▼", "詳細オプションを表示 ▼", "고급 옵션 표시 ▼", "显示高级选项 ▼", "顯示進階選項 ▼", "Показать доп. параметры ▼", "إظهار الخيارات المتقدمة ▼", "Erweiterte Optionen ▼"); } }
        public static string HideAdvancedOptions { get { return L("Hide Advanced Options ▲", "詳細オプションを隠す ▲", "고급 옵션 숨기기 ▲", "隐藏高级选项 ▲", "隱藏進階選項 ▲", "Скрыть доп. параметры ▲", "إخفاء الخيارات المتقدمة ▲", "Erweiterte Optionen ▲"); } }
        public static string AltStream { get { return L("Alt Stream", "代替ストリーム", "대체 스트림", "备用流", "替代資料流", "Альт. потоки", "تدفق بديل", "Alt. Streams"); } }
        public static string MountManager { get { return L("Mount Manager", "マウント管理", "마운트 관리자", "挂载管理器", "掛載管理員", "Диспетчер", "مدير التركيب", "Mount-Manager"); } }
        public static string IgnoreCase { get { return L("Ignore Case", "大小文字無視", "대소문자 무시", "忽略大小写", "忽略大小攝", "Без регистра", "تجاهل الحالة", "Groß-/Klein."); } }
        public static string ReadOnly { get { return L("Read Only", "読み取り専用", "읽기 전용", "只读", "唯讀", "Только чтение", "للقراءة فقط", "Nur Lesen"); } }
        public static string Paranoia { get { return L("Paranoia", "パラノイア", "편집증 모드", "偏执模式", "偏執模式", "Паранойя", "وضع الأمان", "Paranoia"); } }
        public static string Removable { get { return L("Removable", "リムーバブル", "이동식", "可移动", "卸除式", "Съёмный", "قابل للإزالة", "Wechselm."); } }
        public static string CurrentSession { get { return L("Current Session", "現セッション", "현재 세션", "当前会话", "目前工作階段", "Сессия", "الجلسة الحالية", "Sitzung"); } }
        public static string ShowPassword { get { return L("Show Password", "パスワードを表示", "비밀번호 표시", "显示密码", "顯示密碼", "Показать пароль", "إظهار كلمة المرور", "Passwort anzeigen"); } }
        public static string RememberPassword { get { return L("Remember Password", "パスワードを記憶", "비밀번호 기억", "记住密码", "記住密碼", "Запомнить пароль", "تذكر كلمة المرور", "Passwort merken"); } }

        // Labels
        public static string Language { get { return L("Language:", "言語:", "언어:", "语言:", "語言:", "Язык:", "اللغة:", "Sprache:"); } }
        public static string Drive { get { return L("Drive", "ドライブ", "드라이브", "驱动器", "磁碟機", "Диск", "محرك الأقراص", "Laufwerk"); } }
        public static string Status { get { return L("Status", "状態", "상태", "状态", "狀態", "Статус", "الحالة", "Status"); } }
        public static string Password { get { return L("Password:", "パスワード:", "비밀번호:", "密码:", "密碼:", "Пароль:", "كلمة المرور:", "Passwort:"); } }
        public static string Timeout { get { return L("Timeout:", "タイムアウト:", "타임아웃:", "超时:", "逾時:", "Таймаут:", "مهلة:", "Timeout:"); } }
        public static string VolumeName { get { return L("Name:", "名前:", "이름:", "名称:", "名稱:", "Имя:", "الاسم:", "Name:"); } }
        public static string VolumeSerial { get { return L("Serial:", "シリアル:", "시리얼:", "序列号:", "序號:", "Серийный:", "التسلسلي:", "Serial:"); } }

        // Messages
        public static string Error { get { return L("Error", "エラー", "오류", "错误", "錯誤", "Ошибка", "خطأ", "Fehler"); } }
        public static string SelectDriveError { get { return L("Please select a drive letter.", "ドライブ文字を選択してください。", "드라이브 문자를 선택하세요.", "请选择驱动器号。", "請選擇磁碟機代號。", "Выберите букву диска.", "يرجى اختيار حرف محرك الأقراص.", "Bitte Laufwerksbuchstaben wählen."); } }
        public static string PasswordEmptyError { get { return L("Password cannot be empty.", "パスワードは空にできません。", "비밀번호는 비워둘 수 없습니다.", "密码不能为空。", "密碼不能為空。", "Пароль не может быть пустым.", "لا يمكن أن تكون كلمة المرور فارغة.", "Passwort darf nicht leer sein."); } }
        public static string MountFailed { get { return L("Mount failed: {0}", "マウント失敗: {0}", "마운트 실패: {0}", "挂载失败: {0}", "掛載失敗: {0}", "Ошибка монтирования: {0}", "فشل التركيب: {0}", "Einhängen fehlgeschlagen: {0}"); } }
        public static string MountResult { get { return L("Mount result: '{0}'", "マウント結果: '{0}'", "마운트 결과: '{0}'", "挂载结果: '{0}'", "掛載結果: '{0}'", "Результат: '{0}'", "نتيجة التركيب: '{0}'", "Ergebnis: '{0}'"); } }
        public static string Copied { get { return L("Copied!", "コピーしました！", "복사됨!", "已复制！", "已複製！", "Скопировано!", "تم النسخ!", "Kopiert!"); } }
        public static string SelectEncryptedDirectory { get { return L("Select the encrypted directory (rootDir)", "暗号化ディレクトリ (rootDir) を選択", "암호화 디렉토리 (rootDir) 선택", "选择加密目录 (rootDir)", "選擇加密目錄 (rootDir)", "Выберите зашифрованный каталог", "حدد المجلد المشفر (rootDir)", "Verschlüsseltes Verzeichnis wählen"); } }

        // Tooltips
        public static string TooltipRefresh { get { return L("Refresh drive list", "ドライブ一覧を更新", "드라이브 목록 새로고침", "刷新驱动器列表", "重新整理磁碟機清單", "Обновить список дисков", "تحديث قائمة محركات الأقراص", "Laufwerksliste aktualisieren"); } }
        public static string TooltipAltStream { get { return L("Enable NTFS alternate data streams", "NTFS 代替データストリームを有効化", "NTFS 대체 데이터 스트림 활성화", "启用 NTFS 备用数据流", "啟用 NTFS 替代資料流", "Включить альт. потоки данных NTFS", "تمكين تدفقات البيانات البديلة NTFS", "NTFS-alt. Datenströme aktivieren"); } }
        public static string TooltipMountManager { get { return L("Register with Windows Mount Manager (enables Recycle Bin)", "Windows Mount Manager に登録（ごみ箱を有効化）", "Windows Mount Manager에 등록 (휴지통 활성화)", "向 Windows Mount Manager 注册（启用回收站）", "向 Windows Mount Manager 註冊（啟用資源回收筒）", "Зарегистрировать в диспетчере (корзина)", "التسجيل مع مدير التركيب (سلة المحذوفات)", "Beim Mount-Manager registrieren (Papierkorb)"); } }
        public static string TooltipParanoia { get { return L("AES-256, renamed IVs, external IV chaining", "AES-256、リネーム IV、外部 IV チェーン", "AES-256, 이름 변경 IV, 외부 IV 체인", "AES-256、重命名 IV、外部 IV 链", "AES-256、重新命名 IV、外部 IV 鏈", "AES-256, переименование IV", "AES-256، إعادة تسمية IV، سلسلة IV", "AES-256, umbenannte IVs, ext. IV-Verkettung"); } }
        public static string TooltipLanguage { get { return L("Select display language", "表示言語を選択", "표시 언어 선택", "选择显示语言", "選擇顯示語言", "Выберите язык интерфейса", "اختر لغة العرض", "Anzeigesprache auswählen"); } }
    }
}
