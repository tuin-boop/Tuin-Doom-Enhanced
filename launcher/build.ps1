$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio 2022 C++ Build Tools are required.' }
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake -S "$root\duct-tape-doom-main" -B "$root\build" -G 'Visual Studio 17 2022' -A x64 -DDOOM_BUILD_SDL=ON -DDOOM_RENDER3D=ON -DDOOM_SOUNDFONT=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DBUILD_TESTING=ON -DDOOM_GAME_DATA= -DDOOM_TEST_GPU=OFF
if ($LASTEXITCODE) { throw 'Configure failed' }
& $cmake --build "$root\build" --config Release --parallel 8
if ($LASTEXITCODE) { throw 'Engine build failed' }
New-Item -ItemType Directory -Force "$root\dist" | Out-Null
& "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /codepage:65001 /target:winexe /platform:x64 /optimize+ "/out:$root\dist\Tuin-Doom-Enhanced.exe" "/resource:$root\build\Release\doom.exe,doom.exe" "/resource:$PSScriptRoot\hero.png,hero.png" "/resource:$PSScriptRoot\graphics.cfg,graphics.cfg" "/resource:$PSScriptRoot\DiggingMaterials.wad,DiggingMaterials.wad" "/resource:$PSScriptRoot\mods\FluidWeapons.pk3,FluidWeapons.pk3" "/resource:$PSScriptRoot\mods\FluidWeapons-SFX.pk3,FluidWeapons-SFX.pk3" "/resource:$PSScriptRoot\mods\FluidMonsters.pk3,FluidMonsters.pk3" /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Xml.Linq.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll "$PSScriptRoot\Launcher.cs" "$PSScriptRoot\LanMenu.cs"
if ($LASTEXITCODE) { throw 'Launcher build failed' }
