# Who has the CYD serial port open?
#
# Useful when esptool says "could not open COMx" and killing the obvious
# programs does not help - but note that a *disabled* or wedged device looks the
# same from the outside and is not held by anybody, so check
# tools/restart_cyd_port.ps1 first.
#
# One-time setup (Sysinternals Handle, official download):
#     python -c "import urllib.request,zipfile,io;zipfile.ZipFile(io.BytesIO(urllib.request.urlopen('https://download.sysinternals.com/files/Handle.zip').read())).extractall('tools')"
#
# Then, with administrator rights (needs elevation to see other users' handles):
#     powershell -NoProfile -ExecutionPolicy Bypass -File tools\find_port_holder.ps1
param(
    [string]$Pattern = '1A86|7523|Serial|COM5|CH34|usbser|VCP|USBPDO'
)

$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot 'handle64.exe'
$dump = Join-Path $PSScriptRoot 'handles.txt'

if (-not (Test-Path $exe)) {
    Write-Output "handle64.exe is missing next to this script - see the header"
    exit 1
}

& $exe -accepteula -nobanner -a | Out-File -Encoding utf8 $dump
Write-Output ("handle dump: {0} lines" -f (Get-Content $dump).Count)

# handle.exe prints a header line per process followed by its handles, so the
# owner has to be tracked while scanning.
$owner = '?'
$matches = 0
foreach ($line in Get-Content $dump) {
    if ($line -match '^(.+?)\s+pid:\s*(\d+)') {
        $owner = '{0} pid {1}' -f $Matches[1].Trim(), $Matches[2]
        continue
    }
    if ($line -match $Pattern -and $line -notmatch 'handle(64|64a)?\.exe') {
        Write-Output ("  {0}: {1}" -f $owner, $line.Trim())
        $matches++
    }
}
Write-Output "matches: $matches"
Write-Output "no matches means nothing holds the port - then it is the device state"
