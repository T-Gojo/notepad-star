#Requires -Version 5.1
[CmdletBinding()]
param(
	[ValidateSet('Debug', 'Release')][string] $Configuration = 'Debug',
	[switch] $Run,
	[switch] $Test,
	[string] $MSBuild
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $MSBuild) {
	$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
	if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 Desktop development with C++, the v143 toolset, and a Windows SDK.' }
	$MSBuild = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
	if ($LASTEXITCODE -ne 0 -or -not $MSBuild) { throw 'Visual Studio C++ build tools were not found.' }
}
& $MSBuild (Join-Path $PSScriptRoot 'NotepadStar.vcxproj') /m:2 /nologo /v:quiet "/p:Configuration=$Configuration" /p:Platform=x64
if ($LASTEXITCODE -ne 0) { throw "Editor build failed (exit $LASTEXITCODE)." }
$output = Join-Path $PSScriptRoot "build\$Configuration"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
foreach ($item in @(
	@('LICENSE', 'LICENSE.txt'),
	@('reference\notepad-plus-plus\scintilla\License.txt', 'Scintilla-LICENSE.txt'),
	@('reference\notepad-plus-plus\lexilla\License.txt', 'Lexilla-LICENSE.txt'),
	@('reference\windows-cpp-preview\licenses\Boost-LICENSE.txt', 'Boost-LICENSE.txt'),
	@('reference\windows-cpp-preview\NOTICE.txt', 'NOTICE.txt')
)) {
	Copy-Item -LiteralPath (Join-Path $root $item[0]) -Destination (Join-Path $output $item[1])
}
$exe = Join-Path $output 'notepad-star.exe'
if ($Test) {
	& $MSBuild (Join-Path $PSScriptRoot 'tests\FileIoTests.vcxproj') /nologo /v:quiet "/p:Configuration=$Configuration" /p:Platform=x64
	if ($LASTEXITCODE -ne 0) { throw 'Test build failed.' }
	& (Join-Path $output 'file-io-tests.exe')
	if ($LASTEXITCODE -ne 0) { throw 'File I/O tests failed.' }
	& (Join-Path $PSScriptRoot 'tests\smoke.ps1') -Executable $exe
}
Write-Output "Built $exe"
if ($Run) {
	$process = Start-Process -FilePath $exe -ArgumentList '--preview' -PassThru
	if (-not $process.WaitForInputIdle(30000)) { throw 'Preview did not become responsive.' }
	$process.Refresh()
	if ($process.HasExited) { throw "Preview exited early (exit $($process.ExitCode))." }
	Write-Output "Preview running (PID $($process.Id))."
}
