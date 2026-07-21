namespace Doio.Kb16.Mapper;

public sealed record ConfigurationChange(string Location, string OldValue, string NewValue);

public sealed record ConfigurationDiffResult(IReadOnlyList<ConfigurationChange> Changes, int ByteDifference)
{
    public int ChangedControlCount => Changes.Count;
    public bool HasChanges => Changes.Count > 0;
}

public static class ConfigurationDiff
{
    private static readonly string[] LayerNames = ["数字层", "导航层", "系统层"];
    private static readonly string[] EncoderNames = ["左旋钮", "右旋钮", "中旋钮"];
    private static readonly string[] EncoderActionNames = ["逆时针", "顺时针", "按压"];

    public static ConfigurationDiffResult Compare(DeviceConfiguration oldConfiguration, DeviceConfiguration newConfiguration)
    {
        oldConfiguration.Validate();
        newConfiguration.Validate();
        var changes = new List<ConfigurationChange>();

        for (var index = 0; index < 16; index++)
        {
            if (oldConfiguration.NativeKeys[index] == newConfiguration.NativeKeys[index]) continue;
            changes.Add(new ConfigurationChange(
                $"Codex 层 · {KeyPosition(index)}",
                NativeName(oldConfiguration.NativeKeys[index]),
                NativeName(newConfiguration.NativeKeys[index])));
        }

        for (var index = 0; index < oldConfiguration.CodexEncoders.Length; index++)
        {
            AddActionChange(
                changes,
                $"Codex 层 · {EncoderNames[index / 3 + 1]} · {EncoderActionNames[index % 3]}",
                oldConfiguration.CodexEncoders[index],
                newConfiguration.CodexEncoders[index]);
        }

        for (var layerIndex = 0; layerIndex < oldConfiguration.Layers.Length; layerIndex++)
        {
            var oldLayer = oldConfiguration.Layers[layerIndex];
            var newLayer = newConfiguration.Layers[layerIndex];
            for (var keyIndex = 0; keyIndex < oldLayer.Keys.Length; keyIndex++)
                AddActionChange(changes, $"{LayerNames[layerIndex]} · {KeyPosition(keyIndex)}", oldLayer.Keys[keyIndex], newLayer.Keys[keyIndex]);
            for (var actionIndex = 0; actionIndex < oldLayer.Encoders.Length; actionIndex++)
                AddActionChange(
                    changes,
                    $"{LayerNames[layerIndex]} · {EncoderNames[actionIndex / 3]} · {EncoderActionNames[actionIndex % 3]}",
                    oldLayer.Encoders[actionIndex],
                    newLayer.Encoders[actionIndex]);
        }

        var byteDifference = oldConfiguration.ToBinary().Zip(newConfiguration.ToBinary()).Count(pair => pair.First != pair.Second);
        return new ConfigurationDiffResult(changes, byteDifference);
    }

    private static void AddActionChange(List<ConfigurationChange> changes, string location, ActionDescriptor oldAction, ActionDescriptor newAction)
    {
        if (oldAction.Kind == newAction.Kind && oldAction.Modifiers == newAction.Modifiers && oldAction.Code == newAction.Code) return;
        changes.Add(new ConfigurationChange(location, ActionCatalog.Describe(oldAction), ActionCatalog.Describe(newAction)));
    }

    private static string KeyPosition(int index) => $"第 {index / 4 + 1} 排第 {index % 4 + 1} 列";

    private static string NativeName(NativeControl value) => value switch
    {
        NativeControl.JoystickUp => "摇杆上",
        NativeControl.JoystickRight => "摇杆右",
        NativeControl.JoystickDown => "摇杆下",
        NativeControl.JoystickLeft => "摇杆左",
        NativeControl.MicACT10 => "Mic ACT10",
        _ => value.ToString(),
    };
}

public static class ConfigurationApplyGuard
{
    public static bool Matches(uint expectedGeneration, uint expectedCrc, DeviceConfiguration expectedConfiguration, DeviceSnapshot current) =>
        current.Generation == expectedGeneration &&
        current.Crc32 == expectedCrc &&
        current.Configuration.ToBinary().SequenceEqual(expectedConfiguration.ToBinary());
}
