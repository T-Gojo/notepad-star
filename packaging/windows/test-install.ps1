#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $Installer,
    [Parameter(Mandatory = $true)][string] $Report
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:GITHUB_ACTIONS -ne 'true' -or -not $env:RUNNER_TEMP) {
    throw 'Installer lifecycle tests run only on disposable GitHub Actions runners, never on a developer profile.'
}
$key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\NotepadStarRust'
$shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'Notepad Star.lnk'
if ((Test-Path -LiteralPath $key) -or (Test-Path -LiteralPath $shortcut)) {
    throw 'An existing installation/shortcut must not be overwritten by the lifecycle test.'
}
$installerPath = (Get-Item -LiteralPath $Installer).FullName
$root = Join-Path $env:RUNNER_TEMP ('notepad-star-install-test-' + [guid]::NewGuid().ToString('N'))
$install = Join-Path $root 'application'
$profile = Join-Path $root 'profile'
New-Item -ItemType Directory -Path $profile -Force | Out-Null
$sentinel = Join-Path $profile 'keep-user-data.txt'
[IO.File]::WriteAllText($sentinel, 'preserve this test profile')
function Invoke-Setup([string] $File, [string[]] $Arguments, [int] $Expected) {
    $process = Start-Process -FilePath $File -ArgumentList $Arguments -PassThru
    if (-not $process.WaitForExit(120000)) {
        Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
        throw 'Installer process exceeded two minutes.'
    }
    if ($process.ExitCode -ne $Expected) { throw "Unexpected installer exit: $($process.ExitCode), expected $Expected." }
}
$editor = $null
try {
    Invoke-Setup $installerPath @('/S', "/D=$install") 0
    $exe = Join-Path $install 'notepad-star.exe'
    $uninstaller = Join-Path $install 'uninstall.exe'
    if (-not (Test-Path -LiteralPath $exe) -or -not (Test-Path -LiteralPath $key)) {
        throw 'Installation did not create/register the application.'
    }
    $hash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    $note = Join-Path $install 'keep-user-file.txt'
    [IO.File]::WriteAllText($note, 'unknown files must survive uninstall')
    $editor = Start-Process -FilePath $exe -ArgumentList @('--preview', '--profile-dir', ('"' + $profile + '"')) -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 200
        $editor.Refresh()
    } while (-not $editor.HasExited -and $editor.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($editor.HasExited -or $editor.MainWindowHandle -eq 0) { throw 'The installed app did not show a window.' }
    Invoke-Setup $installerPath @('/S', "/D=$install") 2
    Invoke-Setup $uninstaller @('/S') 2
    if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $hash) { throw 'A running installation was modified.' }
    if (-not $editor.CloseMainWindow() -or -not $editor.WaitForExit(15000)) { throw 'The installed test window did not close normally.' }
    Invoke-Setup $installerPath @('/S', "/D=$install") 0
    if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $hash) { throw 'Reinstall/upgrade changed the payload unexpectedly.' }
    Invoke-Setup $uninstaller @('/S') 0
    if ((Test-Path -LiteralPath $exe) -or (Test-Path -LiteralPath $key) -or (Test-Path -LiteralPath $shortcut)) {
        throw 'Uninstall left application files or registration behind.'
    }
    if (-not (Test-Path -LiteralPath $sentinel) -or -not (Test-Path -LiteralPath $note)) {
        throw 'Uninstall removed profile data or an unknown user file.'
    }
    $result = [ordered]@{ schema=1; status='passed'; installer_sha256=(Get-FileHash $installerPath -Algorithm SHA256).Hash; install=$true; running_guard=$true; reinstall=$true; uninstall=$true; user_data_preserved=$true }
    $result | ConvertTo-Json | Set-Content -LiteralPath $Report -Encoding UTF8
}
finally {
    if ($null -ne $editor) {
        $editor.Refresh()
        if (-not $editor.HasExited) { Stop-Process -Id $editor.Id }
    }
    # The runner is disposable; retain failed installation artifacts for diagnosis.
}
