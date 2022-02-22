$scriptpath = $MyInvocation.MyCommand.Path
$scriptdir = Split-Path $scriptpath

Set-Location $scriptdir
New-Item ReleaseBuild -ItemType Directory
Set-Location ReleaseBuild

Copy-Item ../event.man .
Copy-Item ../registerETW.ps1 .
Copy-Item ../unregisterETW.ps1 .
Copy-Item ../Build/Win32/Release/FontLoadInterceptor32.dll .
Copy-Item ../Build/x64/Release/FontLoadInterceptor64.dll .
Copy-Item ../Build/x64/Release/FontDatabaseBuilder.exe .
Copy-Item ../Build/x64/Release/SubtitleFontAutoLoaderDaemon.exe .
