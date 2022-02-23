$scriptpath = $MyInvocation.MyCommand.Path
$scriptdir = Split-Path $scriptpath
$etwman = Join-Path $scriptdir "event.man"
$exepath = Join-Path $scriptdir "SubtitleFontAutoLoaderDaemon.exe"

$currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if($currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){
    echo "Unregister previous ETW manifest"
    wevtutil um `"$etwman`"

    [XML]$xml = Get-Content $etwman
    $xml.instrumentationManifest.instrumentation.events.provider.resourceFileName = $exepath.ToString()
    $xml.instrumentationManifest.instrumentation.events.provider.messageFileName = $exepath.ToString()
    $xml.Save($etwman)

    icacls $exepath /grant "*S-1-5-19:(RX)"

    echo "Register new ETW manifest"
    wevtutil im `"$etwman`"
    
    echo "Done."

    Start-Sleep -Seconds 3
}else{
    Start-Process "powershell" -ArgumentList "-File",$scriptpath -Verb runAs 
}