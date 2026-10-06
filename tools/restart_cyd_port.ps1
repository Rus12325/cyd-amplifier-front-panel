# Bring the CYD's CH340 serial port back to life.
#
# Two failure modes look identical from the outside - "the port is busy" from
# esptool, "port not found" from .NET - and neither one means a program is
# holding it:
#
#   problem 22  the device is disabled (this is what an interrupted device
#               restart leaves behind); every tool reports the port as missing
#   other       the driver is wedged; a restart is the cure
#
# Needs administrator rights:
#
#     powershell -NoProfile -ExecutionPolicy Bypass -File tools\restart_cyd_port.ps1
#
# Edit $Match if your board has a different USB-serial chip.
param(
    [string]$Match = '*CH340*'
)

$ErrorActionPreference = 'Continue'
$devices = @(Get-PnpDevice -Class Ports | Where-Object { $_.FriendlyName -like $Match })

if ($devices.Count -eq 0) {
    Write-Output "no serial port matching '$Match' - is the board plugged in?"
    exit 1
}

foreach ($dev in $devices) {
    $code = (Get-PnpDeviceProperty -InstanceId $dev.InstanceId `
             -KeyName 'DEVPKEY_Device_ProblemCode' -ErrorAction SilentlyContinue).Data
    Write-Output ("{0}`n  {1}`n  status={2} problem={3}" -f `
                  $dev.FriendlyName, $dev.InstanceId, $dev.Status, $code)

    if ($code -eq 22) {
        Write-Output "  disabled -> enabling"
        Enable-PnpDevice -InstanceId $dev.InstanceId -Confirm:$false
    } else {
        Write-Output "  wedged -> restarting"
        & "C:\Windows\System32\pnputil.exe" /restart-device $dev.InstanceId | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Write-Output "  pnputil wants a reboot, trying disable/enable"
            Disable-PnpDevice -InstanceId $dev.InstanceId -Confirm:$false
            Start-Sleep -Seconds 2
            Enable-PnpDevice -InstanceId $dev.InstanceId -Confirm:$false
        }
    }

    Start-Sleep -Seconds 2
    $after = Get-PnpDevice -InstanceId $dev.InstanceId
    $code2 = (Get-PnpDeviceProperty -InstanceId $dev.InstanceId `
              -KeyName 'DEVPKEY_Device_ProblemCode' -ErrorAction SilentlyContinue).Data
    Write-Output ("  now: status={0} problem={1}" -f $after.Status, $code2)
}
