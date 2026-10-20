param([string]$Src, [string]$Dst)
$bad = 0
foreach ($f in 'module.prop','service.sh','config.conf','uninstall.sh') {
  $p = "$Src\$f"
  if (-not (Test-Path $p)) { Write-Host "MISSING: $p"; $bad = 1; continue }
  $t = [IO.File]::ReadAllText($p) -replace "`r`n", "`n"
  [IO.File]::WriteAllText("$Dst\$f", $t, (New-Object Text.UTF8Encoding $false))
  if ([IO.File]::ReadAllBytes("$Dst\$f") -contains 13) { Write-Host "CR left in $f"; $bad = 1 }
}
exit $bad