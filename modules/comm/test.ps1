param([string]$Compiler = 'gcc')
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$compilerPath = (Get-Command $Compiler -ErrorAction Stop).Source
New-Item -ItemType Directory -Path Build -Force | Out-Null
$flags = @('-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ICore/Inc')
& $compilerPath @flags 'Tests/test_comm_protocol.c' 'Core/Src/comm_protocol.c' '-o' 'Build/test_comm_protocol.exe'
if ($LASTEXITCODE -ne 0) { throw 'Protocol test compilation failed.' }
& '.\Build\test_comm_protocol.exe' | Tee-Object -FilePath Build\test_comm_protocol.log
if ($LASTEXITCODE -ne 0) { throw 'Protocol tests failed.' }
& $compilerPath @flags '-ITests/mocks' 'Tests/test_comm_task.c' 'Core/Src/comm_protocol.c' '-o' 'Build/test_comm_task.exe'
if ($LASTEXITCODE -ne 0) { throw 'Comm task test compilation failed.' }
& '.\Build\test_comm_task.exe' | Tee-Object -FilePath Build\test_comm_task.log
if ($LASTEXITCODE -ne 0) { throw 'Comm task tests failed.' }
Write-Output 'COMM TEST PASS: 8 protocol + 18 task groups.'
