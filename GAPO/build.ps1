param([switch]$Test)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$compiler = Get-Command gcc -ErrorAction SilentlyContinue
if ($compiler) {
    $compilerPath = $compiler.Source
} elseif (Test-Path -LiteralPath 'C:\msys64\ucrt64\bin\gcc.exe') {
    $compilerPath = 'C:\msys64\ucrt64\bin\gcc.exe'
} else {
    throw 'GCC was not found. Install MSYS2 UCRT64 GCC or add GCC to PATH.'
}
$env:PATH = (Split-Path -Parent $compilerPath) + ';' + $env:PATH
$flags = @('-Wall', '-Wextra', '-Wpedantic', '-std=c11')
& $compilerPath @flags server.c game.c net.c -o server.exe -lws2_32
if ($LASTEXITCODE) { throw 'Server build failed.' }
& $compilerPath @flags client.c net.c -o client.exe -lws2_32
if ($LASTEXITCODE) { throw 'Client build failed.' }
Write-Host 'Built server.exe and client.exe.'
if ($Test) {
    & $compilerPath @flags tests.c game.c -o tests.exe
    if ($LASTEXITCODE) { throw 'Test build failed.' }
    & .\tests.exe
    if ($LASTEXITCODE) { throw 'Game tests failed.' }
    python integration_test.py
    if ($LASTEXITCODE) { throw 'Socket integration test failed.' }
}
