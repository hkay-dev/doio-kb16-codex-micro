using System.Buffers.Binary;
using System.IO;
using HidSharp;

namespace Doio.Kb16.Mapper;

public sealed class ConfigProtocolClient : IAsyncDisposable
{
    private const int VendorId = 0x303A;
    private const int ProductId = 0x8360;
    private const int ReportSize = 64;
    private const byte ReportId = 6;
    private const byte Channel = 3;
    private const byte Version = 1;
    private const int HeaderSize = 9;
    private const int FragmentSize = ReportSize - HeaderSize;
    private const byte SolidColorMode = 1;
    private readonly HidDevice _device;
    private readonly HidStream _stream;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private ushort _requestId;

    private ConfigProtocolClient(HidDevice device, HidStream stream)
    {
        _device = device;
        _stream = stream;
        _stream.ReadTimeout = 1000;
        _stream.WriteTimeout = 1000;
    }

    public string DeviceName => $"{_device.GetManufacturer() ?? "Work Louder"} {_device.GetProductName() ?? "Codex Micro"}";

    public static ConfigProtocolClient Connect()
    {
        var foundCompatibleDevice = false;
        foreach (var device in DeviceList.Local.GetHidDevices(VendorId, ProductId))
        {
            if (device.GetMaxInputReportLength() < ReportSize || device.GetMaxOutputReportLength() < ReportSize) continue;
            foundCompatibleDevice = true;
            if (device.TryOpen(out var stream)) return new ConfigProtocolClient(device, stream);
        }
        if (foundCompatibleDevice)
            throw new DeviceConnectionException("The Codex Micro configuration interface was found but couldn't be opened. Close any other configurator that may have claimed the HID interface, then try again.", true);
        throw new DeviceConnectionException("No configurable 303A:8360 Codex Micro HID interface was found. Check that the device is connected and running the configurable v1.3-or-newer firmware.");
    }

    public async Task<(uint Generation, uint Crc, ushort ConfigLength)> HelloAsync(CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            var id = NextRequestId();
            await WritePacketAsync(1, id, 0, 1, ReadOnlyMemory<byte>.Empty, cancellationToken);
            var packet = await ReadPacketAsync(1, id, cancellationToken);
            return ParseHelloPayload(packet.Payload.Span);
        }
        finally { _gate.Release(); }
    }

    public static (uint Generation, uint Crc, ushort ConfigLength) ParseHelloPayload(ReadOnlySpan<byte> payload)
    {
        if (payload.Length == 1) EnsureStatus(payload, 1);
        if (payload.Length != 16 || payload[0] != 0 || payload[1] != 1 || payload[2] != 4 || payload[3] != 16 || payload[14] != 3 || payload[15] != 3)
            throw new InvalidDataException("The device reported incompatible configuration capabilities.");
        return (BinaryPrimitives.ReadUInt32LittleEndian(payload[6..]), BinaryPrimitives.ReadUInt32LittleEndian(payload[10..]), BinaryPrimitives.ReadUInt16LittleEndian(payload[4..]));
    }

    public async Task<DeviceSnapshot> ReadConfigurationAsync(CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            var id = NextRequestId();
            await WritePacketAsync(2, id, 0, 1, ReadOnlyMemory<byte>.Empty, cancellationToken);
            var chunks = new SortedDictionary<byte, byte[]>();
            byte expected = 0;
            do
            {
                var packet = await ReadPacketAsync(2, id, cancellationToken);
                expected = packet.ChunkCount;
                if (packet.ChunkIndex >= expected || chunks.ContainsKey(packet.ChunkIndex)) throw new InvalidDataException("The device returned a duplicate or out-of-range chunk.");
                chunks.Add(packet.ChunkIndex, packet.Payload.ToArray());
            } while (chunks.Count < expected);
            var bytes = chunks.OrderBy(item => item.Key).SelectMany(item => item.Value).ToArray();
            if (bytes.Length != 8 + DeviceConfiguration.PayloadSize) throw new InvalidDataException("The device returned a configuration response with an invalid length.");
            var generation = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(0, 4));
            var crc = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(4, 4));
            var configBytes = bytes.AsSpan(8).ToArray();
            if (Crc32.Compute(configBytes) != crc) throw new ConfigurationVerificationException("The device configuration failed its CRC check.");
            return new DeviceSnapshot(generation, crc, DeviceConfiguration.FromBinary(configBytes));
        }
        finally { _gate.Release(); }
    }

    public async Task<DeviceSnapshot> WriteConfigurationAsync(DeviceConfiguration configuration, CancellationToken cancellationToken = default)
    {
        var bytes = configuration.ToBinary();
        var crc = Crc32.Compute(bytes);
        await _gate.WaitAsync(cancellationToken);
        try
        {
            var id = NextRequestId();
            var chunkCount = (byte)((bytes.Length + FragmentSize - 1) / FragmentSize);
            var begin = new byte[6];
            BinaryPrimitives.WriteUInt16LittleEndian(begin, (ushort)bytes.Length);
            BinaryPrimitives.WriteUInt32LittleEndian(begin.AsSpan(2), crc);
            await WritePacketAsync(3, id, 0, chunkCount, begin, cancellationToken);
            EnsureStatus((await ReadPacketAsync(3, id, cancellationToken)).Payload.Span, 3);
            for (byte chunk = 0; chunk < chunkCount; chunk++)
            {
                var offset = chunk * FragmentSize;
                var length = Math.Min(FragmentSize, bytes.Length - offset);
                await WritePacketAsync(4, id, chunk, chunkCount, bytes.AsMemory(offset, length), cancellationToken);
                EnsureStatus((await ReadPacketAsync(4, id, cancellationToken)).Payload.Span, 4);
            }
            await WritePacketAsync(5, id, 0, 1, ReadOnlyMemory<byte>.Empty, cancellationToken);
            EnsureStatus((await ReadPacketAsync(5, id, cancellationToken)).Payload.Span, 5);
        }
        finally { _gate.Release(); }
        var snapshot = await ReadConfigurationAsync(cancellationToken);
        if (snapshot.Crc32 != crc || !snapshot.Configuration.ToBinary().SequenceEqual(bytes)) throw new ConfigurationVerificationException("The configuration read back after the write doesn't match.");
        return snapshot;
    }

    public async Task<DeviceSnapshot> ResetDefaultsAsync(CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            var id = NextRequestId();
            await WritePacketAsync(6, id, 0, 1, ReadOnlyMemory<byte>.Empty, cancellationToken);
            EnsureStatus((await ReadPacketAsync(6, id, cancellationToken)).Payload.Span, 6);
        }
        finally { _gate.Release(); }
        return await ReadConfigurationAsync(cancellationToken);
    }

    public async Task<LightingState> ReadLightingAsync(CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            var id = NextRequestId();
            await WritePacketAsync(7, id, 0, 1, ReadOnlyMemory<byte>.Empty, cancellationToken);
            return ParseLightingPayload((await ReadPacketAsync(7, id, cancellationToken)).Payload.Span);
        }
        finally { _gate.Release(); }
    }

    internal static LightingState ParseLightingPayload(ReadOnlySpan<byte> payload)
    {
        if (payload.Length == 1) EnsureStatus(payload, 7);
        if (payload.Length != 5 || payload[0] > 1 || payload[1] == 0)
            throw new InvalidDataException("The device returned an invalid lighting state.");
        return new LightingState(payload[0] != 0, payload[1], payload[2], payload[3], payload[4]);
    }

    public Task PreviewLightingAsync(byte hue, byte saturation, byte value, CancellationToken cancellationToken = default) =>
        SendLightingCommandAsync(8, [hue, saturation, value], cancellationToken);

    public async Task<LightingState> CommitLightingAsync(byte hue, byte saturation, byte value, CancellationToken cancellationToken = default)
    {
        await SendLightingCommandAsync(9, [hue, saturation, value], cancellationToken);
        var current = await ReadLightingAsync(cancellationToken);
        if (!current.Enabled || current.Mode != SolidColorMode || current.Hue != hue || current.Saturation != saturation || current.Value != value)
            throw new ConfigurationVerificationException(
                $"The lighting values read back after saving don't match. Expected H/S/V {hue}/{saturation}/{value}, but the device returned {current.Hue}/{current.Saturation}/{current.Value}.");
        return current;
    }

    public Task RestoreLightingAsync(LightingState state, CancellationToken cancellationToken = default) =>
        SendLightingCommandAsync(10, [state.Enabled ? (byte)1 : (byte)0, state.Mode, state.Hue, state.Saturation, state.Value], cancellationToken);

    private async Task SendLightingCommandAsync(byte opcode, byte[] payload, CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            var id = NextRequestId();
            await WritePacketAsync(opcode, id, 0, 1, payload, cancellationToken);
            EnsureStatus((await ReadPacketAsync(opcode, id, cancellationToken)).Payload.Span, opcode);
        }
        finally { _gate.Release(); }
    }

    private async Task WritePacketAsync(byte opcode, ushort id, byte chunkIndex, byte chunkCount, ReadOnlyMemory<byte> payload, CancellationToken cancellationToken)
    {
        if (payload.Length > FragmentSize) throw new ArgumentOutOfRangeException(nameof(payload));
        var report = new byte[ReportSize];
        report[0] = ReportId; report[1] = Channel; report[2] = Version; report[3] = opcode;
        BinaryPrimitives.WriteUInt16LittleEndian(report.AsSpan(4), id);
        report[6] = chunkIndex; report[7] = chunkCount; report[8] = (byte)payload.Length;
        payload.CopyTo(report.AsMemory(HeaderSize));
        var output = new byte[_device.GetMaxOutputReportLength()];
        report.CopyTo(output, 0);
        await _stream.WriteAsync(output, 0, output.Length, cancellationToken);
    }

    private async Task<Packet> ReadPacketAsync(byte opcode, ushort id, CancellationToken cancellationToken)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(3));
        while (true)
        {
            var buffer = new byte[_device.GetMaxInputReportLength()];
            int count;
            try { count = await _stream.ReadAsync(buffer, 0, buffer.Length, timeout.Token); }
            catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested) { throw new TimeoutException("Timed out waiting for a response on device Channel 3."); }
            var offset = count > ReportSize && buffer[0] == 0 && buffer[1] == ReportId ? 1 : 0;
            if (count - offset < ReportSize) continue;
            var report = buffer.AsSpan(offset, ReportSize);
            if (report[0] != ReportId || report[1] != Channel || report[2] != Version || report[3] != (byte)(opcode | 0x80)) continue;
            if (BinaryPrimitives.ReadUInt16LittleEndian(report[4..]) != id) continue;
            var length = report[8];
            if (length > FragmentSize) throw new InvalidDataException("The device returned a chunk that's too long.");
            return new Packet(report[6], report[7], report.Slice(HeaderSize, length).ToArray());
        }
    }

    private static void EnsureStatus(ReadOnlySpan<byte> payload, byte opcode)
    {
        if (payload.Length != 1) throw new InvalidDataException("The device returned an invalid acknowledgement packet.");
        if (payload[0] == 0) return;
        var message = payload[0] switch
        {
            1 => "Protocol version mismatch", 2 => "Unknown operation", 3 => "Invalid length", 4 => "Invalid chunk order", 5 => "CRC error",
            6 => "Invalid configuration", 7 => "A control is still held", 8 => "Device storage write failed", 9 => "No write session exists", _ => $"Status {payload[0]}",
        };
        throw new DeviceProtocolException(opcode, payload[0], message);
    }

    private ushort NextRequestId() => ++_requestId == 0 ? ++_requestId : _requestId;

    public ValueTask DisposeAsync()
    {
        _gate.Dispose();
        _stream.Dispose();
        return ValueTask.CompletedTask;
    }

    private sealed record Packet(byte ChunkIndex, byte ChunkCount, ReadOnlyMemory<byte> Payload);
}

public sealed record LightingState(bool Enabled, byte Mode, byte Hue, byte Saturation, byte Value);
