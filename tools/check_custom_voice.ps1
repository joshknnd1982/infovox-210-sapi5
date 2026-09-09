# Selects "Infovox 210 Custom Voice" in the settings dialog and confirms that
# every parameter becomes available, which is what makes it the configurable one.
param([string]$Exe = "$PSScriptRoot\..\output\InfovoxConfig.exe")

Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class W {
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern int GetWindowLongW(IntPtr h, int i);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
}
'@

$CB_GETCOUNT = 0x0146; $CB_SETCURSEL = 0x014E; $WM_COMMAND = 0x0111
$CBN_SELCHANGE = 1; $WS_DISABLED = 0x08000000

$proc = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Seconds 2
$proc.Refresh()
$dlg = $proc.MainWindowHandle
if ($dlg -eq [IntPtr]::Zero) { Write-Output "dialog not found"; $proc | Stop-Process -Force; exit 1 }

$combo = [W]::GetDlgItem($dlg, 1001)
$count = [int][W]::SendMessageW($combo, $CB_GETCOUNT, [IntPtr]::Zero, [IntPtr]::Zero)
$last = $count - 1
Write-Output "voice list holds $count entries; selecting the last one"
[void][W]::SendMessageW($combo, $CB_SETCURSEL, [IntPtr]$last, [IntPtr]::Zero)
$wparam = [IntPtr](($CBN_SELCHANGE -shl 16) -bor 1001)
[void][W]::SendMessageW($dlg, $WM_COMMAND, $wparam, $combo)
Start-Sleep -Milliseconds 400

$names = @{ 1003 = "Language"; 1005 = "Voice type"; 1007 = "Speaking rate";
            1010 = "Pitch"; 1013 = "Pitch modulation"; 1016 = "Breathiness";
            1019 = "Volume" }
$bad = 0
foreach ($id in ($names.Keys | Sort-Object)) {
    $h = [W]::GetDlgItem($dlg, $id)
    $style = [W]::GetWindowLongW($h, -16)
    $enabled = -not ($style -band $WS_DISABLED)
    if (-not $enabled) { $bad++ }
    Write-Output ("  {0,-18} {1}" -f $names[$id], $(if ($enabled) { "enabled" } else { "DISABLED" }))
}
$note = New-Object System.Text.StringBuilder 512
[void][W]::GetWindowTextW([W]::GetDlgItem($dlg, 1026), $note, 512)
Write-Output "note: $($note.ToString())"
$proc | Stop-Process -Force
if ($bad -gt 0) { Write-Output "FAILED: $bad controls stayed disabled"; exit 1 }
Write-Output "custom voice exposes every parameter"
