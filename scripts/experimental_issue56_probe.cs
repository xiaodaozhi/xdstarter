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
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern int SHParseDisplayName(string name, IntPtr binding, out IntPtr idList,
        uint attributes, out uint requestedAttributes);
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern int SHGetNameFromIDList(IntPtr idList, uint format, out IntPtr name);
    [DllImport("shell32.dll")]
    private static extern IntPtr ILClone(IntPtr idList);
    [DllImport("shell32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ILRemoveLastID(IntPtr idList);
    [DllImport("shell32.dll")]
    private static extern void ILFree(IntPtr idList);
    [DllImport("ole32.dll")]
    private static extern int CoInitializeEx(IntPtr reserved, uint flags);
    [DllImport("ole32.dll")]
    private static extern void CoUninitialize();

    private static string Name(IntPtr idList, uint format)
    {
        IntPtr value;
        int result = SHGetNameFromIDList(idList, format, out value);
        if (result < 0) return String.Format("<HRESULT:{0:X8}>", result);
        try { return Marshal.PtrToStringUni(value) ?? ""; }
        finally { Marshal.FreeCoTaskMem(value); }
    }

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
            IntPtr idList;
            uint attrs;
            int parsed = SHParseDisplayName(args[0], IntPtr.Zero, out idList, 0, out attrs);
            var route = new StringBuilder();
            if (parsed < 0) route.AppendFormat("<parse HRESULT:{0:X8}>", parsed);
            else
            {
                try
                {
                    var cursor = ILClone(idList);
                    if (cursor == IntPtr.Zero) throw new InvalidOperationException("Cannot clone PIDL");
                    try
                    {
                        for (int i = 0; i < 32; ++i)
                        {
                            route.Append(Name(cursor, 0)).Append('\t')
                                 .Append(Name(cursor, 0x80028000)).Append('\n');
                            if (!ILRemoveLastID(cursor)) break;
                        }
                    }
                    finally { ILFree(cursor); }
                }
                finally { Marshal.FreeCoTaskMem(idList); }
            }
            string encodedRoute = Convert.ToBase64String(Encoding.UTF8.GetBytes(route.ToString()));
            IntPtr computerItem;
            uint computerAttrs;
            int computerResult = SHParseDisplayName(
                @"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}\" + args[0], IntPtr.Zero,
                out computerItem, 0, out computerAttrs);
            string computer = String.Format("<HRESULT:{0:X8}>", computerResult);
            if (computerResult >= 0)
            {
                try { computer = Name(computerItem, 0) + "\t" + Name(computerItem, 0x80058000); }
                finally { Marshal.FreeCoTaskMem(computerItem); }
            }
            string encodedComputer = Convert.ToBase64String(Encoding.UTF8.GetBytes(computer));
            Console.WriteLine("{0}|{1}|{2}|{3}|{4}", System.Diagnostics.Process.GetCurrentProcess().Id,
                result == IntPtr.Zero ? 0 : 1, encoded, encodedRoute, encodedComputer);
            return result == IntPtr.Zero ? 4 : 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine("probe failure type: " + error.GetType().FullName);
            return 6;
        }
        finally { CoUninitialize(); }
    }
}
