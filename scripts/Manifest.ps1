$ErrorActionPreference='Stop'
$root=Join-Path (Split-Path $PSScriptRoot) 'package'
@(Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {$_.Name -ne 'manifest.json' -and $_.FullName -notlike '*\Backups\*'} | Sort-Object FullName | ForEach-Object {
 [pscustomobject]@{path=$_.FullName.Substring($root.Length+1).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName).Hash;bytes=$_.Length}
}) | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $root 'manifest.json') -Encoding UTF8
