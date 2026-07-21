using System.IO;

namespace Doio.Kb16.Mapper;

public enum DeviceOperation { Read, Write, Reset }

public sealed class DeviceConnectionException(string message, bool mayBeInUse = false) : IOException(message)
{
    public bool MayBeInUse { get; } = mayBeInUse;
}

public sealed class DeviceProtocolException(byte opcode, byte statusCode, string message) : IOException($"The device rejected operation {opcode}: {message}.")
{
    public byte Opcode { get; } = opcode;
    public byte StatusCode { get; } = statusCode;
}

public sealed class ConfigurationVerificationException(string message) : IOException(message);

public sealed class DeviceStateChangedException() : InvalidOperationException("The device configuration changed after this draft was loaded. Your draft is still here. Export it, or read the device again before editing.");

public sealed record DeviceErrorPresentation(string Title, string Message, bool Disconnect);

public static class DeviceErrorPresenter
{
    public static DeviceErrorPresentation Create(Exception exception, DeviceOperation operation) => exception switch
    {
        DeviceStateChangedException => new("Device Configuration Changed", exception.Message, false),
        ConfigurationVerificationException => new("Verification Failed", exception.Message, false),
        DeviceProtocolException { StatusCode: 7 } => new("Device Busy", "The device found a held key or encoder. Release every control and try again. The draft wasn't written.", false),
        DeviceProtocolException { StatusCode: 8 } => new("Write Failed", exception.Message, false),
        DeviceProtocolException => new(operation == DeviceOperation.Write ? "Write Failed" : "Device Rejected the Operation", exception.Message, false),
        DeviceConnectionException { MayBeInUse: true } => new("Device Connection Failed", exception.Message, true),
        DeviceConnectionException => new("Device Not Found", exception.Message, true),
        TimeoutException => new("Device Timed Out", exception.Message, true),
        _ when operation == DeviceOperation.Write => new("Write Failed", exception.Message, true),
        _ when operation == DeviceOperation.Reset => new("Restore Failed", exception.Message, true),
        _ => new("Device Communication Failed", exception.Message, true),
    };
}
