# Joue des WAV generes par gen.js : play.ps1 <dossier> <nom> [nom...]. A combiner avec --audio-debug.
param([string]$Folder, [Parameter(ValueFromRemainingArguments = $true)][string[]]$Names)
foreach ($f in $Names) {
  Write-Output ((Get-Date -Format 'HH:mm:ss.fff') + ' START ' + $f)
  (New-Object System.Media.SoundPlayer (Join-Path $Folder "$f.wav")).PlaySync()
  Write-Output ((Get-Date -Format 'HH:mm:ss.fff') + ' END ' + $f)
  Start-Sleep -Milliseconds 2500
}
