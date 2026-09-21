$ErrorActionPreference = "Stop"
$src = Join-Path $PSScriptRoot "..\dist"
$dst = "${env:ProgramFiles(x86)}\Winamp\Plugins"
if (-not (Test-Path $dst)) { throw "Winamp Plugins folder not found: $dst" }
foreach ($f in @("gen_spotify.dll", "ml_spotify.dll", "in_winamplify.dll")) {
  $p = Join-Path $src $f
  if (-not (Test-Path $p)) { throw "Build first (msbuild winamplify.sln /p:Configuration=Release /p:Platform=Win32). Missing dist\$f" }
  Copy-Item $p $dst -Force
  Write-Host "Copied $f -> $dst"
}
foreach ($oldName in @("in_wamplify.dll", "in_loosamp.dll")) {
  $old = Join-Path $dst $oldName
  if (Test-Path $old) {
    Remove-Item $old -Force
    Write-Host "Removed leftover $oldName"
  }
}
Write-Host "Restart Winamp."
