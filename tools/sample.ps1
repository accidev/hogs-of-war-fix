# Run under 32-bit Windows PowerShell. Starts the game, samples thread EIPs, lists windows, kills it.
param([string]$Exe, [int]$Samples = 24, [int]$IntervalMs = 500)

Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class Smp {
    [DllImport("kernel32.dll")] public static extern IntPtr OpenThread(int access, bool inherit, int tid);
    [DllImport("kernel32.dll")] public static extern int SuspendThread(IntPtr h);
    [DllImport("kernel32.dll")] public static extern int ResumeThread(IntPtr h);
    [DllImport("kernel32.dll")] public static extern bool GetThreadContext(IntPtr h, byte[] ctx);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, int size, out int read);
    public delegate bool EnumProc(IntPtr hwnd, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(IntPtr hwnd, out int pid);
    [DllImport("user32.dll", CharSet=CharSet.Ansi)] public static extern int GetWindowTextA(IntPtr hwnd, StringBuilder sb, int max);
    [DllImport("user32.dll", CharSet=CharSet.Ansi)] public static extern int GetClassNameA(IntPtr hwnd, StringBuilder sb, int max);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);

    // x86 CONTEXT: ContextFlags @0, Ebp @0xB4, Eip @0xB8, Esp @0xC4
    public static uint[] Regs(int tid) {
        IntPtr h = OpenThread(0x0002 | 0x0008 | 0x0040, false, tid);
        if (h == IntPtr.Zero) return null;
        byte[] ctx = new byte[716];
        BitConverter.GetBytes(0x10001).CopyTo(ctx, 0);
        uint[] r = null;
        if (SuspendThread(h) != -1) {
            if (GetThreadContext(h, ctx))
                r = new uint[] { BitConverter.ToUInt32(ctx, 0xB8), BitConverter.ToUInt32(ctx, 0xC4) };
            ResumeThread(h);
        }
        CloseHandle(h);
        return r;
    }
    public static List<string> Windows(int pid) {
        var res = new List<string>();
        EnumWindows((hw, lp) => {
            int p; GetWindowThreadProcessId(hw, out p);
            if (p == pid) {
                var t = new StringBuilder(256); var c = new StringBuilder(256);
                GetWindowTextA(hw, t, 256); GetClassNameA(hw, c, 256);
                res.Add(string.Format("hwnd={0:X} visible={1} class='{2}' title='{3}'", hw.ToInt64(), IsWindowVisible(hw), c, t));
            }
            return true;
        }, IntPtr.Zero);
        return res;
    }
}
"@

$p = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -PassThru
$hits = @{}
try {
    for ($i = 0; $i -lt $Samples; $i++) {
        Start-Sleep -Milliseconds $IntervalMs
        $p.Refresh()
        if ($p.HasExited) { "exited early, code $($p.ExitCode)"; break }
        $mods = $p.Modules | Sort-Object { $_.BaseAddress.ToInt64() }
        foreach ($t in $p.Threads) {
            $r = [Smp]::Regs($t.Id)
            if (-not $r) { continue }
            $eip = [int64]$r[0]
            $m = $mods | Where-Object { $eip -ge $_.BaseAddress.ToInt64() -and $eip -lt ($_.BaseAddress.ToInt64() + $_.ModuleMemorySize) } | Select-Object -First 1
            $loc = if ($m) { "{0}+0x{1:X}" -f $m.ModuleName, ($eip - $m.BaseAddress.ToInt64()) } else { "0x{0:X8}" -f $eip }
            $key = "tid $($t.Id) $loc"
            $hits[$key] = 1 + [int]$hits[$key]
        }
    }
    "--- EIP samples (thread, location, count)"
    $hits.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 25 | ForEach-Object { "{0,4}  {1}" -f $_.Value, $_.Key }
    "--- stack scan (return-address candidates, first 30 per thread)"
    $mods = $p.Modules
    $hp = [Smp]::OpenProcess(0x0410,$false, $p.Id)
    foreach ($t in $p.Threads) {
        $r = [Smp]::Regs($t.Id)
        if (-not $r) { continue }
        $buf = New-Object byte[] 8192
        $n = 0
        [void][Smp]::ReadProcessMemory($hp, [IntPtr][int64]$r[1], $buf, $buf.Length, [ref]$n)
        $out = @()
        for ($o = 0; $o -lt $n -and $out.Count -lt 30; $o += 4) {
            $v = [int64][BitConverter]::ToUInt32($buf, $o)
            $m = $mods | Where-Object { $v -ge $_.BaseAddress.ToInt64() -and $v -lt ($_.BaseAddress.ToInt64() + $_.ModuleMemorySize) } | Select-Object -First 1
            if ($m) { $out += "{0}+0x{1:X}" -f $m.ModuleName, ($v - $m.BaseAddress.ToInt64()) }
        }
        "tid $($t.Id) eip={0:X8} esp={1:X8}: {2}" -f $r[0], $r[1], ($out -join ' ')
    }
    "--- windows"
    [Smp]::Windows($p.Id)
    "--- modules"
    $p.Modules | ForEach-Object { "{0:X8} {1}" -f $_.BaseAddress.ToInt64(), $_.FileName }
    "--- cpu {0:N1}s ws {1}MB" -f $p.TotalProcessorTime.TotalSeconds, [int]($p.WorkingSet64 / 1MB)
} finally {
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; "killed pid $($p.Id)" }
}
