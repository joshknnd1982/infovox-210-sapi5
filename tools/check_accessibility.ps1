# Reads the settings dialog the way a screen reader does.
#
# PowerShell's UIA client reports almost every Win32 control as a plain "Pane",
# which hides exactly the problem worth checking for; MSAA (oleacc) reports the
# roles and names NVDA actually announces, so this uses that.
param([string]$Exe = "$PSScriptRoot\..\output\InfovoxConfig.exe")

Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;

public static class Msaa {
    [DllImport("user32.dll")] public static extern IntPtr FindWindowW(string c, string n);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc p, IntPtr l);
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetWindowLongW(IntPtr h, int i);
    [DllImport("oleacc.dll")] public static extern int AccessibleObjectFromWindow(IntPtr hwnd, uint id, ref Guid iid,
        [MarshalAs(UnmanagedType.IUnknown)] out object acc);
    [DllImport("oleacc.dll", CharSet=CharSet.Unicode)] public static extern uint GetRoleTextW(uint role, StringBuilder s, uint n);

    public static System.Collections.Generic.List<IntPtr> Children(IntPtr parent) {
        var list = new System.Collections.Generic.List<IntPtr>();
        EnumChildWindows(parent, (h, l) => { list.Add(h); return true; }, IntPtr.Zero);
        return list;
    }
}
'@

$proc = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Seconds 2
$proc.Refresh()
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) { Write-Output "dialog not found"; $proc | Stop-Process -Force; exit 1 }
Write-Output ("dialog: " + $proc.MainWindowTitle)

$iid = [Guid]"618736e0-3c3d-11cf-810c-00aa00389b71"   # IAccessible
$WS_TABSTOP = 0x00010000
$WS_DISABLED = 0x08000000

Write-Output ("{0,-4} {1,-6} {2,-18} {3,-14} {4}" -f "tab", "id", "class", "role", "name")
$i = 0
foreach ($h in [Msaa]::Children($hwnd)) {
    $cls = New-Object System.Text.StringBuilder 256
    [void][Msaa]::GetClassNameW($h, $cls, 256)
    $txt = New-Object System.Text.StringBuilder 512
    [void][Msaa]::GetWindowTextW($h, $txt, 512)
    $id = [Msaa]::GetDlgCtrlID($h)
    $style = [Msaa]::GetWindowLongW($h, -16)
    $tab = if ($style -band $WS_TABSTOP) { "yes" } else { "-" }
    $dis = if ($style -band $WS_DISABLED) { " (disabled)" } else { "" }

    $role = "?"; $name = ""
    $acc = $null
    $OBJID_CLIENT = [uint32]4294967292   # 0xFFFFFFFC; PowerShell reads the literal as -4
    if ([Msaa]::AccessibleObjectFromWindow($h, $OBJID_CLIENT, [ref]$iid, [ref]$acc) -eq 0 -and $acc) {
        try {
            $t = $acc.GetType()
            $r = $t.InvokeMember("accRole", "GetProperty", $null, $acc, @([int]0))
            $sb = New-Object System.Text.StringBuilder 128
            [void][Msaa]::GetRoleTextW([uint32]$r, $sb, 128)
            $role = $sb.ToString()
            $name = $t.InvokeMember("accName", "GetProperty", $null, $acc, @([int]0))
        } catch { }
    }
    if (-not $name) { $name = $txt.ToString() }
    Write-Output ("{0,-4} {1,-6} {2,-18} {3,-14} {4}{5}" -f $tab, $id, $cls.ToString(), $role, $name, $dis)
    $i++
}
$proc | Stop-Process -Force
