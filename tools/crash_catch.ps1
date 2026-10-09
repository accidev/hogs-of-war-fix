# Run under 32-bit Windows PowerShell.
# Starts the game under a minimal debugger, presses Start in the launcher dialog, waits RunSec,
# then acts on the game window and reports access violations with registers and a stack scan,
# mapped to modules. Puts back windows the game moved.
#   -Action minimize  minimize the window (what Alt+Tab does to it), wait AfterSec, kill
#   -Action alttab    Alt+Tab away and back
#   -Action tour      every 4 s a PrintWindow shot (-Shot x.png gives x_0.png ...), the first
#                     -EscRounds of them with Esc posted to skip the intro videos; 8 rounds
#   -Action none      only wait AfterSec, kill
param([string]$Exe, [int]$RunSec = 10, [int]$AfterSec = 6,
      [ValidateSet('minimize', 'alttab', 'tour', 'none')][string]$Action = 'minimize', [string]$Shot, [int]$EscRounds = 2)

Add-Type -ReferencedAssemblies System.Drawing @"
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class Dbg {
    [StructLayout(LayoutKind.Sequential)] public struct STARTUPINFO { public int cb; public IntPtr r1, desk, title; public int x, y, w, h, cx, cy, fill, flags; public short show, r2; public IntPtr r3, i, o, e; }
    [StructLayout(LayoutKind.Sequential)] public struct PROCINFO { public IntPtr hProcess, hThread; public int pid, tid; }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool CreateProcessW(string app, string cmd, IntPtr pa, IntPtr ta, bool inh, int flags, IntPtr env, string dir, ref STARTUPINFO si, out PROCINFO pi);
    [DllImport("kernel32.dll")] static extern bool WaitForDebugEvent(byte[] ev, int ms);
    [DllImport("kernel32.dll")] static extern bool ContinueDebugEvent(int pid, int tid, uint status);
    [DllImport("kernel32.dll")] static extern bool TerminateProcess(IntPtr h, int code);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] static extern IntPtr OpenThread(int a, bool i, int tid);
    [DllImport("kernel32.dll")] static extern bool GetThreadContext(IntPtr h, byte[] ctx);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, int n, out int r);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] static extern int GetFinalPathNameByHandleW(IntPtr h, StringBuilder s, int n, int f);
    public delegate bool EP(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EP cb, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr p, EP cb, IntPtr l);
    [DllImport("user32.dll")] static extern int GetWindowThreadProcessId(IntPtr h, out int p);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll", CharSet=CharSet.Ansi)] static extern int GetClassNameA(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet=CharSet.Ansi)] static extern int GetWindowTextA(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] static extern bool PostMessageA(IntPtr h, int m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, int flags, IntPtr extra);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern int GetWindowLongW(IntPtr h, int i);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr hdc, int flags);
    static void AltTab() {
        keybd_event(0x12, 0, 0, IntPtr.Zero); keybd_event(0x09, 0, 0, IntPtr.Zero);
        System.Threading.Thread.Sleep(50);
        keybd_event(0x09, 0, 2, IntPtr.Zero); keybd_event(0x12, 0, 2, IntPtr.Zero);
    }

    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern bool EnumDisplaySettingsW(string dev, int mode, byte[] dm);
    [DllImport("user32.dll")] static extern bool GetWindowPlacement(IntPtr h, byte[] wp);
    [DllImport("user32.dll")] static extern bool SetWindowPlacement(IntPtr h, byte[] wp);

    // Primary display mode "WxHxBPP" (DEVMODEW: BitsPerPel @168, PelsWidth @172, PelsHeight @176).
    static string Mode() {
        byte[] dm = new byte[220]; BitConverter.GetBytes((short)220).CopyTo(dm, 68);
        if (!EnumDisplaySettingsW(null, -1, dm)) return "?";
        return string.Format("{0}x{1}x{2}", BitConverter.ToInt32(dm, 172), BitConverter.ToInt32(dm, 176), BitConverter.ToInt32(dm, 168));
    }
    // Remember where every visible window is, so a mode switch that shoves them to another monitor can be undone.
    static List<KeyValuePair<IntPtr, byte[]>> SavePlacements(int skipPid) {
        var r = new List<KeyValuePair<IntPtr, byte[]>>();
        EnumWindows((h, l) => { int p; GetWindowThreadProcessId(h, out p);
            if (p != skipPid && IsWindowVisible(h)) { byte[] wp = new byte[44]; BitConverter.GetBytes(44).CopyTo(wp, 0); if (GetWindowPlacement(h, wp)) r.Add(new KeyValuePair<IntPtr, byte[]>(h, wp)); }
            return true; }, IntPtr.Zero);
        return r;
    }
    public static int RestorePlacements(List<KeyValuePair<IntPtr, byte[]>> saved) {
        int n = 0;
        foreach (var kv in saved) {
            byte[] now = new byte[44]; BitConverter.GetBytes(44).CopyTo(now, 0);
            if (!GetWindowPlacement(kv.Key, now)) continue;
            bool moved = false; for (int i = 8; i < 44; i++) if (now[i] != kv.Value[i]) { moved = true; break; }
            if (moved && SetWindowPlacement(kv.Key, kv.Value)) n++;
        }
        return n;
    }

    static List<KeyValuePair<long, string>> mods = new List<KeyValuePair<long, string>>();
    static List<string> log = new List<string>();
    static IntPtr hProc;

    static string Name(IntPtr hFile) {
        var sb = new StringBuilder(512);
        if (hFile == IntPtr.Zero || GetFinalPathNameByHandleW(hFile, sb, 512, 0) == 0) return "?";
        string s = sb.ToString(); int i = s.LastIndexOf('\\'); return i >= 0 ? s.Substring(i + 1) : s;
    }
    static string Where(long a) {
        KeyValuePair<long, string> best = new KeyValuePair<long, string>(0, null);
        foreach (var m in mods) if (m.Key <= a && m.Key > best.Key) best = m;
        if (best.Value == null || a - best.Key > 0x2000000) return string.Format("0x{0:X8}", a);
        return string.Format("{0}+0x{1:X}", best.Value, a - best.Key);
    }
    static IntPtr Window(int pid, string cls) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => { int p; GetWindowThreadProcessId(h, out p); var c = new StringBuilder(64); GetClassNameA(h, c, 64);
            if (p == pid && IsWindowVisible(h) && c.ToString() == cls) found = h; return true; }, IntPtr.Zero);
        return found;
    }
    static bool ClickStart(IntPtr dlg) {
        bool hit = false;
        EnumChildWindows(dlg, (h, l) => { var t = new StringBuilder(64); GetWindowTextA(h, t, 64);
            if (!hit && t.ToString() == "Start") { PostMessageA(h, 0x00F5, IntPtr.Zero, IntPtr.Zero); hit = true; } return true; }, IntPtr.Zero);
        return hit;
    }
    static void Dump(int tid, uint code, long addr, bool first) {
        log.Add(string.Format("EXCEPTION {0:X8} at {1} first_chance={2} tid={3}", code, Where(addr), first, tid));
        IntPtr ht = OpenThread(0x0008 | 0x0040, false, tid);
        byte[] ctx = new byte[716]; BitConverter.GetBytes(0x10007).CopyTo(ctx, 0);
        if (!GetThreadContext(ht, ctx)) { log.Add("  no context"); CloseHandle(ht); return; }
        CloseHandle(ht);
        // x86 CONTEXT offsets: Edi 9C Esi A0 Ebx A4 Edx A8 Ecx AC Eax B0 Ebp B4 Eip B8 Esp C4
        string[] names = { "eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp", "eip" };
        int[] offs = { 0xB0, 0xA4, 0xAC, 0xA8, 0xA0, 0x9C, 0xB4, 0xC4, 0xB8 };
        var sb = new StringBuilder("  ");
        for (int i = 0; i < names.Length; i++) sb.AppendFormat("{0}={1:X8} ", names[i], BitConverter.ToUInt32(ctx, offs[i]));
        log.Add(sb.ToString());
        long esp = BitConverter.ToUInt32(ctx, 0xC4);
        byte[] st = new byte[4096]; int n;
        ReadProcessMemory(hProc, new IntPtr(esp), st, st.Length, out n);
        var frames = new List<string>();
        for (int o = 0; o + 4 <= n && frames.Count < 40; o += 4) {
            long v = BitConverter.ToUInt32(st, o); string w = Where(v);
            if (!w.StartsWith("0x")) frames.Add(string.Format("[esp+{0:X}] {1}", o, w));
        }
        log.Add("  stack: " + string.Join("  ", frames.ToArray()));
        byte[] code16 = new byte[16];
        if (ReadProcessMemory(hProc, new IntPtr(addr), code16, 16, out n)) log.Add("  code: " + BitConverter.ToString(code16));
    }

    public static string shot;
    public static int escRounds = 2;
    public static List<string> Run(string exe, int runSec, int afterSec, string action) {
        var si = new STARTUPINFO(); si.cb = Marshal.SizeOf(si); PROCINFO pi;
        if (!CreateProcessW(exe, "\"" + exe + "\"", IntPtr.Zero, IntPtr.Zero, false, 0x2, IntPtr.Zero, System.IO.Path.GetDirectoryName(exe), ref si, out pi))
            throw new Exception("CreateProcess failed " + Marshal.GetLastWin32Error());
        hProc = pi.hProcess;
        var placements = SavePlacements(pi.pid);
        string mode = Mode(); log.Add("display mode at start: " + mode);
        byte[] ev = new byte[96 + 160];
        DateTime start = DateTime.Now, clicked = DateTime.MinValue, acted = DateTime.MinValue;
        bool done = false, returned = false; int avCount = 0, tour = 0;
        while (!done) {
            if (WaitForDebugEvent(ev, 100)) {
                int code = BitConverter.ToInt32(ev, 0), pid = BitConverter.ToInt32(ev, 4), tid = BitConverter.ToInt32(ev, 8);
                uint status = 0x00010002; // DBG_CONTINUE
                switch (code) {
                    case 1: { // EXCEPTION_DEBUG_EVENT: ExceptionRecord at +12, dwFirstChance after record (80 bytes)
                        uint exc = BitConverter.ToUInt32(ev, 12); long addr = BitConverter.ToUInt32(ev, 12 + 12);
                        bool first = BitConverter.ToInt32(ev, 12 + 80) != 0;
                        if (exc == 0x80000003 || exc == 0x4000001F) break; // initial breakpoints
                        status = 0x80010001; // DBG_EXCEPTION_NOT_HANDLED
                        if (exc == 0xC0000005 || !first) {
                            if (avCount++ < 5 || !first) Dump(tid, exc, addr, first);
                            if (!first) { TerminateProcess(hProc, 1); }
                        } else if (exc != 0x406D1388) log.Add(string.Format("exception {0:X8} at {1} (first chance)", exc, Where(addr)));
                        break; }
                    case 3: { // CREATE_PROCESS: hFile +12, base +20
                        IntPtr hf = new IntPtr(BitConverter.ToInt32(ev, 12));
                        mods.Add(new KeyValuePair<long, string>(BitConverter.ToUInt32(ev, 20), Name(hf))); CloseHandle(hf); break; }
                    case 6: { // LOAD_DLL: hFile +12, base +16
                        IntPtr hf = new IntPtr(BitConverter.ToInt32(ev, 12));
                        mods.Add(new KeyValuePair<long, string>(BitConverter.ToUInt32(ev, 16), Name(hf))); CloseHandle(hf); break; }
                    case 5: log.Add("process exited, code " + BitConverter.ToUInt32(ev, 12)); done = true; break;
                }
                ContinueDebugEvent(pid, tid, status);
            }
            double t = (DateTime.Now - start).TotalSeconds;
            string m = Mode(); if (m != mode) { log.Add(string.Format("display mode -> {0} at {1:N1}s", m, t)); mode = m; }
            if (clicked == DateTime.MinValue) {
                IntPtr dlg = Window(pi.pid, "#32770");
                if (dlg != IntPtr.Zero && ClickStart(dlg)) { clicked = DateTime.Now; log.Add(string.Format("clicked Start at {0:N1}s", t)); }
            } else if (acted == DateTime.MinValue && (DateTime.Now - clicked).TotalSeconds > runSec) {
                IntPtr w = Window(pi.pid, "PigsWClass");
                RECT rc; GetWindowRect(w, out rc);
                if (shot != null && rc.R > rc.L && rc.B > rc.T) {
                    using (var bmp = new System.Drawing.Bitmap(rc.R - rc.L, rc.B - rc.T))
                    using (var g = System.Drawing.Graphics.FromImage(bmp)) {
                        g.CopyFromScreen(rc.L, rc.T, 0, 0, bmp.Size); bmp.Save(shot, System.Drawing.Imaging.ImageFormat.Png);
                    }
                    log.Add("screenshot: " + shot);
                }
                log.Add(string.Format("game window {0:X} rect ({1},{2})-({3},{4}) size {5}x{6} style {7:X8}; action {8}", w.ToInt64(), rc.L, rc.T, rc.R, rc.B, rc.R - rc.L, rc.B - rc.T, GetWindowLongW(w, -16), action));
                if (action == "minimize") ShowWindow(w, 6); // SW_MINIMIZE
                if (action == "alttab") { log.Add(string.Format("foreground before: {0:X}", GetForegroundWindow().ToInt64())); AltTab(); }
                acted = DateTime.Now;
            } else if (action == "tour" && acted != DateTime.MinValue && acted != DateTime.MaxValue) {
                // every 4 s: screenshot + Esc (skips intro videos); 8 rounds
                if ((DateTime.Now - acted).TotalSeconds > 4 * (tour + 1)) {
                    IntPtr w = Window(pi.pid, "PigsWClass");
                    RECT rc; GetWindowRect(w, out rc);
                    // PrintWindow renders only the game window, even when other windows cover it
                    if (shot != null && rc.R > rc.L) {
                        string f = shot.Replace(".png", "_" + tour + ".png");
                        using (var bmp = new System.Drawing.Bitmap(rc.R - rc.L, rc.B - rc.T))
                        using (var g = System.Drawing.Graphics.FromImage(bmp)) {
                            IntPtr hdc = g.GetHdc(); bool ok = PrintWindow(w, hdc, 2); g.ReleaseHdc(hdc);
                            bmp.Save(f, System.Drawing.Imaging.ImageFormat.Png);
                            log.Add(string.Format("{0:N1}s shot {1} printwindow={2}", t, f, ok));
                        }
                    }
                    // WM_KEYDOWN Esc sets the game's video-skip flag (MainWndProc); stop after two,
                    // or it reaches "Really quit app?" in the menu
                    if (tour < escRounds) { PostMessageA(w, 0x0100, new IntPtr(0x1B), new IntPtr(0x00010001)); PostMessageA(w, 0x0101, new IntPtr(0x1B), new IntPtr(unchecked((int)0xC0010001))); }
                    if (++tour >= 8) { log.Add("tour done; killing"); TerminateProcess(hProc, 0); acted = DateTime.MaxValue; }
                }
            } else if (action == "alttab" && acted != DateTime.MinValue && acted != DateTime.MaxValue && !returned && (DateTime.Now - acted).TotalSeconds > 3) {
                log.Add(string.Format("foreground after alt-tab: {0:X}; alt-tab back at {1:N1}s", GetForegroundWindow().ToInt64(), t));
                AltTab(); returned = true;
            } else if (action != "tour" && acted != DateTime.MinValue && acted != DateTime.MaxValue && (DateTime.Now - acted).TotalSeconds > afterSec) {
                log.Add("survived " + afterSec + "s after action; killing"); TerminateProcess(hProc, 0); acted = DateTime.MaxValue;
            }
            if (t > 120) { log.Add("timeout"); TerminateProcess(hProc, 0); }
        }
        System.Threading.Thread.Sleep(1500);
        log.Add("display mode at end: " + Mode() + "; windows put back: " + RestorePlacements(placements));
        return log;
    }
}
"@

if ($Shot) { [Dbg]::shot = $Shot }
[Dbg]::escRounds = $EscRounds
[Dbg]::Run($Exe, $RunSec, $AfterSec, $Action)
