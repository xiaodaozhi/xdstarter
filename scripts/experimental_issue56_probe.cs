using System;
using System.Runtime.InteropServices;
using System.Text;

internal static class ShellNameProbe
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct SHFILEINFO
    {
        public IntPtr icon;
        public int iconIndex;
        public uint attributes;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string name;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 80)] public string type;
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr SHGetFileInfoW(string path, uint attributes, ref SHFILEINFO info,
        uint size, uint flags);
    [DllImport("ole32.dll")]
    private static extern int CoInitializeEx(IntPtr reserved, uint flags);
    [DllImport("ole32.dll")]
    private static extern void CoUninitialize();

    [STAThread]
    private static int Main(string[] args)
    {
        if (args.Length != 1) return 2;
        int com = CoInitializeEx(IntPtr.Zero, 2);
        if (com < 0) return 3;
        try
        {
            var info = new SHFILEINFO();
            var result = SHGetFileInfoW(args[0], 0, ref info,
                (uint)Marshal.SizeOf(typeof(SHFILEINFO)), 0x200);
            string encoded = Convert.ToBase64String(Encoding.UTF8.GetBytes(info.name ?? ""));
            Console.WriteLine("{0}|{1}|{2}", System.Diagnostics.Process.GetCurrentProcess().Id,
                result == IntPtr.Zero ? 0 : 1, encoded);
            return result == IntPtr.Zero ? 4 : 0;
        }
        finally { CoUninitialize(); }
    }
}
