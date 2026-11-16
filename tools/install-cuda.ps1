$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$workspace = Split-Path -Parent $PSScriptRoot
$toolRoot = Join-Path $workspace '.tools'
$downloadRoot = Join-Path $toolRoot 'downloads'
$cudaRoot = Join-Path $toolRoot 'cuda'
New-Item -ItemType Directory -Force -Path $downloadRoot, $cudaRoot | Out-Null
$baseUri = 'https://developer.download.nvidia.com/compute/cuda/redist/'
$manifestUri = $baseUri + 'redistrib_12.8.1.json'
$manifestPath = Join-Path $downloadRoot 'redistrib_12.8.1.json'
Invoke-WebRequest -Uri $manifestUri -OutFile $manifestPath
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.release_label -ne '12.8.1') { throw 'Unexpected CUDA manifest version' }

foreach ($component in @('cuda_nvcc', 'cuda_cudart', 'cuda_cccl')) {
    $entry = $manifest.$component.'windows-x86_64'
    if (-not $entry -or $entry.relative_path -notmatch '^[a-z0-9_/-]+[a-z0-9_.-]+\.zip$' -or $entry.relative_path.Contains('..')) {
        throw "Invalid package path for $component"
    }
    $archivePath = Join-Path $downloadRoot (Split-Path -Leaf $entry.relative_path)
    if (-not (Test-Path -LiteralPath $archivePath) -or (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $entry.sha256) {
        Write-Output "Downloading $component ($($entry.size) bytes) from NVIDIA..."
        Invoke-WebRequest -Uri ($baseUri + $entry.relative_path) -OutFile $archivePath
    }
    if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $entry.sha256) {
        throw "SHA-256 mismatch for $component"
    }
    Write-Output "Verified $component. Extracting..."
    $extractRoot = Join-Path $toolRoot ('packages/' + $component)
    New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
    Expand-Archive -LiteralPath $archivePath -DestinationPath $extractRoot -Force
    $packageDirectories = @(Get-ChildItem -LiteralPath $extractRoot -Directory)
    if ($packageDirectories.Count -ne 1) { throw "Unexpected archive layout for $component" }
    foreach ($item in Get-ChildItem -LiteralPath $packageDirectories[0].FullName -Force) {
        # Preserve each package's license independently when merging the toolkit tree.
        if ($item.Name -match '^(LICENSE|EULA)') {
            Copy-Item -LiteralPath $item.FullName -Destination (Join-Path $cudaRoot ($component + '-' + $item.Name)) -Force
        } else {
            Copy-Item -LiteralPath $item.FullName -Destination $cudaRoot -Recurse -Force
        }
    }
}
& (Join-Path $cudaRoot 'bin/nvcc.exe') --version
if ($LASTEXITCODE -ne 0) { throw 'CUDA compiler verification failed' }
Write-Output "CUDA Toolkit installed locally at $cudaRoot"
