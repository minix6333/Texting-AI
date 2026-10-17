# CI helper: turns the error lines of a log into one GitHub annotation (readable without signing in).
#   ci_errors.ps1 <log> <title> [pattern]
param([string]$Log, [string]$Title, [string]$Pattern = '(fatal )?error [A-Z]*[0-9]+|error MSB|: error|undefined reference|FAILED')
$lines = Select-String -Path $Log -Pattern $Pattern | Select-Object -First 60 | ForEach-Object { $_.Line.Trim() }
if (-not $lines) { $lines = Get-Content $Log -Tail 40 }
$msg = ($lines -join "`n").Replace('%', '%25').Replace("`r", '%0D').Replace("`n", '%0A')
Write-Output "::error title=$Title::$msg"
