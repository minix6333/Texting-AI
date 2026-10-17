#Requires -Version 5.1
<#
  注音 AI (ZhuyinLLM) for Windows — uninstaller.
      powershell -ExecutionPolicy Bypass -File uninstall.ps1 [-Purge]
  -Purge also deletes your settings and learned words (%LOCALAPPDATA%\ZhuyinLLM).
#>
param([switch]$Purge)
$ErrorActionPreference = 'Continue'

$Clsid = '{1B5BEE20-7F75-40F4-A6F8-1B380B750667}'
$ProfileGuid = '{34D9D8DD-6A8B-4B71-B04B-62BA54162400}'
$Dest = Join-Path $env:ProgramFiles 'ZhuyinLLM'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"")
    if ($Purge) { $argList += '-Purge' }
    Start-Process powershell -Verb RunAs -ArgumentList $argList -Wait
    exit
}

# take it out of the user's keyboards
$tip = "0404:$Clsid$ProfileGuid"
$list = Get-WinUserLanguageList
$changed = $false
foreach ($lang in $list) {
    if ($lang.InputMethodTips -contains $tip) {
        [void]$lang.InputMethodTips.Remove($tip)
        $changed = $true
    }
}
if ($changed) { Set-WinUserLanguageList $list -Force }

Get-Process zhuyinllm-server -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'ZhuyinLLM' -ErrorAction SilentlyContinue

$sys = Join-Path $env:SystemRoot 'System32\regsvr32.exe'
$wow = Join-Path $env:SystemRoot 'SysWOW64\regsvr32.exe'
if (Test-Path (Join-Path $Dest 'ZhuyinLLM-x64.dll')) {
    Start-Process $sys -ArgumentList '/u', '/s', "`"$(Join-Path $Dest 'ZhuyinLLM-x64.dll')`"" -Wait
}
if ((Test-Path $wow) -and (Test-Path (Join-Path $Dest 'ZhuyinLLM-x86.dll'))) {
    Start-Process $wow -ArgumentList '/u', '/s', "`"$(Join-Path $Dest 'ZhuyinLLM-x86.dll')`"" -Wait
}

# Files still loaded by running apps are deleted at the next restart.
Add-Type -Namespace ZhuyinLLM -Name Native -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)]
public static extern bool MoveFileEx(string existing, string replacement, int flags);
'@
$pending = 0
Get-ChildItem $Dest -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object {
    try { Remove-Item $_.FullName -Force -ErrorAction Stop }
    catch {
        [void][ZhuyinLLM.Native]::MoveFileEx($_.FullName, $null, 4)  # MOVEFILE_DELAY_UNTIL_REBOOT
        $pending++
    }
}
Remove-Item $Dest -Recurse -Force -ErrorAction SilentlyContinue

if ($Purge) { Remove-Item (Join-Path $env:LOCALAPPDATA 'ZhuyinLLM') -Recurse -Force -ErrorAction SilentlyContinue }

Write-Host '已移除注音 AI。'
if ($pending -gt 0) { Write-Host "有 $pending 個檔案還在使用中，重新開機後會刪除。" }
