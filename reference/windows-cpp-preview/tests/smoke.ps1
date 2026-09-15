#Requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string] $Executable)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class StarUi {
    [DllImport("user32.dll")] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string className, string title);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window, int id);
    [DllImport("user32.dll", EntryPoint="SendMessageW", CharSet=CharSet.Unicode)] private static extern IntPtr WriteText(IntPtr window, uint message, IntPtr unused, string text);
    [DllImport("user32.dll", EntryPoint="SendMessageW", CharSet=CharSet.Unicode)] private static extern IntPtr ReadText(IntPtr window, uint message, IntPtr size, StringBuilder text);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr window, StringBuilder name, int length);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] private static extern IntPtr SendMessageTimeout(IntPtr window, uint message, IntPtr wParam, IntPtr lParam, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    private delegate bool EnumWindowsProc(IntPtr window, IntPtr lParam);
    [DllImport("user32.dll")] private static extern bool EnumThreadWindows(uint thread, EnumWindowsProc callback, IntPtr lParam);
    public static long Send(IntPtr window, uint message, long wParam, long lParam) {
        IntPtr result;
        if (SendMessageTimeout(window, message, new IntPtr(wParam), new IntPtr(lParam), 2, 5000, out result) == IntPtr.Zero)
            throw new Exception("UI message timed out: " + message);
        return result.ToInt64();
    }
    public static string Text(IntPtr window) {
        var text = new StringBuilder(1024);
        ReadText(window, 0xD, new IntPtr(text.Capacity), text);
        return text.ToString();
    }
    public static void SetText(IntPtr window, string text) {
        if (window == IntPtr.Zero) throw new Exception("Text control missing.");
        if (WriteText(window, 0xC, IntPtr.Zero, text) == IntPtr.Zero) throw new Exception("WM_SETTEXT failed.");
    }
    public static IntPtr Dialog(IntPtr main) {
        uint pid;
        uint thread = GetWindowThreadProcessId(main, out pid);
        IntPtr found = IntPtr.Zero;
        EnumThreadWindows(thread, (window, unused) => {
            var name = new StringBuilder(64);
            GetClassName(window, name, name.Capacity);
            if (name.ToString() == "#32770" && IsWindowVisible(window)) found = window;
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@

function Assert-True([bool] $Condition, [string] $Message) {
	if (-not $Condition) { throw $Message }
}
function Get-Editor([IntPtr] $Window) {
	$after = [IntPtr]::Zero
	do {
		$after = [StarUi]::FindWindowEx($Window, $after, 'Scintilla', $null)
		if ($after -ne [IntPtr]::Zero -and [StarUi]::IsWindowVisible($after)) { return $after }
	} while ($after -ne [IntPtr]::Zero)
	throw 'No visible editor control.'
}
function Command([IntPtr] $Window, [int] $Id) {
	[void][StarUi]::Send($Window, 0x111, $Id, 0)
}
function Wait-Dialog([IntPtr] $Window) {
	$deadline = [DateTime]::UtcNow.AddSeconds(5)
	do {
		$dialog = [StarUi]::Dialog($Window)
		if ($dialog -ne [IntPtr]::Zero) { return $dialog }
		Start-Sleep -Milliseconds 50
	} while ([DateTime]::UtcNow -lt $deadline)
	throw 'Expected dialog did not appear.'
}

$scratch = Join-Path ([IO.Path]::GetTempPath()) ('notepad-star-ui-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
$process = $null
try {
	$first = Join-Path $scratch 'first.cpp'
	$second = Join-Path $scratch 'second.py'
	[IO.File]::WriteAllText($first, "int main() { return 0; }`r`n")
	[IO.File]::WriteAllText($second, "print('hello')`n")
	$process = Start-Process -FilePath $Executable -ArgumentList "`"$first`" `"$second`"" -PassThru
	Assert-True ($process.WaitForInputIdle(30000)) 'Editor did not become responsive.'
	$deadline = [DateTime]::UtcNow.AddSeconds(10)
	do {
		$process.Refresh()
		if ($process.MainWindowHandle -ne [IntPtr]::Zero) { break }
		if ($process.HasExited) { throw "Editor exited early: $($process.ExitCode)" }
		Start-Sleep -Milliseconds 50
	} while ([DateTime]::UtcNow -lt $deadline)
	$window = $process.MainWindowHandle
	Assert-True ($window -ne [IntPtr]::Zero) 'Editor window missing.'
	$name = New-Object Text.StringBuilder 64
	[void][StarUi]::GetClassName($window, $name, $name.Capacity)
	Assert-True ($name.ToString() -eq 'NotepadStar.Editor') 'Application identity is not isolated.'
	$tabs = [StarUi]::GetDlgItem($window, 501)
	Assert-True ([StarUi]::Send($tabs, 0x1304, 0, 0) -eq 2) 'Two files did not open in separate tabs.'
	$editor = Get-Editor $window
	Assert-True ([StarUi]::Send($editor, 4002, 0, 0) -eq 2) 'Python syntax was not selected.'
	[void][StarUi]::Send($editor, 2013, 0, 0)
	foreach ($character in 'alpha beta alpha'.ToCharArray()) {
		[void][StarUi]::Send($editor, 0x102, [int]$character, 0)
	}
	Assert-True ([StarUi]::Send($editor, 2159, 0, 0) -ne 0) 'Editing did not mark the document modified.'
	Command $window 102
	Assert-True ([IO.File]::ReadAllText($second) -eq 'alpha beta alpha') 'Save did not write the edited text.'
	Command $window 136
	$firstEditor = Get-Editor $window
	Assert-True ($firstEditor -ne $editor) 'Tab switch did not preserve independent editor buffers.'
	Assert-True ([StarUi]::Send($firstEditor, 4002, 0, 0) -eq 3) 'C++ syntax was not selected.'
	Assert-True ([IO.File]::ReadAllText($first) -eq "int main() { return 0; }`r`n") 'Editing another tab changed the first file.'
	Command $window 135
	Assert-True ((Get-Editor $window) -eq $editor) 'Returning to a tab lost its buffer.'
	Command $window 130
	Assert-True ([StarUi]::Send($editor, 2269, 0, 0) -eq 1) 'Word wrap did not turn on.'
	Command $window 131
	Assert-True ([StarUi]::Send($editor, 2482, 32, 0) -eq 0x28211e) 'Dark editor background was not applied.'

	Command $window 120
	[StarUi]::SetText([StarUi]::GetDlgItem($window, 300), 'alpha')
	[StarUi]::SetText([StarUi]::GetDlgItem($window, 301), 'gamma')
	[void][StarUi]::Send($editor, 2160, 0, 0)
	Command $window 121
	$selectionStart = [StarUi]::Send($editor, 2143, 0, 0)
	$selectionEnd = [StarUi]::Send($editor, 2145, 0, 0)
	Assert-True ($selectionStart -eq 0 -and $selectionEnd -eq 5) "Find selected the wrong range: $selectionStart to $selectionEnd. Query: $([StarUi]::Text([StarUi]::GetDlgItem($window, 300))). Document: $([StarUi]::Text($editor))"
	Command $window 124
	Command $window 102
	Assert-True ([IO.File]::ReadAllText($second) -eq 'gamma beta gamma') 'Replace all produced incorrect text.'
	Command $window 110
	Command $window 102
	Assert-True ([IO.File]::ReadAllText($second) -eq 'alpha beta alpha') 'Replace all was not one undo operation.'
	Command $window 125

	[void][StarUi]::Send($editor, 2318, 0, 0)
	[void][StarUi]::Send($editor, 0x102, [int][char]'!', 0)
	[void][StarUi]::PostMessage($window, 0x111, [IntPtr]105, [IntPtr]::Zero)
	$dialog = Wait-Dialog $window
	[void][StarUi]::PostMessage($dialog, 0x111, [IntPtr]2, [IntPtr]::Zero)
	Start-Sleep -Milliseconds 100
	Assert-True ([StarUi]::Send($tabs, 0x1304, 0, 0) -eq 2) 'Cancel did not preserve the dirty tab.'
	Command $window 110
	Assert-True ([StarUi]::Send($editor, 2159, 0, 0) -eq 0) 'Undo did not return to the saved state.'

	Command $window 105
	Assert-True ([StarUi]::Send($tabs, 0x1304, 0, 0) -eq 1) 'Closing a saved tab failed.'
	Command $window 100
	Assert-True ([StarUi]::Send($tabs, 0x1304, 0, 0) -eq 2) 'New tab failed.'
	Command $window 105
	[void][StarUi]::PostMessage($window, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
	Assert-True ($process.WaitForExit(10000)) 'Clean application exit failed.'
	Assert-True ($process.ExitCode -eq 0) 'Application exited with an error.'
	Write-Output 'UI checks passed: independent tabs, editing/save, lexers, find/replace, undo, theme/wrap, cancel-close, new/close and exit.'
} finally {
	if ($null -ne $process -and -not $process.HasExited) { Stop-Process -Id $process.Id }
	Remove-Item -LiteralPath $scratch -Recurse -Force
}
