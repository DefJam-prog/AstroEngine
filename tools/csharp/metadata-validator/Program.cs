using System.Net.Sockets;
using System.Collections.Immutable;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace AstroEngine.Tools.MetadataValidator;

internal static class Program
{
    private const int ProtocolVersion = 1;
    private const long MaxAssemblyBytes = 64L * 1024 * 1024;
    internal const int MaxTypesPerWorker = 64;

    private static int Main(string[] args)
    {
        try
        {
            if (args.Length > 0 && args[0] == "--worker")
                return RunWorker(args);
            if (args.Length > 0 && args[0] == "inspect-many")
                return RunInspectMany(args);
            if (args.Length > 0 && args[0] == "inspect")
                return RunInspect(args);
            if (args.Length > 0 && args[0] == "--probe")
                return RunProbe(args);
            if (args.Length > 0 && args[0] == "--timeout-probe")
                return RunTimeoutProbe(args);
            PrintUsage();
            return 2;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"metadata-validator: {ex.GetType().Name}: {ex.Message}");
            return 1;
        }
    }

    private static int RunInspect(string[] args)
    {
        if (!OperatingSystem.IsWindows())
            return 1;
        if (args.Length != 7 || args[1] != "--dotnet-root" || args[3] != "--assembly" || args[5] != "--type")
        {
            PrintUsage();
            return 2;
        }

        var result = AppContainerWorker.Inspect(args[2], args[4], args[6], TimeSpan.FromSeconds(15));
        Console.WriteLine(JsonSerializer.Serialize(result, JsonOptions));
        return result.Status == "ok" ? 0 : 1;
    }

    private static int RunInspectMany(string[] args)
    {
        if (!OperatingSystem.IsWindows())
            return 1;
        if (args.Length < 7 || args[1] != "--dotnet-root" || args[3] != "--assembly")
        {
            PrintUsage();
            return 2;
        }
        var types = new List<string>();
        var totalTypeNameBytes = 0;
        for (var index = 5; index < args.Length; index += 2)
        {
            if (index + 1 >= args.Length || args[index] != "--type" || string.IsNullOrWhiteSpace(args[index + 1])
                || args[index + 1].Length > 512 || args[index + 1].Any(char.IsControl))
            {
                PrintUsage();
                return 2;
            }
            totalTypeNameBytes += Encoding.UTF8.GetByteCount(args[index + 1]);
            if (types.Count >= MaxTypesPerWorker || totalTypeNameBytes > 8192)
            {
                PrintUsage();
                return 2;
            }
            types.Add(args[index + 1]);
        }
        if (types.Count == 0 || types.Distinct(StringComparer.Ordinal).Count() != types.Count)
        {
            PrintUsage();
            return 2;
        }

        var result = AppContainerWorker.InspectMany(args[2], args[4], types, TimeSpan.FromSeconds(15));
        Console.WriteLine(JsonSerializer.Serialize(result, JsonOptions));
        return result.Status == "ok" ? 0 : 1;
    }

    private static int RunProbe(string[] args)
    {
        if (!OperatingSystem.IsWindows())
            return 1;
        if (args.Length != 7 || args[1] != "--dotnet-root" || args[3] != "--outside-file" || args[5] != "--port"
            || !int.TryParse(args[6], out var port) || port is < 1 or > 65535)
        {
            PrintUsage();
            return 2;
        }

        var result = AppContainerWorker.Probe(args[2], args[4], port, TimeSpan.FromSeconds(15));
        Console.WriteLine(JsonSerializer.Serialize(result, JsonOptions));
        return result.Status == "ok" ? 0 : 1;
    }

    private static int RunTimeoutProbe(string[] args)
    {
        if (!OperatingSystem.IsWindows())
            return 1;
        if (args.Length != 3 || args[1] != "--dotnet-root")
        {
            PrintUsage();
            return 2;
        }
        var result = AppContainerWorker.TimeoutProbe(args[2]);
        Console.WriteLine(JsonSerializer.Serialize(result, JsonOptions));
        return result.Status == "terminated" ? 0 : 1;
    }

    private static int RunWorker(string[] args)
    {
        if (args.Length != 3)
            return 2;
        var inputRoot = Path.GetFullPath(args[1]);
        var outputRoot = Path.GetFullPath(args[2]);
        var requestPath = Path.Combine(inputRoot, "request.json");
        var responsePath = Path.Combine(outputRoot, "response.json");
        try
        {
            var requestBytes = File.ReadAllBytes(requestPath);
            if (requestBytes.Length is 0 or > 16 * 1024)
                throw new InvalidDataException("Request is empty or exceeds 16 KiB.");
            var request = JsonSerializer.Deserialize<WorkerRequest>(requestBytes, JsonOptions)
                ?? throw new InvalidDataException("Request JSON is empty.");
            if (request.SchemaVersion != ProtocolVersion)
                throw new InvalidDataException("Unsupported protocol version.");

            WorkerResponse response;
            switch (request.Operation)
            {
                case "inspect":
                    if (!IsLeafName(request.AssemblyFile) || string.IsNullOrWhiteSpace(request.TypeName)
                        || request.TypeName.Length > 512 || request.TypeName.Any(char.IsControl))
                        throw new InvalidDataException("Assembly filename or managed type name is invalid.");
                    response = Inspect(Path.Combine(inputRoot, request.AssemblyFile), request.TypeName);
                    break;
                case "inspect-many":
                    if (!IsLeafName(request.AssemblyFile) || request.TypeNames is not { Length: > 0 and <= MaxTypesPerWorker }
                        || request.TypeNames.Any(name => string.IsNullOrWhiteSpace(name) || name.Length > 512 || name.Any(char.IsControl))
                        || request.TypeNames.Distinct(StringComparer.Ordinal).Count() != request.TypeNames.Length
                        || request.TypeNames.Sum(name => Encoding.UTF8.GetByteCount(name)) > 8192)
                        throw new InvalidDataException("Assembly filename or managed type list is invalid.");
                    response = InspectMany(Path.Combine(inputRoot, request.AssemblyFile), request.TypeNames);
                    break;
                case "probe":
                    if (string.IsNullOrWhiteSpace(request.ProbePath) || request.ProbePath.Length > 32768
                        || request.Port is < 1 or > 65535)
                        throw new InvalidDataException("Probe parameters are invalid.");
                    response = Probe(request.ProbePath, request.Port);
                    break;
                case "sleep":
                    Thread.Sleep(Math.Clamp(request.DelayMilliseconds, 1, 60_000));
                    response = WorkerResponse.Ok("sleep complete");
                    break;
                default:
                    throw new InvalidDataException("Unsupported operation.");
            }

            var output = JsonSerializer.SerializeToUtf8Bytes(response, JsonOptions);
            if (output.Length > 32 * 1024)
                throw new InvalidDataException("Response exceeds 32 KiB.");
            File.WriteAllBytes(responsePath, output);
            return response.Status == "ok" ? 0 : 1;
        }
        catch (Exception ex)
        {
            try
            {
                var output = JsonSerializer.SerializeToUtf8Bytes(WorkerResponse.Error(ex.GetType().Name + ": " + ex.Message), JsonOptions);
                if (output.Length <= 32 * 1024)
                    File.WriteAllBytes(responsePath, output);
            }
            catch { }
            return 1;
        }
    }

    private static WorkerResponse Inspect(string assemblyPath, string managedTypeName)
    {
        var info = new FileInfo(assemblyPath);
        if (!info.Exists || info.Length is <= 0 or > MaxAssemblyBytes)
            return WorkerResponse.Error("Assembly is missing, empty, or exceeds 64 MiB.");

        using var stream = new FileStream(assemblyPath, FileMode.Open, FileAccess.Read, FileShare.Read);
        using var pe = new PEReader(stream, PEStreamOptions.LeaveOpen);
        if (!pe.HasMetadata)
            return WorkerResponse.Error("Input is not a managed assembly.");

        var reader = pe.GetMetadataReader(MetadataReaderOptions.None);
        if (reader.TypeDefinitions.Count > 250_000)
            return WorkerResponse.Error("Assembly contains too many type definitions.");

        TypeDefinitionHandle matched = default;
        foreach (var handle in reader.TypeDefinitions)
        {
            if (StringComparer.Ordinal.Equals(GetTypeName(reader, handle), managedTypeName))
            {
                if (!matched.IsNil)
                    return WorkerResponse.Error("Managed type name is ambiguous.");
                matched = handle;
            }
        }
        if (matched.IsNil)
            return WorkerResponse.MissingType(managedTypeName);

        var type = reader.GetTypeDefinition(matched);
        var found = new Dictionary<string, int>(StringComparer.Ordinal);
        foreach (var methodHandle in type.GetMethods())
        {
            var method = reader.GetMethodDefinition(methodHandle);
            var name = reader.GetString(method.Name);
            if (!RequiredMethods.Contains(name, StringComparer.Ordinal))
                continue;
            found[name] = found.TryGetValue(name, out var count) ? count + 1 : 1;
        }

        var diagnostics = new List<string>();
        foreach (var name in RequiredMethods)
        {
            if (!found.TryGetValue(name, out var count))
                diagnostics.Add($"Missing method {name}.");
            else if (count != 1)
                diagnostics.Add($"Method {name} is ambiguous.");
            else
            {
                var handle = type.GetMethods().Single(h => StringComparer.Ordinal.Equals(reader.GetString(reader.GetMethodDefinition(h).Name), name));
                var method = reader.GetMethodDefinition(handle);
                var signature = method.DecodeSignature(SignatureTypeProvider.Instance, genericContext: null);
                var attributes = method.Attributes;
                if ((attributes & MethodAttributes.Public) == 0 || (attributes & MethodAttributes.Static) == 0)
                    diagnostics.Add($"Method {name} must be public static.");
                if (signature.Header.IsInstance || signature.GenericParameterCount != 0
                    || signature.ReturnType != "System.Int32"
                    || !signature.ParameterTypes.SequenceEqual(new[] { "System.IntPtr", "System.Int32" }, StringComparer.Ordinal))
                    diagnostics.Add($"Method {name} must have signature static int (IntPtr, int).");
                if (!HasUnmanagedCallersOnlyAttribute(reader, method))
                    diagnostics.Add($"Method {name} must have UnmanagedCallersOnlyAttribute.");
            }
        }

        return diagnostics.Count == 0
            ? WorkerResponse.Valid(managedTypeName, RequiredMethods)
            : WorkerResponse.Invalid(managedTypeName, diagnostics);
    }

    private static WorkerResponse InspectMany(string assemblyPath, IReadOnlyList<string> managedTypeNames)
    {
        var bindings = new List<BindingInspection>(managedTypeNames.Count);
        foreach (var typeName in managedTypeNames)
        {
            var inspection = Inspect(assemblyPath, typeName);
            bindings.Add(new BindingInspection(inspection.ManagedType, inspection.Status,
                inspection.Methods, inspection.Diagnostics));
        }
        return bindings.All(binding => binding.Status == "ok")
            ? WorkerResponse.Batch("ok", bindings)
            : WorkerResponse.Batch("invalid", bindings);
    }

    private static WorkerResponse Probe(string outsidePath, int port)
    {
        var fileDenied = false;
        try { using var _ = File.OpenRead(outsidePath); }
        catch (UnauthorizedAccessException) { fileDenied = true; }
        catch (IOException) { fileDenied = true; }

        var networkDenied = false;
        try
        {
            using var client = new TcpClient();
            var connect = client.ConnectAsync("127.0.0.1", port);
            networkDenied = !connect.Wait(TimeSpan.FromSeconds(2)) || !connect.IsCompletedSuccessfully;
            if (connect.IsCompletedSuccessfully)
                client.Close();
        }
        catch (Exception ex) when (ex is SocketException or UnauthorizedAccessException or IOException or AggregateException)
        {
            networkDenied = true;
        }
        return fileDenied && networkDenied
            ? WorkerResponse.Ok("AppContainer denied outside file and loopback access.") with { OutsideFileDenied = true, NetworkDenied = true }
            : WorkerResponse.Error("AppContainer access probe did not deny all attempted access.") with { OutsideFileDenied = fileDenied, NetworkDenied = networkDenied };
    }

    private static bool IsLeafName(string value) => !string.IsNullOrWhiteSpace(value)
        && value.Length <= 128
        && !value.Contains('/') && !value.Contains('\\')
        && value != "." && value != "..";

    private static string GetTypeName(MetadataReader reader, TypeDefinitionHandle handle)
    {
        var definition = reader.GetTypeDefinition(handle);
        var name = reader.GetString(definition.Name);
        var ns = reader.GetString(definition.Namespace);
        var declaring = definition.GetDeclaringType();
        if (!declaring.IsNil)
            return GetTypeName(reader, declaring) + "+" + name;
        return ns.Length == 0 ? name : ns + "." + name;
    }

    private static bool HasUnmanagedCallersOnlyAttribute(MetadataReader reader, MethodDefinition method)
    {
        foreach (var attributeHandle in method.GetCustomAttributes())
        {
            var attribute = reader.GetCustomAttribute(attributeHandle);
            string typeName;
            if (attribute.Constructor.Kind == HandleKind.MethodDefinition)
            {
                var constructor = reader.GetMethodDefinition((MethodDefinitionHandle)attribute.Constructor);
                typeName = GetTypeName(reader, constructor.GetDeclaringType());
            }
            else if (attribute.Constructor.Kind == HandleKind.MemberReference)
            {
                var constructor = reader.GetMemberReference((MemberReferenceHandle)attribute.Constructor);
                typeName = constructor.Parent.Kind switch
                {
                    HandleKind.TypeReference => GetTypeName(reader, (TypeReferenceHandle)constructor.Parent),
                    HandleKind.TypeDefinition => GetTypeName(reader, (TypeDefinitionHandle)constructor.Parent),
                    _ => string.Empty
                };
            }
            else
            {
                typeName = string.Empty;
            }
            if (StringComparer.Ordinal.Equals(typeName, "System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute"))
                return true;
        }
        return false;
    }

    private static string GetTypeName(MetadataReader reader, TypeReferenceHandle handle)
    {
        var reference = reader.GetTypeReference(handle);
        var name = reader.GetString(reference.Name);
        var ns = reader.GetString(reference.Namespace);
        return ns.Length == 0 ? name : ns + "." + name;
    }

    private static void PrintUsage() => Console.Error.WriteLine(
        "Usage: AstroEngine.CSharpMetadataValidator.exe inspect --dotnet-root <path> --assembly <path> --type <managed-type>\n"
        + "       AstroEngine.CSharpMetadataValidator.exe inspect-many --dotnet-root <path> --assembly <path> --type <managed-type> [--type <managed-type> ...] (maximum 64 types, 8 KiB UTF-8 names)\n"
        + "       AstroEngine.CSharpMetadataValidator.exe --probe --dotnet-root <path> --outside-file <path> --port <port>");

    internal static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = false,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
        MaxDepth = 8,
        WriteIndented = false,
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping
    };

    private static readonly string[] RequiredMethods = ["OnCreate", "FixedUpdate", "OnDestroy"];

    internal sealed record WorkerResponse(int SchemaVersion, string Status, string ManagedType = "",
        IReadOnlyList<string>? Methods = null, IReadOnlyList<string>? Diagnostics = null,
        bool OutsideFileDenied = false, bool NetworkDenied = false, IReadOnlyList<BindingInspection>? Bindings = null)
    {
        internal static WorkerResponse Ok(string message) => new(ProtocolVersion, "ok", Diagnostics: [message]);
        internal static WorkerResponse Error(string message) => new(ProtocolVersion, "error", Diagnostics: [message]);
        internal static WorkerResponse MissingType(string type) => new(ProtocolVersion, "missing-type", type,
            Diagnostics: [$"Managed type '{type}' was not found."]);
        internal static WorkerResponse Valid(string type, IReadOnlyList<string> methods) => new(ProtocolVersion, "ok", type, methods);
        internal static WorkerResponse Invalid(string type, IReadOnlyList<string> diagnostics) => new(ProtocolVersion, "invalid-binding", type, Diagnostics: diagnostics);
        internal static WorkerResponse Batch(string status, IReadOnlyList<BindingInspection> bindings)
            => new(ProtocolVersion, status, Bindings: bindings);
    }
}

internal sealed class SignatureTypeProvider : ISignatureTypeProvider<string, object?>
{
    internal static readonly SignatureTypeProvider Instance = new();
    public string GetArrayType(string elementType, ArrayShape shape) => elementType + "[]";
    public string GetByReferenceType(string elementType) => elementType + "&";
    public string GetFunctionPointerType(MethodSignature<string> signature) => "fnptr";
    public string GetGenericInstantiation(string genericType, ImmutableArray<string> typeArguments) => genericType + "<" + string.Join(',', typeArguments) + ">";
    public string GetGenericMethodParameter(object? genericContext, int index) => "!!" + index;
    public string GetGenericTypeParameter(object? genericContext, int index) => "!" + index;
    public string GetModifiedType(string modifier, string unmodifiedType, bool isRequired) => unmodifiedType;
    public string GetPinnedType(string elementType) => elementType;
    public string GetPointerType(string elementType) => elementType + "*";
    public string GetPrimitiveType(PrimitiveTypeCode typeCode) => typeCode switch
    {
        PrimitiveTypeCode.Int32 => "System.Int32",
        PrimitiveTypeCode.IntPtr => "System.IntPtr",
        PrimitiveTypeCode.Void => "System.Void",
        _ => "System." + typeCode
    };
    public string GetSZArrayType(string elementType) => elementType + "[]";
    public string GetTypeFromDefinition(MetadataReader reader, TypeDefinitionHandle handle, byte rawTypeKind)
        => ProgramTypeName(reader, handle);
    public string GetTypeFromReference(MetadataReader reader, TypeReferenceHandle handle, byte rawTypeKind)
    {
        var reference = reader.GetTypeReference(handle);
        var ns = reader.GetString(reference.Namespace);
        var name = reader.GetString(reference.Name);
        return ns.Length == 0 ? name : ns + "." + name;
    }
    public string GetTypeFromSpecification(MetadataReader reader, object? genericContext, TypeSpecificationHandle handle, byte rawTypeKind)
        => reader.GetTypeSpecification(handle).DecodeSignature(this, genericContext);
    private static string ProgramTypeName(MetadataReader reader, TypeDefinitionHandle handle)
    {
        var definition = reader.GetTypeDefinition(handle);
        var ns = reader.GetString(definition.Namespace);
        var name = reader.GetString(definition.Name);
        return ns.Length == 0 ? name : ns + "." + name;
    }
}

internal static class NativeMethods
{
    internal const int PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES = 0x00020009;
    internal const uint EXTENDED_STARTUPINFO_PRESENT = 0x00080000;
    internal const uint CREATE_SUSPENDED = 0x00000004;
    internal const uint CREATE_NO_WINDOW = 0x08000000;
    internal const uint CREATE_UNICODE_ENVIRONMENT = 0x00000400;
    internal const uint WAIT_OBJECT_0 = 0;
    internal const uint WAIT_TIMEOUT = 258;
    internal const int JobObjectExtendedLimitInformation = 9;
    internal const uint JOB_OBJECT_LIMIT_PROCESS_MEMORY = 0x00000100;
    internal const uint JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000;
    internal const int ERROR_ALREADY_EXISTS = 183;

    [StructLayout(LayoutKind.Sequential)] internal struct SecurityCapabilities
    {
        public IntPtr AppContainerSid; public IntPtr Capabilities; public uint CapabilityCount; public uint Reserved;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] internal struct StartupInfo
    {
        public uint cb; public string? lpReserved; public string? lpDesktop; public string? lpTitle;
        public uint dwX; public uint dwY; public uint dwXSize; public uint dwYSize; public uint dwXCountChars; public uint dwYCountChars;
        public uint dwFillAttribute; public uint dwFlags; public ushort wShowWindow; public ushort cbReserved2;
        public IntPtr lpReserved2; public IntPtr hStdInput; public IntPtr hStdOutput; public IntPtr hStdError;
    }
    [StructLayout(LayoutKind.Sequential)] internal struct StartupInfoEx { public StartupInfo StartupInfo; public IntPtr lpAttributeList; }
    [StructLayout(LayoutKind.Sequential)] internal struct ProcessInformation
    {
        public IntPtr hProcess; public IntPtr hThread; public uint dwProcessId; public uint dwThreadId;
    }
    [StructLayout(LayoutKind.Sequential)] internal struct BasicLimitInformation
    {
        public long PerProcessUserTimeLimit; public long PerJobUserTimeLimit; public uint LimitFlags;
        public UIntPtr MinimumWorkingSetSize; public UIntPtr MaximumWorkingSetSize; public uint ActiveProcessLimit;
        public UIntPtr Affinity; public uint PriorityClass; public uint SchedulingClass;
    }
    [StructLayout(LayoutKind.Sequential)] internal struct IoCounters
    {
        public ulong ReadOperationCount, WriteOperationCount, OtherOperationCount, ReadTransferCount, WriteTransferCount, OtherTransferCount;
    }
    [StructLayout(LayoutKind.Sequential)] internal struct ExtendedLimitInformation
    {
        public BasicLimitInformation BasicLimitInformation; public IoCounters IoInfo;
        public UIntPtr ProcessMemoryLimit, JobMemoryLimit, PeakProcessMemoryUsed, PeakJobMemoryUsed;
    }

    [DllImport("userenv.dll", CharSet = CharSet.Unicode, SetLastError = true)] internal static extern int CreateAppContainerProfile(
        string appContainerName, string displayName, string description, IntPtr capabilities, uint capabilityCount, out IntPtr sid);
    [DllImport("userenv.dll", CharSet = CharSet.Unicode, SetLastError = true)] internal static extern int DeleteAppContainerProfile(string appContainerName);
    [DllImport("advapi32.dll", SetLastError = true)] internal static extern IntPtr FreeSid(IntPtr sid);
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern bool InitializeProcThreadAttributeList(IntPtr list, int count, int flags, ref IntPtr size);
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern bool UpdateProcThreadAttribute(IntPtr list, uint flags, IntPtr attribute, IntPtr value, IntPtr size, IntPtr previous, IntPtr returned);
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern void DeleteProcThreadAttributeList(IntPtr list);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] internal static extern bool CreateProcessW(
        string applicationName, System.Text.StringBuilder commandLine, IntPtr processAttributes, IntPtr threadAttributes,
        bool inheritHandles, uint creationFlags, IntPtr environment, string currentDirectory,
        ref StartupInfoEx startupInfo, out ProcessInformation processInformation);
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern IntPtr CreateJobObjectW(IntPtr attributes, string? name);
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern bool SetInformationJobObject(IntPtr job, int infoClass, ref ExtendedLimitInformation info, uint length);
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern bool TerminateJobObject(IntPtr job, uint exitCode);
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern bool TerminateProcess(IntPtr process, uint exitCode);
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern uint ResumeThread(IntPtr thread);
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern bool GetExitCodeProcess(IntPtr process, out uint exitCode);
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern bool CloseHandle(IntPtr handle);
}

internal sealed record WorkerRequest(int SchemaVersion, string Operation, string AssemblyFile = "", string TypeName = "",
    string ProbePath = "", int Port = 0, int DelayMilliseconds = 0, string[]? TypeNames = null);

internal sealed record BindingInspection(string ManagedType, string Status, IReadOnlyList<string>? Methods,
    IReadOnlyList<string>? Diagnostics);

[SupportedOSPlatform("windows")]
internal static class AppContainerWorker
{
    private const uint ProcessMemoryLimitBytes = 512u * 1024u * 1024u;

    internal static Program.WorkerResponse Inspect(string dotnetRoot, string assemblyPath, string managedTypeName, TimeSpan timeout)
    {
        var info = new FileInfo(assemblyPath);
        if (!info.Exists || info.Length <= 0 || info.Length > 64L * 1024 * 1024)
            return Program.WorkerResponse.Error("Assembly is missing, empty, or exceeds 64 MiB.");
        var request = new WorkerRequest(1, "inspect", "project.dll", managedTypeName);
        return Run(dotnetRoot, request, timeout, inputRoot => File.Copy(info.FullName, Path.Combine(inputRoot, "project.dll")));
    }

    internal static Program.WorkerResponse InspectMany(string dotnetRoot, string assemblyPath, IReadOnlyList<string> managedTypeNames, TimeSpan timeout)
    {
        var info = new FileInfo(assemblyPath);
        if (!info.Exists || info.Length <= 0 || info.Length > 64L * 1024 * 1024)
            return Program.WorkerResponse.Error("Assembly is missing, empty, or exceeds 64 MiB.");
        if (managedTypeNames.Count is 0 or > Program.MaxTypesPerWorker || managedTypeNames.Any(name => string.IsNullOrWhiteSpace(name)
            || name.Length > 512 || name.Any(char.IsControl))
            || managedTypeNames.Sum(name => Encoding.UTF8.GetByteCount(name)) > 8192)
            return Program.WorkerResponse.Error("Managed type list is empty or exceeds the protocol limits.");
        var request = new WorkerRequest(1, "inspect-many", "project.dll", TypeNames: managedTypeNames.ToArray());
        return Run(dotnetRoot, request, timeout, inputRoot => File.Copy(info.FullName, Path.Combine(inputRoot, "project.dll")));
    }

    internal static Program.WorkerResponse Probe(string dotnetRoot, string outsidePath, int port, TimeSpan timeout)
        => Run(dotnetRoot, new WorkerRequest(1, "probe", ProbePath: Path.GetFullPath(outsidePath), Port: port), timeout, null);

    internal static Program.WorkerResponse TimeoutProbe(string dotnetRoot)
    {
        var result = Run(dotnetRoot, new WorkerRequest(1, "sleep", DelayMilliseconds: 10_000), TimeSpan.FromSeconds(5), null);
        return result.Status == "error" && result.Diagnostics?.Any(d => d.Contains("exceeded its time limit", StringComparison.Ordinal)) == true
            ? new Program.WorkerResponse(1, "terminated", Diagnostics: ["Timed worker was terminated by its Job Object."])
            : result;
    }

    private static Program.WorkerResponse Run(string dotnetRoot, WorkerRequest request, TimeSpan timeout, Action<string>? stageInput)
    {
        if (!OperatingSystem.IsWindows() || !Environment.Is64BitProcess)
            return Program.WorkerResponse.Error("The metadata validator requires Windows x64.");
        dotnetRoot = Path.GetFullPath(dotnetRoot);
        var runtimeSource = SelectRuntime(dotnetRoot);
        if (runtimeSource is null)
            return Program.WorkerResponse.Error("A local .NET runtime was not found under the supplied dotnet root.");

        var baseDirectory = AppContext.BaseDirectory;
        var workerExe = Path.Combine(baseDirectory, "AstroEngine.CSharpMetadataValidator.exe");
        if (!File.Exists(workerExe))
            return Program.WorkerResponse.Error("The metadata-validator apphost is missing.");

        var tempRoot = Path.Combine(Path.GetTempPath(), "astro-metadata-validator-" + Guid.NewGuid().ToString("N"));
        var inputRoot = Path.Combine(tempRoot, "input");
        var outputRoot = Path.Combine(tempRoot, "output");
        var stagedWorker = Path.Combine(tempRoot, "worker");
        var stagedDotnetRoot = Path.Combine(tempRoot, "runtime");
        var profileName = "AstroEngine.MetadataValidator." + Guid.NewGuid().ToString("N");
        IntPtr appContainerSid = IntPtr.Zero;
        IntPtr job = IntPtr.Zero;
        NativeMethods.ProcessInformation process = default;
        var profileCreated = false;
        var timedOut = false;
        try
        {
            Directory.CreateDirectory(inputRoot);
            Directory.CreateDirectory(outputRoot);
            Directory.CreateDirectory(stagedWorker);
            Directory.CreateDirectory(stagedDotnetRoot);
            CopyWorkerFiles(baseDirectory, stagedWorker);
            CopyRuntime(runtimeSource.Value, dotnetRoot, stagedDotnetRoot);
            stageInput?.Invoke(inputRoot);
            var requestPath = Path.Combine(inputRoot, "request.json");
            File.WriteAllBytes(requestPath, JsonSerializer.SerializeToUtf8Bytes(request, Program.JsonOptions));

            var hr = NativeMethods.CreateAppContainerProfile(profileName, "AstroEngine metadata validator",
                "Temporary, no-capability profile for metadata-only inspection.", IntPtr.Zero, 0, out appContainerSid);
            if (hr < 0)
                Marshal.ThrowExceptionForHR(hr);
            profileCreated = true;
            var appContainerIdentity = new SecurityIdentifier(appContainerSid);
            GrantAppContainerAccess(tempRoot, inputRoot, outputRoot, appContainerIdentity);

            var stagedDotnetExe = Path.Combine(stagedDotnetRoot, "dotnet.exe");
            var stagedWorkerDll = Path.Combine(stagedWorker, "AstroEngine.CSharpMetadataValidator.dll");
            var stagedRuntimeConfig = Path.Combine(stagedWorker, "AstroEngine.CSharpMetadataValidator.runtimeconfig.json");
            var stagedDeps = Path.Combine(stagedWorker, "AstroEngine.CSharpMetadataValidator.deps.json");
            var commandLine = new System.Text.StringBuilder(Quote(stagedDotnetExe) + " exec --runtimeconfig " + Quote(stagedRuntimeConfig)
                + " --depsfile " + Quote(stagedDeps) + " " + Quote(stagedWorkerDll) + " --worker " + Quote(inputRoot) + " " + Quote(outputRoot));
            var env = CreateEnvironment(stagedDotnetRoot, outputRoot);
            var envChars = env.ToCharArray();
            var envPtr = Marshal.AllocHGlobal(checked(envChars.Length * sizeof(char)));
            Marshal.Copy(envChars, 0, envPtr, envChars.Length);
            try
            {
                LaunchInAppContainer(stagedDotnetExe, commandLine, envPtr, stagedDotnetRoot, appContainerSid, out process, out job);
            }
            finally { Marshal.FreeHGlobal(envPtr); }

            var wait = NativeMethods.WaitForSingleObject(process.hProcess, (uint)Math.Clamp(timeout.TotalMilliseconds, 100, 60_000));
            if (wait == NativeMethods.WAIT_TIMEOUT)
            {
                timedOut = true;
                NativeMethods.TerminateJobObject(job, 124);
                NativeMethods.WaitForSingleObject(process.hProcess, 2000);
            }
            else if (wait != NativeMethods.WAIT_OBJECT_0)
            {
                return Program.WorkerResponse.Error("WaitForSingleObject failed: " + Marshal.GetLastWin32Error());
            }

            if (timedOut)
                return Program.WorkerResponse.Error("Metadata worker exceeded its time limit and was terminated.");
            var responsePath = Path.Combine(outputRoot, "response.json");
            var responseBytes = ReadBoundedResponse(responsePath);
            if (responseBytes is null)
                return Program.WorkerResponse.Error("Metadata worker did not produce a bounded response.");
            var response = JsonSerializer.Deserialize<Program.WorkerResponse>(responseBytes, Program.JsonOptions);
            if (response is null || response.SchemaVersion != 1)
                return Program.WorkerResponse.Error("Metadata worker returned an invalid response schema.");
            NativeMethods.GetExitCodeProcess(process.hProcess, out var exitCode);
            return exitCode == 0 ? response : response.Status == "ok"
                ? Program.WorkerResponse.Error("Metadata worker exited with code " + exitCode + ".")
                : response;
        }
        catch (Exception ex)
        {
            return Program.WorkerResponse.Error(ex.GetType().Name + ": " + ex.Message);
        }
        finally
        {
            if (job != IntPtr.Zero)
            {
                NativeMethods.CloseHandle(job); // KILL_ON_JOB_CLOSE is a final containment guard.
                job = IntPtr.Zero;
            }
            if (process.hThread != IntPtr.Zero) NativeMethods.CloseHandle(process.hThread);
            if (process.hProcess != IntPtr.Zero) NativeMethods.CloseHandle(process.hProcess);
            if (appContainerSid != IntPtr.Zero) NativeMethods.FreeSid(appContainerSid);
            try { if (Directory.Exists(tempRoot)) Directory.Delete(tempRoot, recursive: true); } catch { }
            if (profileCreated)
            {
                var deleteHr = NativeMethods.DeleteAppContainerProfile(profileName);
                if (deleteHr < 0)
                    Console.Error.WriteLine("metadata-validator: temporary AppContainer profile cleanup failed: 0x" + deleteHr.ToString("X8"));
            }
        }
    }

    private static void LaunchInAppContainer(string exe, System.Text.StringBuilder commandLine, IntPtr environment,
        string workingDirectory, IntPtr sid, out NativeMethods.ProcessInformation process, out IntPtr job)
    {
        process = default;
        job = IntPtr.Zero;
        var size = IntPtr.Zero;
        NativeMethods.InitializeProcThreadAttributeList(IntPtr.Zero, 1, 0, ref size);
        if (size == IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "Attribute-list sizing failed.");
        var list = Marshal.AllocHGlobal(size);
        var capsPtr = IntPtr.Zero;
        var attributeListInitialized = false;
        try
        {
            if (!NativeMethods.InitializeProcThreadAttributeList(list, 1, 0, ref size))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "Attribute-list initialization failed.");
            attributeListInitialized = true;
            var caps = new NativeMethods.SecurityCapabilities { AppContainerSid = sid, Capabilities = IntPtr.Zero, CapabilityCount = 0, Reserved = 0 };
            capsPtr = Marshal.AllocHGlobal(Marshal.SizeOf<NativeMethods.SecurityCapabilities>());
            Marshal.StructureToPtr(caps, capsPtr, false);
            if (!NativeMethods.UpdateProcThreadAttribute(list, 0, (IntPtr)NativeMethods.PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                capsPtr, (IntPtr)Marshal.SizeOf<NativeMethods.SecurityCapabilities>(), IntPtr.Zero, IntPtr.Zero))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "Security-capabilities attribute failed.");

            var startup = new NativeMethods.StartupInfoEx
            {
                StartupInfo = new NativeMethods.StartupInfo { cb = (uint)Marshal.SizeOf<NativeMethods.StartupInfoEx>(), dwFlags = 0 },
                lpAttributeList = list
            };
            if (!NativeMethods.CreateProcessW(exe, commandLine, IntPtr.Zero, IntPtr.Zero, false,
                NativeMethods.EXTENDED_STARTUPINFO_PRESENT | NativeMethods.CREATE_SUSPENDED | NativeMethods.CREATE_NO_WINDOW | NativeMethods.CREATE_UNICODE_ENVIRONMENT,
                environment, workingDirectory, ref startup, out process))
            {
                var error = Marshal.GetLastWin32Error();
                throw new System.ComponentModel.Win32Exception(error, "AppContainer worker creation failed (" + error + "): "
                    + new System.ComponentModel.Win32Exception(error).Message);
            }

            job = NativeMethods.CreateJobObjectW(IntPtr.Zero, null);
            if (job == IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "Job Object creation failed.");
            var limits = new NativeMethods.ExtendedLimitInformation
            {
                BasicLimitInformation = new NativeMethods.BasicLimitInformation
                {
                    LimitFlags = NativeMethods.JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | NativeMethods.JOB_OBJECT_LIMIT_PROCESS_MEMORY
                },
                ProcessMemoryLimit = (UIntPtr)ProcessMemoryLimitBytes
            };
            if (!NativeMethods.SetInformationJobObject(job, NativeMethods.JobObjectExtendedLimitInformation, ref limits,
                (uint)Marshal.SizeOf<NativeMethods.ExtendedLimitInformation>()))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "Job Object limits failed.");
            if (!NativeMethods.AssignProcessToJobObject(job, process.hProcess))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "Worker could not be assigned to its Job Object.");
            if (NativeMethods.ResumeThread(process.hThread) == uint.MaxValue)
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "Worker thread could not be resumed.");
        }
        catch
        {
            if (job != IntPtr.Zero)
            {
                NativeMethods.TerminateJobObject(job, 1);
                NativeMethods.CloseHandle(job);
                job = IntPtr.Zero;
            }
            if (process.hProcess != IntPtr.Zero) NativeMethods.TerminateProcess(process.hProcess, 1);
            if (process.hThread != IntPtr.Zero) NativeMethods.CloseHandle(process.hThread);
            if (process.hProcess != IntPtr.Zero) NativeMethods.CloseHandle(process.hProcess);
            process = default;
            throw;
        }
        finally
        {
            if (capsPtr != IntPtr.Zero) Marshal.FreeHGlobal(capsPtr);
            if (attributeListInitialized) NativeMethods.DeleteProcThreadAttributeList(list);
            Marshal.FreeHGlobal(list);
        }
    }

    private static void GrantAppContainerAccess(string root, string input, string output, SecurityIdentifier sid)
    {
        AddRule(new DirectoryInfo(root), sid, FileSystemRights.ReadAndExecute, InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit);
        AddRule(new DirectoryInfo(input), sid, FileSystemRights.ReadAndExecute, InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit);
        AddRule(new DirectoryInfo(output), sid, FileSystemRights.Modify | FileSystemRights.ReadAndExecute, InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit);
        foreach (var directory in Directory.EnumerateDirectories(root, "*", SearchOption.AllDirectories))
            AddRule(new DirectoryInfo(directory), sid, FileSystemRights.ReadAndExecute, InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit);
        foreach (var file in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories))
        {
            var rights = file.StartsWith(output + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)
                ? FileSystemRights.Modify | FileSystemRights.ReadAndExecute : FileSystemRights.ReadAndExecute;
            AddRule(new FileInfo(file), sid, rights, InheritanceFlags.None);
        }
    }

    private static void AddRule(DirectoryInfo directory, SecurityIdentifier sid, FileSystemRights rights, InheritanceFlags inheritance)
    {
        var acl = directory.GetAccessControl(AccessControlSections.Access);
        acl.AddAccessRule(new FileSystemAccessRule(sid, rights, inheritance, PropagationFlags.None, AccessControlType.Allow));
        directory.SetAccessControl(acl);
    }

    private static void AddRule(FileInfo file, SecurityIdentifier sid, FileSystemRights rights, InheritanceFlags inheritance)
    {
        var acl = file.GetAccessControl(AccessControlSections.Access);
        acl.AddAccessRule(new FileSystemAccessRule(sid, rights, inheritance, PropagationFlags.None, AccessControlType.Allow));
        file.SetAccessControl(acl);
    }

    private static (string Fx, string Shared)? SelectRuntime(string root)
    {
        var fxRoot = Path.Combine(root, "host", "fxr");
        var sharedRoot = Path.Combine(root, "shared", "Microsoft.NETCore.App");
        var fx = Directory.Exists(fxRoot) ? Directory.GetDirectories(fxRoot).OrderByDescending(Path.GetFileName, VersionComparer.Instance).FirstOrDefault() : null;
        var shared = Directory.Exists(sharedRoot) ? Directory.GetDirectories(sharedRoot).OrderByDescending(Path.GetFileName, VersionComparer.Instance).FirstOrDefault() : null;
        return fx is null || shared is null ? null : (fx, shared);
    }

    private static void CopyWorkerFiles(string source, string destination)
    {
        foreach (var name in new[] { "AstroEngine.CSharpMetadataValidator.exe", "AstroEngine.CSharpMetadataValidator.dll",
                     "AstroEngine.CSharpMetadataValidator.deps.json", "AstroEngine.CSharpMetadataValidator.runtimeconfig.json" })
        {
            var path = Path.Combine(source, name);
            if (File.Exists(path)) File.Copy(path, Path.Combine(destination, name));
        }
    }

    private static void CopyRuntime((string Fx, string Shared) source, string dotnetRoot, string destination)
    {
        var dotnet = Path.Combine(dotnetRoot, "dotnet.exe");
        if (!File.Exists(dotnet)) throw new FileNotFoundException("Local dotnet.exe is missing.", dotnet);
        File.Copy(dotnet, Path.Combine(destination, "dotnet.exe"));
        CopyDirectory(source.Fx, Path.Combine(destination, "host", "fxr", Path.GetFileName(source.Fx)));
        CopyDirectory(source.Shared, Path.Combine(destination, "shared", "Microsoft.NETCore.App", Path.GetFileName(source.Shared)));
    }

    private static void CopyDirectory(string source, string destination)
    {
        Directory.CreateDirectory(destination);
        foreach (var file in Directory.EnumerateFiles(source))
            File.Copy(file, Path.Combine(destination, Path.GetFileName(file)));
        foreach (var directory in Directory.EnumerateDirectories(source))
            CopyDirectory(directory, Path.Combine(destination, Path.GetFileName(directory)));
    }

    private static byte[]? ReadBoundedResponse(string path)
    {
        try
        {
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.None, 4096, FileOptions.SequentialScan);
            if (stream.Length is <= 0 or > 32 * 1024)
                return null;
            var bytes = new byte[(int)stream.Length];
            var offset = 0;
            while (offset < bytes.Length)
            {
                var read = stream.Read(bytes, offset, bytes.Length - offset);
                if (read == 0)
                    return null;
                offset += read;
            }
            return stream.ReadByte() == -1 ? bytes : null;
        }
        catch (IOException) { return null; }
        catch (UnauthorizedAccessException) { return null; }
    }

    private static string CreateEnvironment(string dotnetRoot, string tempDirectory)
    {
        var windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            ["=" + Path.GetPathRoot(Environment.CurrentDirectory)!.TrimEnd(Path.DirectorySeparatorChar)] = Environment.CurrentDirectory,
            ["SystemRoot"] = windows,
            ["SystemDrive"] = Path.GetPathRoot(windows)!.TrimEnd(Path.DirectorySeparatorChar),
            ["WINDIR"] = windows,
            ["USERPROFILE"] = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),
            ["APPDATA"] = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            ["LOCALAPPDATA"] = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            ["ProgramData"] = Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
            ["USERNAME"] = Environment.UserName,
            ["HOMEDRIVE"] = Environment.GetEnvironmentVariable("HOMEDRIVE") ?? Path.GetPathRoot(Environment.CurrentDirectory)!,
            ["HOMEPATH"] = Environment.GetEnvironmentVariable("HOMEPATH") ?? Environment.GetFolderPath(Environment.SpecialFolder.UserProfile).Substring(Path.GetPathRoot(Environment.CurrentDirectory)!.Length - 1),
            ["PATH"] = dotnetRoot + Path.PathSeparator + Path.Combine(windows, "System32"),
            ["TEMP"] = tempDirectory,
            ["TMP"] = tempDirectory,
            ["DOTNET_ROOT"] = dotnetRoot,
            ["DOTNET_ROOT_X64"] = dotnetRoot,
            ["DOTNET_MULTILEVEL_LOOKUP"] = "0",
            ["DOTNET_ROLL_FORWARD"] = "LatestPatch",
            ["DOTNET_NOLOGO"] = "1",
            ["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1"
        };
        return string.Join('\0', values.OrderBy(item => item.Key, StringComparer.OrdinalIgnoreCase).Select(item => item.Key + "=" + item.Value)) + "\0\0";
    }

    private static string Quote(string value)
    {
        var result = new System.Text.StringBuilder(value.Length + 2).Append('"');
        var slashes = 0;
        foreach (var character in value)
        {
            if (character == '\\') { slashes++; continue; }
            if (character == '"')
            {
                result.Append('\\', slashes * 2 + 1).Append('"');
                slashes = 0;
                continue;
            }
            result.Append('\\', slashes).Append(character);
            slashes = 0;
        }
        result.Append('\\', slashes * 2).Append('"');
        return result.ToString();
    }

    private sealed class VersionComparer : IComparer<string?>
    {
        internal static readonly VersionComparer Instance = new();
        public int Compare(string? x, string? y)
        {
            var hasX = Version.TryParse(x, out var vx);
            var hasY = Version.TryParse(y, out var vy);
            if (hasX && hasY) return vx!.CompareTo(vy);
            return StringComparer.Ordinal.Compare(x, y);
        }
    }
}
