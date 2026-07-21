using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace Doio.Kb16.Mapper;

public static class AppIdentity
{
    public const string ApplicationUserModelId = "Doio.Kb16.CodexMapper";
    private const ushort VtLpwstr = 31;
    private const int DwmwaBorderColor = 34;
    private const int DwmwaCaptionColor = 35;
    private const int DwmwaTextColor = 36;
    private static readonly PropertyKey AppUserModelIdKey = new(new Guid("9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3"), 5);

    public static int ApplyToCurrentProcess() => SetCurrentProcessExplicitAppUserModelID(ApplicationUserModelId);

    public static string? ReadCurrentProcessApplicationUserModelId()
    {
        var result = GetCurrentProcessExplicitAppUserModelID(out var value);
        if (result < 0 || value == IntPtr.Zero) return null;
        try { return Marshal.PtrToStringUni(value); }
        finally { Marshal.FreeCoTaskMem(value); }
    }

    public static int ApplyToWindow(Window window)
    {
        var handle = new WindowInteropHelper(window).Handle;
        var interfaceId = typeof(IPropertyStore).GUID;
        var result = SHGetPropertyStoreForWindow(handle, ref interfaceId, out var store);
        if (result < 0) return result;

        var value = new PropVariant
        {
            VariantType = VtLpwstr,
            PointerValue = Marshal.StringToCoTaskMemUni(ApplicationUserModelId),
        };
        try
        {
            var key = AppUserModelIdKey;
            result = store.SetValue(ref key, ref value);
            return result < 0 ? result : store.Commit();
        }
        finally
        {
            Marshal.FreeCoTaskMem(value.PointerValue);
            Marshal.ReleaseComObject(store);
        }
    }

    public static void ApplyWarmTitleBar(Window window)
    {
        if (!OperatingSystem.IsWindowsVersionAtLeast(10, 0, 22000)) return;
        var handle = new WindowInteropHelper(window).Handle;
        if (handle == IntPtr.Zero) return;
        TrySetDwmColor(handle, DwmwaCaptionColor, 0xF2, 0xF1, 0xEC);
        TrySetDwmColor(handle, DwmwaTextColor, 0x24, 0x24, 0x20);
        TrySetDwmColor(handle, DwmwaBorderColor, 0xD1, 0xD0, 0xC9);
    }

    private static void TrySetDwmColor(IntPtr handle, int attribute, byte red, byte green, byte blue)
    {
        try
        {
            var colorRef = red | green << 8 | blue << 16;
            _ = DwmSetWindowAttribute(handle, attribute, ref colorRef, sizeof(int));
        }
        catch (DllNotFoundException) { }
        catch (EntryPointNotFoundException) { }
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode, PreserveSig = true)]
    private static extern int SetCurrentProcessExplicitAppUserModelID(string applicationId);

    [DllImport("shell32.dll", PreserveSig = true)]
    private static extern int GetCurrentProcessExplicitAppUserModelID(out IntPtr applicationId);

    [DllImport("shell32.dll", PreserveSig = true)]
    private static extern int SHGetPropertyStoreForWindow(IntPtr windowHandle, ref Guid interfaceId, [MarshalAs(UnmanagedType.Interface)] out IPropertyStore propertyStore);

    [DllImport("dwmapi.dll", PreserveSig = true)]
    private static extern int DwmSetWindowAttribute(IntPtr windowHandle, int attribute, ref int attributeValue, int attributeSize);

    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    private struct PropertyKey(Guid formatId, uint propertyId)
    {
        public Guid FormatId = formatId;
        public uint PropertyId = propertyId;
    }

    [StructLayout(LayoutKind.Explicit)]
    private struct PropVariant
    {
        [FieldOffset(0)] public ushort VariantType;
        [FieldOffset(8)] public IntPtr PointerValue;
    }

    [ComImport]
    [Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IPropertyStore
    {
        [PreserveSig] int GetCount(out uint propertyCount);
        [PreserveSig] int GetAt(uint propertyIndex, out PropertyKey key);
        [PreserveSig] int GetValue(ref PropertyKey key, out PropVariant value);
        [PreserveSig] int SetValue(ref PropertyKey key, ref PropVariant value);
        [PreserveSig] int Commit();
    }
}
