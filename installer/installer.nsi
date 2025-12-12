; Read version from Version.txt
!define SRCDIR ".."
!searchparse /file "${SRCDIR}\Version.txt" "" VERSION

!include LogicLib.nsh
!include x64.nsh
!include WinVer.nsh
!include MUI2.nsh

Name "EncFSyInstaller ${VERSION}"
BrandingText https://github.com/miyabe/encfsy/
OutFile "EncFSynstall_${VERSION}.exe"
!define MUI_ICON "icon.ico"

InstallDir $PROGRAMFILES32\Zamasoft\EncFSy
RequestExecutionLevel admin
ShowUninstDetails show

; MUI Pages
!insertmacro MUI_PAGE_LICENSE "licdata.rtf"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; MUI Language settings (must be after MUI_PAGE_* macros)
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Japanese"
!insertmacro MUI_LANGUAGE "Korean"
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "TradChinese"
!insertmacro MUI_LANGUAGE "Arabic"
!insertmacro MUI_LANGUAGE "German"
!insertmacro MUI_LANGUAGE "Russian"

; Language strings
LangString MSG_DOKANY_REQUIRED ${LANG_ENGLISH} "EncFSy requires Dokany. Please try again after installed Dokany."
LangString MSG_DOKANY_REQUIRED ${LANG_JAPANESE} "EncFSyÇ…ÇÕDokanyÇ™ïKóvÇ≈Ç∑ÅBDokanyÇÉCÉìÉXÉgÅ[ÉãÇµÇƒÇ©ÇÁçƒìxÇ®ééÇµÇ≠ÇæÇ≥Ç¢ÅB"
LangString MSG_DOKANY_REQUIRED ${LANG_KOREAN} "EncFSy? Dokany? ?????. Dokany? ??? ? ?? ??????."
LangString MSG_DOKANY_REQUIRED ${LANG_SIMPCHINESE} "EncFSy é˘óv DokanyÅB?à¿ëï Dokany ç@èd?ÅB"
LangString MSG_DOKANY_REQUIRED ${LANG_TRADCHINESE} "EncFSy é˘óv DokanyÅBêøà¿Â‰ Dokany å„èdééÅB"
LangString MSG_DOKANY_REQUIRED ${LANG_ARABIC} "????? EncFSy ?????? Dokany. ???? ???????? ??? ???? ??? ????? Dokany."
LangString MSG_DOKANY_REQUIRED ${LANG_GERMAN} "EncFSy erfordert Dokany. Bitte versuchen Sie es erneut, nachdem Sie Dokany installiert haben."
LangString MSG_DOKANY_REQUIRED ${LANG_RUSSIAN} "EncFSy ÑÑÑÇÑuÑqÑÖÑuÑÑ Dokany. ÑPÑÄÑwÑpÑ|ÑÖÑzÑÉÑÑÑp, ÑÅÑÄÑrÑÑÑÄÑÇÑyÑÑÑu ÑÅÑÄÑÅÑçÑÑÑ{ÑÖ ÑÅÑÄÑÉÑ|Ñu ÑÖÑÉÑÑÑpÑ~ÑÄÑrÑ{Ñy Dokany."

LangString DESC_SECTION ${LANG_ENGLISH} "Install EncFSy encrypted filesystem."
LangString DESC_SECTION ${LANG_JAPANESE} "EncFSyà√çÜâªÉtÉ@ÉCÉãÉVÉXÉeÉÄÇÉCÉìÉXÉgÅ[ÉãÇµÇ‹Ç∑ÅB"
LangString DESC_SECTION ${LANG_KOREAN} "EncFSy ??? ?? ???? ?????."
LangString DESC_SECTION ${LANG_SIMPCHINESE} "à¿ëï EncFSy â¡ñßï∂åèån?ÅB"
LangString DESC_SECTION ${LANG_TRADCHINESE} "à¿Â‰ EncFSy â¡ñß?àƒånìùÅB"
LangString DESC_SECTION ${LANG_ARABIC} "????? ???? ??????? ?????? EncFSy."
LangString DESC_SECTION ${LANG_GERMAN} "EncFSy verschl?sseltes Dateisystem installieren."
LangString DESC_SECTION ${LANG_RUSSIAN} "ÑTÑÉÑÑÑpÑ~ÑÄÑrÑyÑÑÑé ÑxÑpÑäÑyÑÜÑÇÑÄÑrÑpÑ~Ñ~ÑÖÑê ÑÜÑpÑzÑ|ÑÄÑrÑÖÑê ÑÉÑyÑÉÑÑÑuÑ}ÑÖ EncFSy."

!macro EncFSyFiles

  SetOutPath $PROGRAMFILES32\Zamasoft\EncFSy
 
    File ${SRCDIR}\x64\Release\encfsw.exe
    File ${SRCDIR}\x64\Release\encfs.exe
    File ${SRCDIR}\Version.txt

  SetShellVarContext all
  CreateDirectory $SMPROGRAMS\EncFSy
  CreateShortCut $SMPROGRAMS\EncFSy\EncFSy.lnk $PROGRAMFILES32\Zamasoft\EncFSy\encfsw.exe
  CreateShortCut $SMPROGRAMS\EncFSy\Uninstall.lnk $PROGRAMFILES32\Zamasoft\EncFSy\EncFSyUninstall.exe

!macroend

!macro EncFSySetup
  WriteUninstaller $PROGRAMFILES32\Zamasoft\EncFSy\EncFSyUninstall.exe

  ; Write the uninstall keys for Windows
  WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\EncFSy" "DisplayName" "EncFSy ${VERSION}"
  WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\EncFSy" "UninstallString" '"$PROGRAMFILES32\Zamasoft\EncFSy\EncFSyUninstall.exe"'
  WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\EncFSy" "NoModify" 1
  WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\EncFSy" "NoRepair" 1

!macroend

Section "EncFSy" section_x64
  !insertmacro EncFSyFiles
  !insertmacro EncFSySetup
SectionEnd

; Section descriptions
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${section_x64} $(DESC_SECTION)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  RMDir /r $PROGRAMFILES32\Zamasoft\EncFSy
  RMDir $PROGRAMFILES32\EncFSy

  SetShellVarContext all
  RMDir /r $SMPROGRAMS\EncFSy

  ; Remove registry keys
  DeleteRegKey HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\EncFSy"
SectionEnd

Function .onInit
  ; Show language selection dialog
  !insertmacro MUI_LANGDLL_DISPLAY

  ${DisableX64FSRedirection}
  IfFileExists "$SYSDIR\drivers\dokan2.sys" Skip
    MessageBox MB_OK $(MSG_DOKANY_REQUIRED)
    ExecShell "open" "https://github.com/dokan-dev/dokany/releases"
    Abort
  Skip:
  IntOp $0 ${SF_SELECTED} | ${SF_RO}
  SectionSetFlags ${section_x64} $0

FunctionEnd

Function .onInstSuccess
  IfSilent noshellopen
    ExecShell "open" "$PROGRAMFILES32\Zamasoft\EncFSy"
  noshellopen:
FunctionEnd

