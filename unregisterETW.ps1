$scriptpath = $MyInvocation.MyCommand.Path
$scriptdir = Split-Path $scriptpath
$etwman = Join-Path $scriptdir "event.man"

$currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if($currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){
    echo "Unregister previous ETW manifest"
    wevtutil um `"$etwman`"
    echo "Done."
    Start-Sleep -Seconds 3
}else{
    Start-Process "powershell.exe" -ArgumentList "-File","`"$scriptpath`"","-ExecutionPolicy","Bypass" -Verb runAs 
}