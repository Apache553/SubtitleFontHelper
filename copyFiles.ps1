$scriptpath = $MyInvocation.MyCommand.Path
$scriptdir = Split-Path $scriptpath
$configuration = $env:BUILD_CONFIGURATION
if($configuration -eq $null)
{
	$configuration = "Release"
}

Set-Location $scriptdir
New-Item ReleaseBuild -ItemType Directory
Set-Location ReleaseBuild

Copy-Item ../event.man .
Copy-Item ../registerETW.ps1 .
Copy-Item ../unregisterETW.ps1 .
Copy-Item ../enableAutoStart.ps1 .
Copy-Item ../disableAutoStart.ps1 .
Copy-Item ../SubtitleFontHelper.example.xml .
Copy-Item ../Build/Win32/$configuration/FontLoadInterceptor32.dll .
Copy-Item ../Build/Win32/$configuration/Generated32.dll .
Copy-Item ../Build/x64/$configuration/FontLoadInterceptor64.dll .
Copy-Item ../Build/x64/$configuration/Generated64.dll .
Copy-Item ../Build/x64/$configuration/FontDatabaseBuilder.exe .
Copy-Item ../Build/x64/$configuration/SubtitleFontAutoLoaderDaemon.exe .
