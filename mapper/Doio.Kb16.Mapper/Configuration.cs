using System.Buffers.Binary;
using System.IO;
using System.Text.Json.Serialization;

namespace Doio.Kb16.Mapper;

public enum NativeControl : byte
{
    AG00, AG01, JoystickUp, JoystickRight,
    AG02, AG03, JoystickLeft, JoystickDown,
    AG04, AG05, ACT06, ACT07,
    ACT08, ACT09, ACT12, MicACT10,
}

public enum ActionKind : byte
{
    None, Keyboard, Consumer, Mouse, Firmware,
}

public sealed class ActionDescriptor
{
    [JsonConverter(typeof(JsonStringEnumConverter<ActionKind>))]
    public ActionKind Kind { get; set; }
    public byte Modifiers { get; set; }
    public ushort Code { get; set; }

    public ActionDescriptor Clone() => new() { Kind = Kind, Modifiers = Modifiers, Code = Code };
}

public sealed class LayerConfiguration
{
    public ActionDescriptor[] Keys { get; set; } = CreateActions(16);
    public ActionDescriptor[] Encoders { get; set; } = CreateActions(9);

    internal static ActionDescriptor[] CreateActions(int count) => Enumerable.Range(0, count).Select(_ => new ActionDescriptor()).ToArray();
}

public sealed class DeviceConfiguration
{
    public const int PayloadSize = 340;
    public int SchemaVersion { get; set; } = 1;
    public NativeControl[] NativeKeys { get; set; } = Enum.GetValues<NativeControl>();
    public ActionDescriptor[] CodexEncoders { get; set; } = LayerConfiguration.CreateActions(6);
    public LayerConfiguration[] Layers { get; set; } = Enumerable.Range(0, 3).Select(_ => new LayerConfiguration()).ToArray();

    public byte[] ToBinary()
    {
        Validate();
        var data = new byte[PayloadSize];
        var offset = 0;
        foreach (var native in NativeKeys) data[offset++] = (byte)native;
        foreach (var action in CodexEncoders) WriteAction(data, ref offset, action);
        foreach (var layer in Layers)
            foreach (var action in layer.Keys)
                WriteAction(data, ref offset, action);
        foreach (var layer in Layers)
            foreach (var action in layer.Encoders)
                WriteAction(data, ref offset, action);
        if (offset != PayloadSize) throw new InvalidOperationException($"Invalid configuration length: {offset}");
        return data;
    }

    public static DeviceConfiguration FromBinary(ReadOnlySpan<byte> data)
    {
        if (data.Length != PayloadSize) throw new InvalidDataException($"The device configuration must be {PayloadSize} bytes long.");
        var result = new DeviceConfiguration();
        var offset = 0;
        result.NativeKeys = new NativeControl[16];
        for (var index = 0; index < result.NativeKeys.Length; index++) result.NativeKeys[index] = (NativeControl)data[offset++];
        result.CodexEncoders = new ActionDescriptor[6];
        for (var index = 0; index < result.CodexEncoders.Length; index++) result.CodexEncoders[index] = ReadAction(data, ref offset);
        result.Layers = Enumerable.Range(0, 3).Select(_ => new LayerConfiguration()).ToArray();
        foreach (var layer in result.Layers)
        {
            layer.Keys = new ActionDescriptor[16];
            for (var index = 0; index < layer.Keys.Length; index++) layer.Keys[index] = ReadAction(data, ref offset);
        }
        foreach (var layer in result.Layers)
        {
            layer.Encoders = new ActionDescriptor[9];
            for (var index = 0; index < layer.Encoders.Length; index++) layer.Encoders[index] = ReadAction(data, ref offset);
        }
        result.Validate();
        return result;
    }

    public DeviceConfiguration Clone() => FromBinary(ToBinary());

    public void Validate()
    {
        if (SchemaVersion != 1) throw new InvalidDataException("Only configuration schema 1 is supported.");
        if (NativeKeys is null || CodexEncoders is null || Layers is null)
            throw new InvalidDataException("The configuration is missing its native keys, Codex encoders, or layers.");
        if (NativeKeys.Length != 16 || NativeKeys.Distinct().Count() != 16 || NativeKeys.Any(value => (byte)value >= 16))
            throw new InvalidDataException("The Codex layer must have exactly 16 distinct native controls.");
        if (CodexEncoders.Length != 6 || Layers.Length != 3) throw new InvalidDataException("The encoder or layer count is invalid.");
        ValidateActions(CodexEncoders);
        foreach (var layer in Layers)
        {
            if (layer is null || layer.Keys is null || layer.Encoders is null)
                throw new InvalidDataException("An ordinary layer is missing its keys or encoder actions.");
            if (layer.Keys.Length != 16 || layer.Encoders.Length != 9) throw new InvalidDataException("Each ordinary layer must have 16 keys and 9 encoder actions.");
            ValidateActions(layer.Keys);
            ValidateActions(layer.Encoders);
        }
    }

    private static void ValidateActions(IEnumerable<ActionDescriptor> actions)
    {
        foreach (var action in actions)
        {
            if (action is null) throw new InvalidDataException("The configuration has a missing action.");
            var valid = action.Kind switch
            {
                ActionKind.None => action.Modifiers == 0 && action.Code == 0,
                ActionKind.Keyboard => action.Code is >= 0x04 and <= 0xE7,
                ActionKind.Consumer => action.Modifiers == 0 && action.Code is >= 1 and <= 6,
                ActionKind.Mouse => action.Modifiers == 0 && action.Code is >= 1 and <= 9,
                ActionKind.Firmware => action.Modifiers == 0 && action.Code is >= 1 and <= 19,
                _ => false,
            };
            if (!valid) throw new InvalidDataException($"Invalid action: {action.Kind}/{action.Modifiers}/{action.Code}");
        }
    }

    private static void WriteAction(Span<byte> data, ref int offset, ActionDescriptor action)
    {
        data[offset++] = (byte)action.Kind;
        data[offset++] = action.Modifiers;
        BinaryPrimitives.WriteUInt16LittleEndian(data[offset..], action.Code);
        offset += 2;
    }

    private static ActionDescriptor ReadAction(ReadOnlySpan<byte> data, ref int offset)
    {
        var result = new ActionDescriptor
        {
            Kind = (ActionKind)data[offset++],
            Modifiers = data[offset++],
            Code = BinaryPrimitives.ReadUInt16LittleEndian(data[offset..]),
        };
        offset += 2;
        return result;
    }
}

public sealed record DeviceSnapshot(uint Generation, uint Crc32, DeviceConfiguration Configuration);

public static class Crc32
{
    public static uint Compute(ReadOnlySpan<byte> data)
    {
        uint crc = 0xFFFFFFFF;
        foreach (var value in data)
        {
            crc ^= value;
            for (var bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320u & (uint)-(int)(crc & 1));
        }
        return ~crc;
    }
}
