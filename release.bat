@echo off
setlocal EnableExtensions

cd /d "%~dp0"

if exist "%~dp0local.bat" call "%~dp0local.bat"

set "ROOT=%~dp0"
set "CONFIG=Release"
set "PLATFORM=x64"
set "GUI_PLATFORM=AnyCPU"
if not defined SIGN_CERT_FILE set "SIGN_CERT_FILE=%ROOT%codesign.pfx"
set "CERT_FILE=%SIGN_CERT_FILE%"
set "INSTALLER_SCRIPT=%ROOT%installer\installer.nsi"
set "TIMESTAMP_URL=http://timestamp.digicert.com"
set "ENCFS_EXE=%ROOT%x64\Release\encfs.exe"
set "ENCFSW_EXE=%ROOT%x64\Release\encfsw.exe"
set "TEST_EXE=%ROOT%x64\Release\test.exe"
if not defined TEST_LOG_DIR set "TEST_LOG_DIR=%ROOT%x64\Release\test-logs"
if not defined TEST_TIMEOUT_SECONDS set "TEST_TIMEOUT_SECONDS=600"
if not defined TEST_ARGS set "TEST_ARGS=-s"
if not defined CASE_TEST_ARGS set "CASE_TEST_ARGS=--case-insensitive -s -c basic"
if /i "%SKIP_CASE_TESTS%"=="1" set "CASE_TEST_ARGS="

call :resolve_msbuild || exit /b 1
call :resolve_makensis || exit /b 1

set "SIGN_ENABLED=1"
if /i "%SKIP_SIGN%"=="1" set "SIGN_ENABLED="

if defined SIGN_ENABLED (
    if not exist "%CERT_FILE%" (
        echo ERROR: Certificate file not found: "%CERT_FILE%"
        exit /b 1
    )
    call :resolve_password || exit /b 1
    call :validate_certificate || exit /b 1
    call :resolve_signtool || exit /b 1
)

set /p VERSION=<"%ROOT%Version.txt"
if not defined VERSION (
    echo ERROR: Failed to read Version.txt
    exit /b 1
)
set "INSTALLER_EXE=%ROOT%installer\EncFSynstall_%VERSION%.exe"

echo Using MSBuild: "%MSBUILD%"
echo Using makensis: "%MAKENSIS%"
if defined SIGN_ENABLED (
    echo Using signtool: "%SIGNTOOL%"
) else (
    echo Signing disabled because SKIP_SIGN=1.
)
echo.

echo [1/6] Rebuilding encfs.exe
"%MSBUILD%" "%ROOT%EncFSy_console\EncFSy_console.vcxproj" /m /t:Rebuild /p:Configuration=%CONFIG%;Platform=%PLATFORM%;SolutionDir=%ROOT%
if errorlevel 1 exit /b 1

echo.
echo [2/6] Rebuilding encfsw.exe
"%MSBUILD%" "%ROOT%EncFSy_gui\EncFSy_gui.csproj" /m /t:Rebuild /p:Configuration=%CONFIG%;Platform=%GUI_PLATFORM%;SolutionDir=%ROOT%
if errorlevel 1 exit /b 1

echo.
echo [3/6] Rebuilding test.exe
"%MSBUILD%" "%ROOT%EncFSy_test\EncFSy_test.vcxproj" /m /t:Rebuild /p:Configuration=%CONFIG%;Platform=%PLATFORM%;SolutionDir=%ROOT%
if errorlevel 1 exit /b 1

call :require_file "%ENCFS_EXE%" || exit /b 1
call :require_file "%ENCFSW_EXE%" || exit /b 1
call :require_file "%TEST_EXE%" || exit /b 1

echo.
if /i "%SKIP_TESTS%"=="1" (
    echo [4/6] Skipping tests because SKIP_TESTS=1.
) else (
    echo [4/6] Running release tests
    call :run_tests || exit /b 1
)

echo.
if defined SIGN_ENABLED (
    echo [5/6] Signing executables
    call :sign_file "%ENCFS_EXE%" || exit /b 1
    call :sign_file "%ENCFSW_EXE%" || exit /b 1
) else (
    echo [5/6] Skipping executable signing
)

echo.
echo [6/6] Building installer
"%MAKENSIS%" "%INSTALLER_SCRIPT%"
if errorlevel 1 exit /b 1

call :require_file "%INSTALLER_EXE%" || exit /b 1
if defined SIGN_ENABLED (
    echo Signing installer
    call :sign_file "%INSTALLER_EXE%" || exit /b 1
) else (
    echo Skipping installer signing
)

echo.
if defined SIGN_ENABLED (
    echo Signed artifacts:
) else (
    echo Unsigned artifacts:
)
echo   "%ENCFS_EXE%"
echo   "%ENCFSW_EXE%"
echo   "%INSTALLER_EXE%"
exit /b 0

:run_tests
if not exist "%TEST_LOG_DIR%" mkdir "%TEST_LOG_DIR%" || exit /b 1
call :run_test_case release-default "%TEST_ARGS%" || exit /b 1
if defined CASE_TEST_ARGS (
    echo.
    call :run_test_case release-case-insensitive "%CASE_TEST_ARGS%" || exit /b 1
)
exit /b 0

:run_test_case
set "TEST_CASE_NAME=%~1"
set "TEST_CASE_ARGS=%~2"
set "TEST_LOG=%TEST_LOG_DIR%\%TEST_CASE_NAME%.log"
set "TEST_ERR_LOG=%TEST_LOG_DIR%\%TEST_CASE_NAME%.err.log"
del "%TEST_LOG%" "%TEST_ERR_LOG%" >nul 2>nul
echo Running "%TEST_EXE%" %TEST_CASE_ARGS%
echo   Log: "%TEST_LOG%"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$timeout = [int]$env:TEST_TIMEOUT_SECONDS * 1000; $p = Start-Process -FilePath $env:TEST_EXE -ArgumentList $env:TEST_CASE_ARGS -WorkingDirectory (Join-Path $env:ROOT 'x64\Release') -RedirectStandardOutput $env:TEST_LOG -RedirectStandardError $env:TEST_ERR_LOG -PassThru; if (-not $p.WaitForExit($timeout)) { Stop-Process -Id $p.Id -Force; exit 124 }; exit $p.ExitCode"
set "TEST_EXIT=%ERRORLEVEL%"
if not "%TEST_EXIT%"=="0" (
    echo ERROR: Test case failed: %TEST_CASE_NAME% (exit %TEST_EXIT%)
    if exist "%TEST_LOG%" type "%TEST_LOG%"
    if exist "%TEST_ERR_LOG%" type "%TEST_ERR_LOG%"
    exit /b %TEST_EXIT%
)
findstr /C:"Total:" /C:"*** ALL TESTS PASSED ***" "%TEST_LOG%"
if errorlevel 1 type "%TEST_LOG%"
exit /b 0

:resolve_password
set "SIGN_PASSWORD=%PFX_PASSWORD:"=%"
if defined SIGN_PASSWORD exit /b 0
echo ERROR: PFX_PASSWORD is not set. Define it in local.bat.
exit /b 1

:validate_certificate
powershell -NoProfile -ExecutionPolicy Bypass -Command "$cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($env:CERT_FILE, $env:SIGN_PASSWORD); if (-not $cert.HasPrivateKey) { [Console]::Error.WriteLine('ERROR: Code signing certificate has no private key.'); [Environment]::Exit(1) }; if ($cert.NotAfter -le (Get-Date)) { [Console]::Error.WriteLine(('ERROR: Code signing certificate expired on {0:u}. Set SIGN_CERT_FILE to a renewed PFX before releasing.' -f $cert.NotAfter)); [Environment]::Exit(1) }; $eku = $cert.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.37' }; if ($eku -and $eku.Format($false) -notmatch '1\.3\.6\.1\.5\.5\.7\.3\.3|Code Signing') { [Console]::Error.WriteLine('ERROR: Certificate is not valid for code signing.'); [Environment]::Exit(1) }; Write-Host ('Using certificate: {0}; expires {1:u}' -f $cert.Subject, $cert.NotAfter)"
if errorlevel 1 exit /b 1
exit /b 0

:resolve_msbuild
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\amd64\MSBuild.exe`) do (
        if not defined MSBUILD set "MSBUILD=%%I"
    )
    if not defined MSBUILD (
        for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
            if not defined MSBUILD set "MSBUILD=%%I"
        )
    )
)
if not defined MSBUILD (
    for /f "delims=" %%I in ('where msbuild 2^>nul') do (
        if not defined MSBUILD set "MSBUILD=%%I"
    )
)
if defined MSBUILD exit /b 0
echo ERROR: MSBuild.exe was not found.
exit /b 1

:resolve_makensis
if exist "%ProgramFiles(x86)%\NSIS\makensis.exe" set "MAKENSIS=%ProgramFiles(x86)%\NSIS\makensis.exe"
if not defined MAKENSIS if exist "%ProgramFiles%\NSIS\makensis.exe" set "MAKENSIS=%ProgramFiles%\NSIS\makensis.exe"
if not defined MAKENSIS (
    for /f "delims=" %%I in ('where makensis 2^>nul') do (
        if not defined MAKENSIS set "MAKENSIS=%%I"
    )
)
if defined MAKENSIS exit /b 0
echo ERROR: makensis.exe was not found.
exit /b 1

:resolve_signtool
if defined WIN_SDK (
    if exist "%WIN_SDK%\signtool.exe" set "SIGNTOOL=%WIN_SDK%\signtool.exe"
    if not defined SIGNTOOL if exist "%WIN_SDK%\x64\signtool.exe" set "SIGNTOOL=%WIN_SDK%\x64\signtool.exe"
    if not defined SIGNTOOL if exist "%WIN_SDK%\x86\signtool.exe" set "SIGNTOOL=%WIN_SDK%\x86\signtool.exe"
)
if not defined SIGNTOOL (
    for /f "delims=" %%I in ('dir /b /ad "%ProgramFiles(x86)%\Windows Kits\10\bin\10.*" 2^>nul ^| sort /r') do (
        if not defined SIGNTOOL if exist "%ProgramFiles(x86)%\Windows Kits\10\bin\%%I\x64\signtool.exe" set "SIGNTOOL=%ProgramFiles(x86)%\Windows Kits\10\bin\%%I\x64\signtool.exe"
        if not defined SIGNTOOL if exist "%ProgramFiles(x86)%\Windows Kits\10\bin\%%I\x86\signtool.exe" set "SIGNTOOL=%ProgramFiles(x86)%\Windows Kits\10\bin\%%I\x86\signtool.exe"
    )
)
if not defined SIGNTOOL if exist "%ProgramFiles(x86)%\Windows Kits\10\bin\x64\signtool.exe" set "SIGNTOOL=%ProgramFiles(x86)%\Windows Kits\10\bin\x64\signtool.exe"
if not defined SIGNTOOL (
    for /f "delims=" %%I in ('where signtool 2^>nul') do (
        if not defined SIGNTOOL set "SIGNTOOL=%%I"
    )
)
if defined SIGNTOOL exit /b 0
echo ERROR: signtool.exe was not found.
exit /b 1

:require_file
if exist "%~1" exit /b 0
echo ERROR: Expected file not found: "%~1"
exit /b 1

:sign_file
echo Signing "%~1"
"%SIGNTOOL%" sign /f "%CERT_FILE%" /p "%SIGN_PASSWORD%" /fd sha256 /tr "%TIMESTAMP_URL%" /td sha256 /v "%~1"
if errorlevel 1 exit /b 1
exit /b 0

