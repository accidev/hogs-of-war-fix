# Run under 32-bit Windows PowerShell.
# Starts the game, waits for its launcher dialog (LaserLock is unpacked by then),
# dumps every module image from the game folder page by page, kills the game.
param([string]$Exe, [string]$OutDir, [int]$TimeoutSec = 15)

Add-Type @"
using System; using System.Runtime.InteropServices;
public static class Dmp {
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, int size, out int read);
    public static byte[] Image(int pid, long b, int size) {
        IntPtr h = OpenProcess(0x0410, false, pid);
        byte[] img = new byte[size], page = new byte[4096];
        for (int o = 0; o < size; o += 4096) {
            int n;
            if (ReadProcessMemory(h, new IntPtr(b + o), page, 4096, out n))
                Array.Copy(page, 0, img, o, Math.Min(n, size - o));
        }
        return img;
    }
}
"@

New-Item -ItemType Directory -Force $OutDir | Out-Null
$p = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -PassThru
try {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do { Start-Sleep -Milliseconds 300; $p.Refresh() } until ($p.HasExited -or $p.MainWindowHandle -ne 0 -or (Get-Date) -gt $deadline)
    if ($p.HasExited) { throw "game exited early, code $($p.ExitCode)" }
    "window=0x{0:X} title='{1}'" -f $p.MainWindowHandle.ToInt64(), $p.MainWindowTitle
    $dir = Split-Path $Exe
    foreach ($m in $p.Modules) {
        "{0:X8} {1:X8} {2}" -f $m.BaseAddress.ToInt64(), $m.ModuleMemorySize, $m.FileName
        if ($m.FileName.StartsWith($dir, [StringComparison]::OrdinalIgnoreCase)) {
            $f = Join-Path $OutDir ("{0}_{1:X8}.bin" -f $m.ModuleName, $m.BaseAddress.ToInt64())
            [IO.File]::WriteAllBytes($f, [Dmp]::Image($p.Id, $m.BaseAddress.ToInt64(), $m.ModuleMemorySize))
        }
    }
} finally {
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; "killed pid $($p.Id)" }
}
