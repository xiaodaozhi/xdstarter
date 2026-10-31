# Read-only observations for Shell fixture paths. URL zone mapping is not a
# documented explanation of the desktop.ini trust decision.
if (-not ('XDStarterShellPathFacts' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

[ComImport, Guid("79EAC9EE-BAF9-11CE-8C82-00AA004BA90B"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IXDStarterSecurityManager {
    [PreserveSig] int SetSecuritySite(IntPtr site);
    [PreserveSig] int GetSecuritySite(out IntPtr site);
    [PreserveSig] int MapUrlToZone([MarshalAs(UnmanagedType.LPWStr)] string url, out uint zone, uint flags);
}

public sealed class XDStarterZoneObservation {
    public int? managerHResult;
    public bool managerAvailable;
    public int? mapHResult;
    public long? zone;
    public string errorType;
    public int? errorHResult;
}

public sealed class XDStarterVolumeObservation {
    public bool mountPathAvailable;
    public int mountPathError;
    public string mountPath;
    public bool volumeNameAvailable;
    public int volumeNameError;
    public string volumeName;
}

public static class XDStarterShellPathFacts {
    [DllImport("urlmon.dll")]
    private static extern int CoInternetCreateSecurityManager(IntPtr provider,
        [MarshalAs(UnmanagedType.Interface)] out IXDStarterSecurityManager manager, uint reserved);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern bool GetVolumePathNameW(string path, StringBuilder mountPath, uint length);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern bool GetVolumeNameForVolumeMountPointW(string mountPath, StringBuilder volumeName, uint length);

    public static XDStarterZoneObservation Zone(string path) {
        var observation = new XDStarterZoneObservation();
        IXDStarterSecurityManager manager = null;
        try {
            int created = CoInternetCreateSecurityManager(IntPtr.Zero, out manager, 0);
            observation.managerHResult = created;
            observation.managerAvailable = manager != null;
            if (created < 0 || manager == null) return observation;
            uint zone;
            int mapped = manager.MapUrlToZone(new Uri(path).AbsoluteUri, out zone, 0);
            observation.mapHResult = mapped;
            if (mapped >= 0) observation.zone = zone;
        } catch (Exception error) {
            observation.errorType = error.GetType().FullName;
            observation.errorHResult = error.HResult;
        } finally {
            if (manager != null) Marshal.FinalReleaseComObject(manager);
        }
        return observation;
    }

    public static XDStarterVolumeObservation Volume(string path) {
        var observation = new XDStarterVolumeObservation();
        var mount = new StringBuilder(1024);
        observation.mountPathAvailable = GetVolumePathNameW(path, mount, (uint)mount.Capacity);
        if (!observation.mountPathAvailable) {
            observation.mountPathError = Marshal.GetLastWin32Error();
            return observation;
        }
        observation.mountPath = mount.ToString();
        var volume = new StringBuilder(1024);
        observation.volumeNameAvailable = GetVolumeNameForVolumeMountPointW(observation.mountPath, volume, (uint)volume.Capacity);
        if (observation.volumeNameAvailable) observation.volumeName = volume.ToString();
        else observation.volumeNameError = Marshal.GetLastWin32Error();
        return observation;
    }
}
'@
}

function Get-XDStarterStringHash([string]$Value) {
    $normalized = $Value.TrimEnd('\').ToUpperInvariant()
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData(
        [Text.Encoding]::UTF8.GetBytes($normalized))).ToLowerInvariant()
}

function Get-XDStarterPathFacts([string]$Path) {
    $zone = [XDStarterShellPathFacts]::Zone($Path)
    $volume = [XDStarterShellPathFacts]::Volume($Path)
    return @{
        urlZone = @{ managerHResult = $zone.managerHResult; managerAvailable = $zone.managerAvailable;
                     mapHResult = $zone.mapHResult; zone = $zone.zone;
                     errorType = $zone.errorType; errorHResult = $zone.errorHResult }
        volume = @{ mountPathAvailable = $volume.mountPathAvailable; mountPathError = $volume.mountPathError;
                    mountPathSha256 = if ($volume.mountPathAvailable) { Get-XDStarterStringHash $volume.mountPath } else { $null };
                    volumeNameAvailable = $volume.volumeNameAvailable; volumeNameError = $volume.volumeNameError;
                    volumeNameSha256 = if ($volume.volumeNameAvailable) { Get-XDStarterStringHash $volume.volumeName } else { $null } }
    }
}
