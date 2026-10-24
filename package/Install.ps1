[CmdletBinding()]
param([string]$GamePath, [switch]$UseExistingAsiLoader, [switch]$ReplaceExisting, [switch]$AllowDifferentGameBuild)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function FindGame {
    $roots=@('C:\Steam','C:\Program Files (x86)\Steam')
    $steamRecord=Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue
    $steam=if($steamRecord){$steamRecord.SteamPath}else{$null}
    if($steam){$roots+= $steam}
    foreach($root in @($roots)){
        $vdf=Join-Path $root 'steamapps\libraryfolders.vdf'
        if(Test-Path -LiteralPath $vdf){
            foreach($match in [regex]::Matches((Get-Content -LiteralPath $vdf -Raw),'"path"\s+"([^"]+)"')){
                $roots+=$match.Groups[1].Value.Replace('\\','\')
            }
        }
    }
    $found=@($roots | Select-Object -Unique | ForEach-Object {Join-Path $_ 'steamapps\common\Enshrouded'} | Where-Object {Test-Path -LiteralPath (Join-Path $_ 'enshrouded.exe')})
    if($found.Count -eq 1){return $found[0]}
    return Read-Host 'Paste the Enshrouded folder shown by Steam > Manage > Browse local files'
}
if(!$GamePath){$GamePath=FindGame}
$GamePath=(Resolve-Path -LiteralPath $GamePath.Trim('"')).Path
if(!(Test-Path -LiteralPath (Join-Path $GamePath 'enshrouded.exe'))){throw 'enshrouded.exe was not found in that folder.'}
if(Get-Process enshrouded -ErrorAction SilentlyContinue){throw 'Close Enshrouded before installing. No files were changed.'}
$tested='D64966E23B1A458EE6F147BCBCEAC0B577FA7E39A212F371287D8CC119817882'
if((Hash (Join-Path $GamePath 'enshrouded.exe')) -ne $tested -and !$AllowDifferentGameBuild){
    throw 'Different game build from the packaged snapshot. See README.txt compatibility; an agent may review and pass -AllowDifferentGameBuild explicitly.'
}
# Verify package integrity before writing to the game.
$manifest=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') -Raw | ConvertFrom-Json
foreach($entry in $manifest){
    $file=Join-Path $PSScriptRoot $entry.path
    if(!(Test-Path -LiteralPath $file) -or (Hash $file) -ne $entry.sha256){throw "Package is incomplete or changed: $($entry.path)"}
}
$payload=Join-Path $PSScriptRoot 'GameFiles'
$plan=@(Get-ChildItem -LiteralPath $payload -Recurse -File | ForEach-Object {
    [pscustomobject]@{Source=$_.FullName; Relative=$_.FullName.Substring($payload.Length+1); Generated=$false}
})
$loader=Join-Path $GamePath 'dinput8.dll'
if(Test-Path -LiteralPath $loader){
    if((Hash $loader) -ne (Hash (Join-Path $PSScriptRoot 'OptionalLoader\dinput8.dll')) -and !$UseExistingAsiLoader){
        throw 'An existing dinput8.dll is present. It has been left untouched. Have your agent verify it loads ASI mods, then rerun with -UseExistingAsiLoader. Never replace it blindly.'
    }
}else{
    $plan += [pscustomobject]@{Source=(Join-Path $PSScriptRoot 'OptionalLoader\dinput8.dll');Relative='dinput8.dll';Generated=$false}
}
# Paths are generated on the recipient PC, never copied from the sender.
$ini=@"
[Diagnostics]
Module=$(Join-Path $GamePath 'mods\AutoLootClient\Probe134.dll')
LogPath=$(Join-Path $GamePath 'mods\AutoLootClient\AutoLoot.log')
Verbose=0
[Testing]
AllowBackgroundLoot=0
LogMotion=0
LogInventory=0
DirectedCorpseOnce=0
DirectCorpseOnce=0
RecoverCorpseMenuOnce=0
RecoverCorpseMenuSource=0
TransferOnce=0
SnapshotOnce=0
NetworkOnce=0
[Safety]
HideAutomaticCorpseHud=0
DirectedResourceAutoLoot=1
DirectedCorpseAutoLoot=1
TargetedCorpseAutoLoot=1
PauseAutomaticActions=1
"@
$plan += [pscustomobject]@{Source=$null;Relative='ClientLootRequests.ini';Generated=$true}
# Preflight all collisions before any game file is changed.
foreach($item in $plan){
    $dest=Join-Path $GamePath $item.Relative
    if(Test-Path -LiteralPath $dest){
        $same=(!$item.Generated -and (Hash $dest) -eq (Hash $item.Source))
        if(!$same -and !$ReplaceExisting){throw "Existing file differs: $($item.Relative). No files changed. Review it first; -ReplaceExisting explicitly permits backed-up replacement."}
    }
}
$backup=Join-Path $PSScriptRoot ('Backups\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
$receipt=@()
try {
    foreach($item in $plan){
        $dest=Join-Path $GamePath $item.Relative
        $existed=Test-Path -LiteralPath $dest
        if($existed){
            $saved=Join-Path $backup $item.Relative
            New-Item -ItemType Directory -Path (Split-Path $saved) -Force | Out-Null
            Copy-Item -LiteralPath $dest -Destination $saved
        }
        $receipt += [pscustomobject]@{Relative=$item.Relative;Existed=$existed;Hash=$null}
        New-Item -ItemType Directory -Path (Split-Path $dest) -Force | Out-Null
        if($item.Generated){[IO.File]::WriteAllText($dest,$ini,[Text.Encoding]::Unicode)}else{Copy-Item -LiteralPath $item.Source -Destination $dest -Force}
        $receipt[-1].Hash=Hash $dest
        if(!$item.Generated -and $receipt[-1].Hash -ne (Hash $item.Source)){throw "Copy verification failed: $($item.Relative)"}
    }
} catch {
    # Roll back only files this invocation touched.
    foreach($entry in $receipt){
        $dest=Join-Path $GamePath $entry.Relative
        if($entry.Existed){Copy-Item -LiteralPath (Join-Path $backup $entry.Relative) -Destination $dest -Force}
        elseif(Test-Path -LiteralPath $dest){Remove-Item -LiteralPath $dest}
    }
    throw
}
[pscustomobject]@{GamePath=$GamePath;Files=$receipt} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $backup 'receipt.json') -Encoding UTF8
Write-Host "Installed and hash-verified. Recovery receipt: $backup\receipt.json"
Write-Host 'Start Enshrouded through Steam. F8 = Auto Loot settings.'
Write-Host 'If you kept a custom ASI loader, ensure it actually loads root-level ASI files.'