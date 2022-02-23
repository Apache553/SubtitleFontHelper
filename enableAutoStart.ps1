$shell = New-Object -comObject WScript.Shell
$scriptpath = $MyInvocation.MyCommand.Path
$scriptdir = Split-Path $scriptpath
$etwman = Join-Path $scriptdir "event.man"
$exepath = Join-Path $scriptdir "SubtitleFontAutoLoaderDaemon.exe"

$startupfolder = [Environment]::GetFolderPath("StartUp")
$shortcutpath = Join-Path $startupfolder "SubtitleFontAutoLoaderDaemon.lnk"
$shortcut = $shell.CreateShortcut($shortcutpath)
$shortcut.TargetPath = $exepath
$shortcut.WorkingDirectory = $scriptdir
$shortcut.Save()