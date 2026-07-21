using System.IO;

namespace Doio.Kb16.Mapper;

public static class DraftEditing
{
    public static void SwapNative(DeviceConfiguration draft, int index, NativeControl desired)
    {
        ArgumentNullException.ThrowIfNull(draft);
        if (index is < 0 or >= 16) throw new ArgumentOutOfRangeException(nameof(index));
        var currentIndex = Array.IndexOf(draft.NativeKeys, desired);
        if (currentIndex < 0) throw new InvalidDataException("The draft is missing the selected native Codex control.");
        (draft.NativeKeys[index], draft.NativeKeys[currentIndex]) = (draft.NativeKeys[currentIndex], draft.NativeKeys[index]);
    }

    public static ActionDescriptor WithModifiers(ActionDescriptor action, bool ctrl, bool shift, bool alt, bool win)
    {
        ArgumentNullException.ThrowIfNull(action);
        return WithModifiers(action, action.Modifiers, ctrl, shift, alt, win);
    }

    public static ActionDescriptor WithModifiers(ActionDescriptor action, byte existingModifiers, bool ctrl, bool shift, bool alt, bool win)
    {
        ArgumentNullException.ThrowIfNull(action);
        var result = action.Clone();
        result.Modifiers = result.Kind == ActionKind.Keyboard
            ? (byte)(KeepSide(existingModifiers, 0x11, 0x01, ctrl)
                | KeepSide(existingModifiers, 0x22, 0x02, shift)
                | KeepSide(existingModifiers, 0x44, 0x04, alt)
                | KeepSide(existingModifiers, 0x88, 0x08, win))
            : (byte)0;
        return result;
    }

    private static byte KeepSide(byte existing, byte mask, byte left, bool enabled)
    {
        if (!enabled) return 0;
        var side = (byte)(existing & mask);
        return side != 0 ? side : left;
    }

    public static bool IsSame(ActionDescriptor left, ActionDescriptor right) =>
        left.Kind == right.Kind && left.Modifiers == right.Modifiers && left.Code == right.Code;
}
