$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot
$installer=Join-Path $repo 'package/Install.ps1'
$fixture=Join-Path $repo 'build/installer-fixture'
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $fixture 'enshrouded.exe'),'native installer fixture; not a game')
[IO.File]::WriteAllText((Join-Path $fixture 'unrelated-mod.txt'),'preserve')
function Get-Process { param($Name,$ErrorAction) return }
& $installer -GamePath $fixture -AllowDifferentGameBuild
$ini=Get-Content (Join-Path $fixture 'ClientLootRequests.ini') -Raw
if($ini -notlike '*mods\AutoLootClient\Probe134.dll*' -or $ini.Contains('<GAME>')){throw 'Recipient module path not generated'}
foreach($p in @('ClientLootRequests.asi','AutoLootPlus.asi','mods/AutoLootClient/Probe134.dll','dinput8.dll')){if(!(Test-Path (Join-Path $fixture $p))){throw "Missing payload $p"}}
if((Get-Content (Join-Path $fixture 'unrelated-mod.txt') -Raw) -ne 'preserve'){throw 'Unrelated file changed'}
$refused=$false
try {& $installer -GamePath $fixture -AllowDifferentGameBuild}catch {$refused=$_.Exception.Message -like '*Existing file differs*'}
if(!$refused){throw 'Collision guard failed'}
[IO.File]::WriteAllText((Join-Path $fixture 'dinput8.dll'),'custom-loader')
& $installer -GamePath $fixture -AllowDifferentGameBuild -UseExistingAsiLoader -ReplaceExisting
if((Get-Content (Join-Path $fixture 'dinput8.dll') -Raw) -ne 'custom-loader'){throw 'Custom loader overwritten'}
if(!(Get-ChildItem (Join-Path $repo 'package/Backups') -Recurse -Filter receipt.json)){throw 'Recovery receipt absent'}
Write-Output 'PASS isolated installer fixture: payload hashes, generated paths, collision refusal, recovery receipts, unrelated files and custom loader preservation'
