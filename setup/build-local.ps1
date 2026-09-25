param(
  [switch]$Run
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tools = Join-Path $root '.tools'
$qt = Join-Path $tools 'Qt\6.10.2\msvc2022_64'
$build = Join-Path $root 'build\msvc-local'
$exe = Join-Path $root 'bin\RelWithDebInfo\Taiga.exe'

Push-Location $root
try {
  if (-not (Test-Path 'deps\anisthesia\CMakeLists.txt')) {
    $git = (Get-Command git.exe -ErrorAction Stop).Source
    $bash = Join-Path (Split-Path (Split-Path $git -Parent) -Parent) 'bin\bash.exe'
    if (-not (Test-Path $bash)) { throw "Git Bash was not found at $bash" }
    & $bash -lc 'git submodule update --init --recursive'
    if ($LASTEXITCODE -ne 0) { throw 'Could not fetch Git submodules.' }
  }

  if (-not (Test-Path (Join-Path $qt 'lib\cmake\Qt6'))) {
    $pythonTarget = Join-Path $tools 'py'
    python -m pip install --target $pythonTarget aqtinstall
    if ($LASTEXITCODE -ne 0) { throw 'Could not install aqtinstall.' }
    $env:PYTHONPATH = $pythonTarget
    $env:APPDATA = Join-Path $tools 'appdata'
    $env:LOCALAPPDATA = $env:APPDATA
    python -m aqt install-qt windows desktop 6.10.2 win64_msvc2022_64 -O (Join-Path $tools 'Qt') --archives qtbase qtsvg qttools --timeout 30
    if ($LASTEXITCODE -ne 0) { throw 'Could not download Qt.' }
    if (Test-Path 'aqtinstall.log') {
      Move-Item -LiteralPath 'aqtinstall.log' -Destination (Join-Path $tools 'aqtinstall.log') -Force
    }
  }

  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (-not (Test-Path $vswhere)) { throw 'Visual Studio Build Tools 2022 is required.' }
  $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if (-not $vs) { throw 'The MSVC C++ build tools are required.' }
  $cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
  if (-not (Test-Path $cmake)) { throw 'CMake is required.' }

  # Some PowerShell environments contain both PATH and Path. MSBuild rejects that pair.
  $savedPath = $env:PATH
  Remove-Item Env:Path -ErrorAction SilentlyContinue
  $env:PATH = $savedPath

  & $cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$qt"
  if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
  & $cmake --build $build --config RelWithDebInfo --target taiga --parallel 6
  if ($LASTEXITCODE -ne 0) { throw 'Taiga build failed.' }

  & (Join-Path $qt 'bin\windeployqt.exe') --release --no-translations $exe
  if ($LASTEXITCODE -ne 0) { throw 'Qt runtime deployment failed.' }

  Write-Host "Built $exe"
  if ($Run) {
    Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Normal
  }
} finally {
  Pop-Location
}
