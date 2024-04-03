@echo off

set signtool="C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe"

rem replace test-certificate with EV
signtool sign /tr http://timestamp.digicert.com /td sha256 /fd sha256 /n "Nefarius Software Solutions e.U." .\x64\Win10\Scream\Scream.sys
signtool sign /tr http://timestamp.digicert.com /td sha256 /fd sha256 /n "Nefarius Software Solutions e.U." .\ARM64\Win10\Scream\Scream.sys

rem create submission package
makecab.exe /f .\Scream_x64.ddf
makecab.exe /f .\Scream_ARM64.ddf

rem sign submission package
signtool sign /tr http://timestamp.digicert.com /td sha256 /fd sha256 /n "Nefarius Software Solutions e.U." .\disk1\*.cab
