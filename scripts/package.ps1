# Builds a Release and packs a portable zip: dist\Mausely-<version>-win64.zip
# Usage: powershell -ExecutionPolicy Bypass -File scripts\package.ps1 [-BuildDir build]

param([string]$BuildDir = "build")

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

cmake -S . -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "configure failed" }
cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) { throw "build failed" }
ctest --test-dir $BuildDir --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "tests failed" }

$version = (Select-String -Path CMakeLists.txt -Pattern 'project\(mausely VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$stage = Join-Path $root "dist\Mausely-$version"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force "$stage\models" | Out-Null

Copy-Item "$BuildDir\bin\mausely.exe", "$BuildDir\bin\onnxruntime.dll" $stage
Copy-Item "$BuildDir\bin\models\*.onnx" "$stage\models"
Copy-Item README.md, LICENSE, THIRD_PARTY_NOTICES.md, CHANGELOG.md $stage

$zip = Join-Path $root "dist\Mausely-$version-win64.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Write-Host "Created $zip"
