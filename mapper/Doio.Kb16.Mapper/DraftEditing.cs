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
        var result = action.Clone();
        result.Modifiers = result.Kind == ActionKind.Keyboard
            ? (byte)((ctrl ? 1 : 0) | (shift ? 2 : 0) | (alt ? 4 : 0) | (win ? 8 : 0))
            : (byte)0;
        return result;
    }

    public static bool IsSame(ActionDescriptor left, ActionDescriptor right) =>
        left.Kind == right.Kind && left.Modifiers == right.Modifiers && left.Code == right.Code;
}
