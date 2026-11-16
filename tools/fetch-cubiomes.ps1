param([string]$DestinationDirectory='')
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$workspace=Split-Path -Parent $PSScriptRoot
$manifest=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'cubiomes-manifest.json') -Raw | ConvertFrom-Json
$destination=Join-Path $workspace 'third_party/cubiomes'
if($DestinationDirectory) { $destination=[System.IO.Path]::GetFullPath($DestinationDirectory) }
foreach($file in $manifest.files.PSObject.Properties) {
    $path=Join-Path $destination $file.Name
    $expected=$file.Value
    if($manifest.patched_files -and $manifest.patched_files.PSObject.Properties[$file.Name]) { $expected=$manifest.patched_files.PSObject.Properties[$file.Name].Value }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
    if(!(Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $expected) {
        $temporary=$path+'.download'
        Invoke-WebRequest -Uri ('https://raw.githubusercontent.com/'+$manifest.repository+'/'+$manifest.commit+'/'+$file.Name) -OutFile $temporary
        if((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ne $file.Value) { throw ('Cubiomes checksum mismatch: '+$file.Name) }
        if($file.Name -eq 'biomenoise.c') {
            $original="        int alt = (int) *dat;`n        uint64_t ds = get_np_dist(np, bt, alt);"
            $replacement="        /* CUDA Craft: UINT64_MAX explicitly starts a chunk with no cached leaf.`n         * Do not invent a previous below-world biome to initialize that cache. */`n        int alt = *dat == UINT64_MAX ? 0 : (int) *dat;`n        uint64_t ds = *dat == UINT64_MAX ? UINT64_MAX : get_np_dist(np, bt, alt);"
            $content=[System.IO.File]::ReadAllText($temporary)
            if(!$content.Contains($original)) { throw 'Cubiomes adapter patch context differs' }
            [System.IO.File]::WriteAllText($temporary,$content.Replace($original,$replacement),(New-Object System.Text.UTF8Encoding($false)))
        }
        if((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ne $expected) { throw ('Patched Cubiomes checksum mismatch: '+$file.Name) }
        Move-Item -LiteralPath $temporary -Destination $path -Force
    }
}
Write-Output 'All pinned Cubiomes files verified.'
