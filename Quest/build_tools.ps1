# Builds parh_render, parh_selftest and parh_vectest for the Quest's arm64 (Phase 7): the root project with the NDK,
# run on the headset through adb (Quest/README.md, "On the headset"): the vector test checks the NEON path bit for bit
# against the scalar one on the real instructions, and the renderer's --bench measures what a track or a set costs a
# core of the Quest.   powershell -File Quest\build_tools.ps1
param(
    [string]$Sdk = "C:\Android-Buildtools\sdk",
    [string]$NdkVersion = "27.2.12479018",
    [string]$Config = "Release",
    # Parallel compile jobs. The machine is shared with other builds and the user (house rules: at most 8).
    [int]$Jobs = 8
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ndk = Join-Path $Sdk "ndk\$NdkVersion"
$build = Join-Path $root "build-quest-tools"

& cmake -S $root -B $build -G "Unix Makefiles" `
    -DCMAKE_MAKE_PROGRAM="$ndk\prebuilt\windows-x86_64\bin\make.exe" `
    -DCMAKE_TOOLCHAIN_FILE="$ndk\build\cmake\android.toolchain.cmake" `
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=$Config -DPARH_BUILD_PLUGIN=OFF
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
& cmake --build $build -j $Jobs --target parh_render parh_selftest parh_vectest
if ($LASTEXITCODE -ne 0) { throw "build failed" }

$out = @((Join-Path $build "Tools\render\parh_render"), (Join-Path $build "Tests\parh_selftest"), (Join-Path $build "Tests\parh_vectest"))
foreach ($f in $out) { Write-Host ("{0}  {1:N0} bytes" -f $f, (Get-Item $f).Length) }
Write-Host "on the headset:"
Write-Host "  adb push $build\Tests\parh_vectest $build\Tools\render\parh_render /data/local/tmp/"
Write-Host "  adb shell chmod +x /data/local/tmp/parh_vectest /data/local/tmp/parh_render"
Write-Host "  adb shell /data/local/tmp/parh_vectest"
Write-Host "  adb shell /data/local/tmp/parh_render --seed 5 --dj 24 --bench --quality quest"
