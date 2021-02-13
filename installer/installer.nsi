!define VERSION "0.7.1"

!include LogicLib.nsh
!include x64.nsh
!include WinVer.nsh
!include MUI2.nsh

LoadLanguageFile "${NSISDIR}\Contrib\Language files\English.nlf"

Name "EncFSyInstaller ${VERSION}"
BrandingText https://github.com/miyabe/encfsy/
OutFile "EncFSynstall_${VERSION}.exe"
!define MUI_ICON "icon.ico"

InstallDir $PROGRAMFILES32\Zamasoft\EncFSy
RequestExecutionLevel admin
LicenseData "licdata.rtf"
ShowUninstDetails show

Page license
Page components
Page instfiles

UninstPage uninstConfirm
UninstPage instfiles

!macro EncFSyFiles

  SetOutPath $PROGRAMFILES32\Zamasoft\EncFSy
 
    File ../x64\Release\*.exe

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

Section "Uninstall"
  RMDir /r $PROGRAMFILES32\Zamasoft\EncFSy
  RMDir $PROGRAMFILES32\EncFSy

  SetShellVarContext all
  RMDir /r $SMPROGRAMS\EncFSy

  ; Remove registry keys
  DeleteRegKey HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\EncFSy"
SectionEnd

Function .onInit
  ${DisableX64FSRedirection}
  IfFileExists "$SYSDIR\drivers\dokan1.sys" Skip
    MessageBox MB_OK "EncFSy requires Dokany. Please try again after installed Dokany."
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

