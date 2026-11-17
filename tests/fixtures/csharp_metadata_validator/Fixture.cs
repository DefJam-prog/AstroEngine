using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace AstroEngine.Tests.Инспектор;

internal static class MustNotRun
{
    [ModuleInitializer]
    internal static void Initialize() => throw new InvalidOperationException("Fixture module initializer executed.");
}

public static class ValidBinding
{
    [UnmanagedCallersOnly]
    public static int OnCreate(IntPtr context, int size) => 0;
    [UnmanagedCallersOnly]
    public static int FixedUpdate(IntPtr context, int size) => 0;
    [UnmanagedCallersOnly]
    public static int OnDestroy(IntPtr context, int size) => 0;
}

public static class InvalidBinding
{
    [UnmanagedCallersOnly]
    public static int OnCreate(int context, int size) => 0;
    [UnmanagedCallersOnly]
    public static int FixedUpdate(int context, int size) => 0;
    [UnmanagedCallersOnly]
    public static int OnDestroy(int context, int size) => 0;
}
