$scriptpath = $MyInvocation.MyCommand.Path
$scriptdir = Split-Path $scriptpath
$etwman = Join-Path $scriptdir "event.man"
$exepath = Join-Path $scriptdir "SubtitleFontAutoLoaderDaemon.exe"

$currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if($currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){
    echo "Unregister previous ETW"
    wevtutil um `"$etwman`"

    [XML]$xml = Get-Content $etwman
    $xml.instrumentationManifest.instrumentation.events.provider.resourceFileName = $exepath.ToString()
    $xml.instrumentationManifest.instrumentation.events.provider.messageFileName = $exepath.ToString()
    $xml.Save($etwman)

    echo "Register new ETW"
    wevtutil im `"$etwman`"

    Start-Sleep -Seconds 3
}else{
    Start-Process "powershell" -ArgumentList "-File",$scriptpath -Verb runAs 
}