param([string]$DestinationDirectory='')
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$workspace=Split-Path -Parent $PSScriptRoot
$manifest=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'hyperchunk-manifest.json') -Raw | ConvertFrom-Json
$patches=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'hyperchunk-patches.json') -Raw | ConvertFrom-Json
$destination=Join-Path $workspace 'third_party/hyperchunk'
if($DestinationDirectory) { $destination=[System.IO.Path]::GetFullPath($DestinationDirectory) }
foreach($file in $manifest.files.PSObject.Properties) {
    $path=Join-Path $destination $file.Name
    $expected=$file.Value
    if($manifest.patched_files.PSObject.Properties[$file.Name]) { $expected=$manifest.patched_files.PSObject.Properties[$file.Name].Value }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
    if((Test-Path -LiteralPath $path) -and (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $expected) { continue }
    $temporary=$path+'.download'
    Invoke-WebRequest -Uri ('https://raw.githubusercontent.com/'+$manifest.repository+'/'+$manifest.revision+'/'+$file.Name) -OutFile $temporary
    if((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ne $file.Value) { throw ('Hyperchunk checksum mismatch: '+$file.Name) }
    $entry=$patches.PSObject.Properties[$file.Name]
    if($entry) {
        $original=[System.IO.File]::ReadAllBytes($temporary)
        $stream=New-Object System.IO.MemoryStream
        try {
            $cursor=0
            foreach($edit in $entry.Value) {
                $offset=[int]$edit.offset
                if($offset -lt $cursor -or $offset+[int]$edit.length -gt $original.Length) { throw 'Invalid pinned byte patch' }
                $stream.Write($original,$cursor,$offset-$cursor)
                $replacement=[Convert]::FromBase64String($edit.base64)
                $stream.Write($replacement,0,$replacement.Length)
                $cursor=$offset+[int]$edit.length
            }
            $stream.Write($original,$cursor,$original.Length-$cursor)
            [System.IO.File]::WriteAllBytes($temporary,$stream.ToArray())
        } finally { $stream.Dispose() }
    }
    if((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ne $expected) { throw ('Patched Hyperchunk checksum mismatch: '+$file.Name) }
    Move-Item -LiteralPath $temporary -Destination $path -Force
}
Write-Output 'All pinned Hyperchunk files and 1.21.11 adapter patches verified.'
