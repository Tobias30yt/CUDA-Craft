$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$workspace=Split-Path -Parent $PSScriptRoot
$manifest=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'game-data-manifest.json') -Raw | ConvertFrom-Json
$destination=Join-Path $workspace '.tools/reference'
New-Item -ItemType Directory -Force -Path $destination | Out-Null
foreach($file in $manifest.files.PSObject.Properties) {
    $url='https://raw.githubusercontent.com/PrismarineJS/minecraft-data/'+$manifest.commit+'/data/pc/1.21.11/'+$file.Name
    $path=Join-Path $destination $file.Name
    Invoke-WebRequest -Uri $url -OutFile $path
    if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.Value) { throw ('Game data checksum mismatch: '+$file.Name) }
    Write-Output ('Verified '+$file.Name)
}
Write-Output 'Run py -3 tools/generate-game-data.py to regenerate the checked-in header.'
