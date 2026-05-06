param(
    [Parameter(Mandatory = $true)]
    [string]$HostName,

    [string]$User = "root",

    [switch]$EnableLauncher,

    [switch]$DisableLauncher,

    [switch]$SkipBinary
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$OnionRoot = Resolve-Path (Join-Path $RepoRoot "..\Onion-4.3.1-1")
$RemoteRoot = "/mnt/SDCARD"
$Target = "${User}@${HostName}"

function Invoke-Remote {
    param([string]$Command)
    & ssh $Target $Command
}

function Copy-RemoteFile {
    param(
        [string]$LocalPath,
        [string]$RemotePath
    )

    if (Test-Path -LiteralPath $LocalPath) {
        & scp -q $LocalPath "${Target}:$RemotePath"
    }
}

function Copy-RemoteDirectoryContents {
    param(
        [string]$LocalDir,
        [string]$RemoteDir,
        [string]$Filter = "*"
    )

    if (Test-Path -LiteralPath $LocalDir) {
        $items = Get-ChildItem -LiteralPath $LocalDir -Filter $Filter -File
        foreach ($item in $items) {
            Copy-RemoteFile $item.FullName "$RemoteDir/$($item.Name)"
        }
    }
}

Write-Host "Preparing remote folders on $Target..."
Invoke-Remote "mkdir -p $RemoteRoot/.tmp_update/res/onyx/icons $RemoteRoot/.tmp_update/script $RemoteRoot/.tmp_update/config $RemoteRoot/miyoo/app"

Write-Host "Syncing launcher icons..."
Copy-RemoteDirectoryContents (Join-Path $RepoRoot "icons") "$RemoteRoot/.tmp_update/res/onyx/icons" "*.png"

Write-Host "Syncing project icon working copies..."
Copy-RemoteDirectoryContents (Join-Path $RepoRoot "..\Icons") "$RemoteRoot/.tmp_update/res/onyx/icons" "*.png"

Write-Host "Syncing fonts..."
Copy-RemoteFile (Join-Path $RepoRoot "..\Fonts\SairaSemiCondensed-Medium.ttf") "$RemoteRoot/miyoo/app/SairaSemiCondensed-Medium.ttf"
Copy-RemoteFile (Join-Path $RepoRoot "..\Fonts\Rajdhani-Medium.ttf") "$RemoteRoot/miyoo/app/Rajdhani-Medium.ttf"

Write-Host "Syncing launcher startup script..."
Copy-RemoteFile (Join-Path $RepoRoot "scripts\onyx_launcher.sh") "$RemoteRoot/.tmp_update/script/onyx_launcher.sh"
Invoke-Remote "chmod +x $RemoteRoot/.tmp_update/script/onyx_launcher.sh"

$bootScreen = Join-Path $RepoRoot "..\onyx-bootScreen.png"
if (Test-Path -LiteralPath $bootScreen) {
    Write-Host "Syncing splash screen..."
    Copy-RemoteFile $bootScreen "$RemoteRoot/.tmp_update/res/bootScreen.png"
}

if (-not $SkipBinary) {
    $binaryCandidates = @(
        (Join-Path $OnionRoot "static\build\.tmp_update\bin\onyxLauncher"),
        (Join-Path $OnionRoot ".tmp_update\bin\onyxLauncher"),
        (Join-Path $OnionRoot "bin\onyxLauncher")
    )

    $binary = $binaryCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if ($binary) {
        Write-Host "Syncing launcher binary..."
        Invoke-Remote "mkdir -p $RemoteRoot/.tmp_update/bin"
        Copy-RemoteFile $binary "$RemoteRoot/.tmp_update/bin/onyxLauncher"
        Invoke-Remote "chmod +x $RemoteRoot/.tmp_update/bin/onyxLauncher"
    }
    else {
        Write-Host "No built onyxLauncher binary found locally. Use -SkipBinary to hide this message."
    }
}

if ($EnableLauncher -and $DisableLauncher) {
    throw "Use either -EnableLauncher or -DisableLauncher, not both."
}

if ($EnableLauncher) {
    Write-Host "Enabling Onyx launcher flag..."
    Invoke-Remote "touch $RemoteRoot/.tmp_update/config/.useOnyxLauncher"
}

if ($DisableLauncher) {
    Write-Host "Disabling Onyx launcher flag..."
    Invoke-Remote "rm -f $RemoteRoot/.tmp_update/config/.useOnyxLauncher"
}

Invoke-Remote "sync"
Write-Host "Done. Restart the Miyoo UI or reboot the device to see launcher binary/runtime changes."
