#Requires -Version 5.1
<#
  注音 AI (ZhuyinLLM) for Windows — installer.

  Run from the folder that holds ZhuyinLLM-x64.dll, ZhuyinLLM-x86.dll, zhuyinllm-server.exe and data\:
      powershell -ExecutionPolicy Bypass -File install.ps1
  Options:
      -SkipModel     do not download the AI model (dictionary only, about 400 MB less)
      -AddKeyboard   also add 注音 AI to the Chinese (Taiwan) keyboards of this user
#>
param(
    [switch]$SkipModel,
    [switch]$AddKeyboard
)
$ErrorActionPreference = 'Stop'

$ModelUrl = 'https://huggingface.co/unsloth/Qwen3-0.6B-GGUF/resolve/50968a4468ef4233ed78cd7c3de230dd1d61a56b/Qwen3-0.6B-Q4_K_M.gguf'
$ModelSha256 = 'ac2d97712095a558e31573f62f466a3f9d93990898b0ec79d7c974c1780d524a'
$Clsid = '{1B5BEE20-7F75-40F4-A6F8-1B380B750667}'
$ProfileGuid = '{34D9D8DD-6A8B-4B71-B04B-62BA54162400}'
$Dest = Join-Path $env:ProgramFiles 'ZhuyinLLM'
$Src = $PSScriptRoot

# Text services are registered for the whole machine: this needs administrator rights.
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"")
    if ($SkipModel) { $argList += '-SkipModel' }
    if ($AddKeyboard) { $argList += '-AddKeyboard' }
    Start-Process powershell -Verb RunAs -ArgumentList $argList -Wait
    exit
}

foreach ($f in 'ZhuyinLLM-x64.dll', 'zhuyinllm-server.exe', 'data\tsi.csv') {
    if (-not (Test-Path (Join-Path $Src $f))) { throw "找不到 $f（請在解壓縮後的資料夾執行 install.ps1）" }
}

Write-Host '注音 AI：安裝到' $Dest
Get-Process zhuyinllm-server -ErrorAction SilentlyContinue | Stop-Process -Force
New-Item -ItemType Directory -Force -Path $Dest, (Join-Path $Dest 'data') | Out-Null

# A DLL that apps still have loaded cannot be overwritten, but it can be renamed.
function Copy-Replacing([string]$from, [string]$to) {
    if (Test-Path $to) {
        try { Remove-Item $to -Force }
        catch {
            $old = "$to.$([DateTime]::Now.Ticks).old"
            Rename-Item $to $old -Force
        }
    }
    Copy-Item $from $to -Force
}
Get-ChildItem $Dest -Filter '*.old' -ErrorAction SilentlyContinue | ForEach-Object { Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue }

foreach ($f in 'ZhuyinLLM-x64.dll', 'ZhuyinLLM-x86.dll', 'zhuyinllm-server.exe') {
    if (Test-Path (Join-Path $Src $f)) { Copy-Replacing (Join-Path $Src $f) (Join-Path $Dest $f) }
}
Copy-Item (Join-Path $Src 'data\*') (Join-Path $Dest 'data') -Force
foreach ($f in 'uninstall.ps1', 'README.md', 'THIRD_PARTY_NOTICES.md') {
    if (Test-Path (Join-Path $Src $f)) { Copy-Item (Join-Path $Src $f) $Dest -Force }
}

# The model (Qwen3-0.6B, Q4_K_M, ~380 MB), pinned to one revision and checked.
$model = Join-Path $Dest 'model.gguf'
if (-not $SkipModel -and -not (Test-Path $model)) {
    $tmp = Join-Path $env:TEMP 'zhuyinllm-model.gguf'
    Write-Host '下載 AI 模型（約 380 MB）…'
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -Uri $ModelUrl -OutFile $tmp -UseBasicParsing
    $hash = (Get-FileHash $tmp -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $ModelSha256) {
        Remove-Item $tmp -Force
        throw "模型檔的 SHA-256 不符（$hash），已刪除。"
    }
    Move-Item $tmp $model -Force
}

# Register the text service: 64-bit apps, and 32-bit apps on 64-bit Windows.
$sys = Join-Path $env:SystemRoot 'System32\regsvr32.exe'
$wow = Join-Path $env:SystemRoot 'SysWOW64\regsvr32.exe'
$p = Start-Process $sys -ArgumentList '/s', "`"$(Join-Path $Dest 'ZhuyinLLM-x64.dll')`"" -Wait -PassThru
if ($p.ExitCode -ne 0) { throw "註冊 ZhuyinLLM-x64.dll 失敗（$($p.ExitCode)）" }
if ((Test-Path $wow) -and (Test-Path (Join-Path $Dest 'ZhuyinLLM-x86.dll'))) {
    $p = Start-Process $wow -ArgumentList '/s', "`"$(Join-Path $Dest 'ZhuyinLLM-x86.dll')`"" -Wait -PassThru
    if ($p.ExitCode -ne 0) { Write-Warning "註冊 32 位元版失敗（$($p.ExitCode)）：32 位元的 App 裡不能用" }
}

# The engine starts at sign-in (the text service also starts it when it is not running).
$server = Join-Path $Dest 'zhuyinllm-server.exe'
Set-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'ZhuyinLLM' -Value "`"$server`""
# start it now, without administrator rights (through Explorer)
Start-Process explorer.exe -ArgumentList "`"$server`""

if ($AddKeyboard) {
    $tip = "0404:$Clsid$ProfileGuid"
    $list = Get-WinUserLanguageList
    $zh = $list | Where-Object { $_.LanguageTag -eq 'zh-Hant-TW' }
    if (-not $zh) {
        $list.Add('zh-Hant-TW')
        $zh = $list | Where-Object { $_.LanguageTag -eq 'zh-Hant-TW' }
    }
    if ($zh.InputMethodTips -notcontains $tip) { $zh.InputMethodTips.Add($tip) }
    Set-WinUserLanguageList $list -Force
}

Write-Host ''
Write-Host '安裝完成。'
if (-not $AddKeyboard) {
    Write-Host '到「設定 > 時間與語言 > 語言與地區 > 中文(台灣) > 語言選項 > 鍵盤 > 新增鍵盤」加入「注音 AI」，'
    Write-Host '再用 Win+Space 切換到它。'
}
Write-Host '單按 Shift 切換中／英（所有視窗共用同一個模式）；Shift+Space 切換全形／半形。'
