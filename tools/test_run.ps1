# Run under 32-bit Windows PowerShell.
# Starts the game, presses OK in its launcher dialog, waits, reports how many top-level
# windows in the system are disabled (the original game disables all of them), kills it.
param([string]$Exe, [int]$RunSec = 8)

Add-Type @"
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class T {
    public delegate bool EP(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EP cb, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EP cb, IntPtr l);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(IntPtr h, out int p);
    [DllImport("user32.dll", CharSet=CharSet.Ansi)] public static extern int GetWindowTextA(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet=CharSet.Ansi)] public static extern int GetClassNameA(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool PostMessageA(IntPtr h, int m, IntPtr w, IntPtr l);
    public static int Disabled(int skipPid) {
        int n = 0;
        EnumWindows((h, l) => { int p; GetWindowThreadProcessId(h, out p); if (p != skipPid && !IsWindowEnabled(h)) n++; return true; }, IntPtr.Zero);
        return n;
    }
    public static List<string> Windows(int pid) {
        var r = new List<string>();
        EnumWindows((h, l) => { int p; GetWindowThreadProcessId(h, out p);
            if (p == pid) { var t = new StringBuilder(256); var c = new StringBuilder(256); GetWindowTextA(h, t, 256); GetClassNameA(h, c, 256);
                r.Add(string.Format("{0:X} visible={1} class='{2}' title='{3}'", h.ToInt64(), IsWindowVisible(h), c, t)); }
            return true; }, IntPtr.Zero);
        return r;
    }
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
    public static List<string> Children(IntPtr dlg) {
        var r = new List<string>();
        EnumChildWindows(dlg, (h, l) => { var t = new StringBuilder(256); var c = new StringBuilder(256); GetWindowTextA(h, t, 256); GetClassNameA(h, c, 256);
            r.Add(string.Format("id={0} class='{1}' text='{2}'", GetDlgCtrlID(h), c, t)); return true; }, IntPtr.Zero);
        return r;
    }
    public static string ClickOk(IntPtr dlg) {
        string hit = null;
        EnumChildWindows(dlg, (h, l) => { var t = new StringBuilder(256); var c = new StringBuilder(256); GetWindowTextA(h, t, 256); GetClassNameA(h, c, 256);
            if (hit == null && c.ToString() == "Button" && t.ToString().Replace("&", "").Trim().ToUpper() == "START") { PostMessageA(h, 0x00F5, IntPtr.Zero, IntPtr.Zero); hit = t.ToString(); }
            return true; }, IntPtr.Zero);
        return hit;
    }
}
"@

"disabled windows before: {0}" -f [T]::Disabled(0)
$p = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -PassThru
try {
    $deadline = (Get-Date).AddSeconds(15)
    do { Start-Sleep -Milliseconds 300; $p.Refresh() } until ($p.HasExited -or $p.MainWindowHandle -ne 0 -or (Get-Date) -gt $deadline)
    if ($p.HasExited) { throw "game exited early, code $($p.ExitCode)" }
    "dialog: '{0}'  modules from game dir: {1}" -f $p.MainWindowTitle, (($p.Modules | Where-Object { $_.FileName -like "$(Split-Path $Exe)\*" } | ForEach-Object ModuleName) -join ', ')
    "dialog controls:"; [T]::Children($p.MainWindowHandle) | ForEach-Object { "  $_" }
    "clicked: " + [T]::ClickOk($p.MainWindowHandle)
    Start-Sleep -Seconds $RunSec
    $p.Refresh()
    if ($p.HasExited) { "game exited, code $($p.ExitCode)" } else {
        "game windows:"; [T]::Windows($p.Id) | ForEach-Object { "  $_" }
        "cpu {0:N1}s ws {1}MB" -f $p.TotalProcessorTime.TotalSeconds, [int]($p.WorkingSet64 / 1MB)
        "disabled windows while running: {0}" -f [T]::Disabled($p.Id)
    }
} finally {
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; "killed pid $($p.Id)" }
}
Start-Sleep -Milliseconds 500
"disabled windows after kill: {0}" -f [T]::Disabled(0)
