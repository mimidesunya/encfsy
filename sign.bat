@echo off
call local.bat

"%WIN_SDK%"\signtool.exe sign -f codesign.pfx -p "%PFX_PASSWORD%" /fd sha256 /tr http://timestamp.comodoca.com/?td=sha256 /td sha256 /as /v "x64\Release\encfs.exe"
