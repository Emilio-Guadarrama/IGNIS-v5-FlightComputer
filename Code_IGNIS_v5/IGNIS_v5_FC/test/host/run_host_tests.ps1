# Build and run the IGNIS v5 host tests with zig's clang (py -m ziglang c++).
# Usage (from anywhere):  powershell -File test\host\run_host_tests.ps1
# No hardware is touched: these tests only exercise pure C++ logic in src/core.
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$core = Join-Path $here '..\..\src\core'
$out  = Join-Path $here 'build'
New-Item -ItemType Directory -Force $out | Out-Null
$exe  = Join-Path $out 'ignis_host_tests.exe'

$tests = Get-ChildItem (Join-Path $here 'test_*.cpp') | ForEach-Object { $_.FullName }
$srcs  = Get-ChildItem (Join-Path $core '*.cpp') | ForEach-Object { $_.FullName }

& py -m ziglang c++ -std=c++17 -O1 -Wall -Wextra -Werror -o $exe @tests @srcs
if ($LASTEXITCODE -ne 0) { Write-Host 'BUILD FAILED'; exit 2 }
& $exe
exit $LASTEXITCODE
