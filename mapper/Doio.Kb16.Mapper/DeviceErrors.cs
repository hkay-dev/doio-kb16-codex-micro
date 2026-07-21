using System.IO;

namespace Doio.Kb16.Mapper;

public enum DeviceOperation { Read, Write, Reset }

public sealed class DeviceConnectionException(string message, bool mayBeInUse = false) : IOException(message)
{
    public bool MayBeInUse { get; } = mayBeInUse;
}

public sealed class DeviceProtocolException(byte opcode, byte statusCode, string message) : IOException($"设备拒绝操作 {opcode}：{message}。")
{
    public byte Opcode { get; } = opcode;
    public byte StatusCode { get; } = statusCode;
}

public sealed class ConfigurationVerificationException(string message) : IOException(message);

public sealed class DeviceStateChangedException() : InvalidOperationException("设备配置已在当前草稿载入后发生变化。当前草稿已保留；请先导出草稿，或主动重新读取设备后再编辑。");

public sealed record DeviceErrorPresentation(string Title, string Message, bool Disconnect);

public static class DeviceErrorPresenter
{
    public static DeviceErrorPresentation Create(Exception exception, DeviceOperation operation) => exception switch
    {
        DeviceStateChangedException => new("设备配置已变化", exception.Message, false),
        ConfigurationVerificationException => new("校验失败", exception.Message, false),
        DeviceProtocolException { StatusCode: 7 } => new("设备正忙", "设备检测到仍有按键或旋钮按住。请全部松开后重试；当前草稿未写入。", false),
        DeviceProtocolException { StatusCode: 8 } => new("写入失败", exception.Message, false),
        DeviceProtocolException => new(operation == DeviceOperation.Write ? "写入失败" : "设备拒绝操作", exception.Message, false),
        DeviceConnectionException { MayBeInUse: true } => new("设备连接失败", exception.Message, true),
        DeviceConnectionException => new("未找到设备", exception.Message, true),
        TimeoutException => new("设备响应超时", exception.Message, true),
        _ when operation == DeviceOperation.Write => new("写入失败", exception.Message, true),
        _ when operation == DeviceOperation.Reset => new("恢复失败", exception.Message, true),
        _ => new("设备通信失败", exception.Message, true),
    };
}
