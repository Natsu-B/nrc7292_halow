$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
# Developer machine only. The produced portable exe bundles Electron/Node.
& npm.cmd install
if ($LASTEXITCODE -ne 0) { throw 'npm install failed' }
& npm.cmd test
if ($LASTEXITCODE -ne 0) { throw 'receiver tests failed' }
& npm.cmd run dist:win
if ($LASTEXITCODE -ne 0) { throw 'Electron packaging failed' }
Write-Host 'Portable output: receiver/dist. No ffplay/Python/Node install is needed on the receiving PC.'
Write-Host 'This is an unsigned test build. Apply your organization signing/distribution policy.'
