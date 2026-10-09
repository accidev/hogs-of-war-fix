# Re-enables every disabled top-level window (the unpatched Hogs of War disables them all and
# only restores them on a clean exit). Fallback only: it also enables windows that an app
# disabled on purpose behind its own modal dialog.
param([string]$Log)
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class Fix {
  public delegate bool EP(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EP cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
  [DllImport("user32.dll")] public static extern bool EnableWindow(IntPtr h, bool en);
  public static int[] Run() {
    int was = 0, now = 0;
    EnumWindows((h, l) => { if (!IsWindowEnabled(h)) { was++; EnableWindow(h, true); if (!IsWindowEnabled(h)) now++; } return true; }, IntPtr.Zero);
    return new int[] { was, now };
  }
}
"@
$r = [Fix]::Run()
$msg = "disabled before: $($r[0]); still disabled after: $($r[1])"
if ($Log) { $msg | Set-Content $Log } else { $msg }
