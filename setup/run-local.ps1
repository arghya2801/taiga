$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'bin\RelWithDebInfo\Taiga.exe'

if (-not (Test-Path -LiteralPath $exe)) {
  throw "Taiga is not built. Run .\setup\build-local.ps1 first."
}

Write-Host "Running $exe"
Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe) -Wait
