param(
  [switch]$Benchmark,
  [switch]$Screenshot
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build\msvc-local'
$qt = Join-Path $root '.tools\Qt\6.10.2\msvc2022_64'
$bin = Join-Path $root 'bin\RelWithDebInfo'
if (-not (Test-Path (Join-Path $build 'CMakeCache.txt'))) {
  throw 'Configure the project first with .\setup\build-local.ps1.'
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmakeBin = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$savedPath = $env:PATH
$savedPlatform = $env:QT_QPA_PLATFORM
$savedPlugins = $env:QT_PLUGIN_PATH
try {
  Remove-Item Env:Path -ErrorAction SilentlyContinue
  $env:PATH = (Join-Path $qt 'bin') + [IO.Path]::PathSeparator + $savedPath
  $env:QT_QPA_PLATFORM = 'windows'
  $env:QT_PLUGIN_PATH = Join-Path $qt 'plugins'
  & (Join-Path $cmakeBin 'cmake.exe') --build $build --config RelWithDebInfo --target taiga-checks taiga-integration-checks --parallel 6
  if ($LASTEXITCODE -ne 0) { throw 'Check build failed.' }
  & (Join-Path $cmakeBin 'ctest.exe') --test-dir $build -C RelWithDebInfo --output-on-failure -R '^taiga-'
  if ($LASTEXITCODE -ne 0) { throw 'Taiga checks failed.' }
  if ($Benchmark) {
    & (Join-Path $bin 'taiga-checks.exe') --benchmark
    if ($LASTEXITCODE -ne 0) { throw 'Statistics benchmark checks failed.' }
  }
  if ($Benchmark -or $Screenshot) {
    $checkArgs = @()
    if ($Benchmark) { $checkArgs += '--benchmark' }
    if ($Screenshot) { $checkArgs += '--screenshot' }
    & (Join-Path $bin 'taiga-integration-checks.exe') @checkArgs
    if ($LASTEXITCODE -ne 0) { throw 'Integration benchmark checks failed.' }
  }
} finally {
  $env:PATH = $savedPath
  $env:QT_QPA_PLATFORM = $savedPlatform
  $env:QT_PLUGIN_PATH = $savedPlugins
}
