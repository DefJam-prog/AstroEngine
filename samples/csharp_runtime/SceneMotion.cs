using System;
using System.Runtime.InteropServices;

namespace AstroEngine.CSharpSample;

[StructLayout(LayoutKind.Sequential)]
public struct SceneMotionPayload
{
    public uint StructSize;
    public uint ApiVersion;
    public ulong EntityId;
    public double FixedDeltaSeconds;
    public IntPtr Api;
}

[StructLayout(LayoutKind.Sequential)]
public struct ScriptLifecyclePayload
{
    public uint StructSize;
    public uint ApiVersion;
    public ulong EntityId;
    public IntPtr Api;
}

[StructLayout(LayoutKind.Sequential)]
public struct ScriptTransformV1
{
    public uint StructSize;
    public uint ApiVersion;
    public double PositionX;
    public double PositionY;
    public double PositionZ;
    public double RotationX;
    public double RotationY;
    public double RotationZ;
    public double ScaleX;
    public double ScaleY;
    public double ScaleZ;
}

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int GetTransformCallback(IntPtr userData, ulong entityId, out ScriptTransformV1 transform);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int SetTransformCallback(IntPtr userData, ulong entityId, ref ScriptTransformV1 transform);

[StructLayout(LayoutKind.Sequential)]
public struct ScriptApiV1
{
    public uint StructSize;
    public uint ApiVersion;
    public IntPtr UserData;
    public IntPtr GetTransform;
    public IntPtr SetTransform;
}

public static class SceneMotion
{
    [UnmanagedCallersOnly]
    public static int OnCreate(IntPtr payload, int payloadSize) => ApplyLifecycle(payload, payloadSize, true);

    [UnmanagedCallersOnly]
    public static int OnDestroy(IntPtr payload, int payloadSize) => ApplyLifecycle(payload, payloadSize, false);

    private static int ApplyLifecycle(IntPtr payload, int payloadSize, bool creating)
    {
        if (payload == IntPtr.Zero || payloadSize != Marshal.SizeOf<ScriptLifecyclePayload>())
            return 1;
        try
        {
            var state = Marshal.PtrToStructure<ScriptLifecyclePayload>(payload);
            if (state.StructSize != Marshal.SizeOf<ScriptLifecyclePayload>() || state.ApiVersion != 1
                || state.EntityId == 0 || state.Api == IntPtr.Zero)
                return 2;
            var api = Marshal.PtrToStructure<ScriptApiV1>(state.Api);
            if (api.StructSize != Marshal.SizeOf<ScriptApiV1>() || api.ApiVersion != 1
                || api.GetTransform == IntPtr.Zero || api.SetTransform == IntPtr.Zero)
                return 3;
            var status = Marshal.GetDelegateForFunctionPointer<GetTransformCallback>(api.GetTransform)(
                api.UserData, state.EntityId, out var transform);
            if (status != 0)
                return status;
            if (transform.StructSize != Marshal.SizeOf<ScriptTransformV1>() || transform.ApiVersion != 1)
                return 4;
            if (creating) transform.PositionY = 2.0;
            else transform.PositionZ = 3.0;
            return Marshal.GetDelegateForFunctionPointer<SetTransformCallback>(api.SetTransform)(
                api.UserData, state.EntityId, ref transform);
        }
        catch
        {
            return 5;
        }
    }

    [UnmanagedCallersOnly]
    public static int FixedUpdate(IntPtr payload, int payloadSize)
    {
        if (payload == IntPtr.Zero || payloadSize != Marshal.SizeOf<SceneMotionPayload>())
            return 1;

        try
        {
            var state = Marshal.PtrToStructure<SceneMotionPayload>(payload);
            if (state.StructSize != Marshal.SizeOf<SceneMotionPayload>() || state.ApiVersion != 1
                || state.EntityId == 0 || state.Api == IntPtr.Zero)
                return 2;
            if (!double.IsFinite(state.FixedDeltaSeconds) || state.FixedDeltaSeconds <= 0.0)
                return 3;

            var api = Marshal.PtrToStructure<ScriptApiV1>(state.Api);
            if (api.StructSize != Marshal.SizeOf<ScriptApiV1>() || api.ApiVersion != 1
                || api.GetTransform == IntPtr.Zero || api.SetTransform == IntPtr.Zero)
                return 4;

            var getTransform = Marshal.GetDelegateForFunctionPointer<GetTransformCallback>(api.GetTransform);
            var status = getTransform(api.UserData, state.EntityId, out var transform);
            if (status != 0)
                return status;
            if (transform.StructSize != Marshal.SizeOf<ScriptTransformV1>() || transform.ApiVersion != 1)
                return 5;

            transform.PositionX += state.FixedDeltaSeconds;
            status = Marshal.GetDelegateForFunctionPointer<SetTransformCallback>(api.SetTransform)(
                api.UserData, state.EntityId, ref transform);
            if (status != 0)
                return status;
            return 0;
        }
        catch
        {
            return 3;
        }
    }
}
