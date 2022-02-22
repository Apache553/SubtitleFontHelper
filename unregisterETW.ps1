$scriptpath = $MyInvocation.MyCommand.Path
$scriptdir = Split-Path $scriptpath
$etwman = Join-Path $scriptdir "event.man"

$currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if($currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){
    echo "Unregister previous ETW"
    wevtutil um `"$etwman`"
    Start-Sleep -Seconds 3
}else{
    Start-Process "powershell" -ArgumentList "-File",$scriptpath -Verb runAs 
}