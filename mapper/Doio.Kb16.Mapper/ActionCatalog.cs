namespace Doio.Kb16.Mapper;

public sealed record ActionOption(string Category, string Name, ActionDescriptor Action)
{
    public string CodeText => $"{(byte)Action.Kind:X2} {Action.Modifiers:X2} {Action.Code:X4}";
    public override string ToString() => Name;
}

public static class ActionCatalog
{
    public static IReadOnlyList<ActionOption> All { get; } = Build();

    public static IReadOnlyList<ActionOption> Filter(string? category, string? search)
    {
        category = string.IsNullOrWhiteSpace(category) ? ActionEditorState.AllCategory : category;
        search = search?.Trim() ?? string.Empty;
        return All.Where(item =>
            (category == ActionEditorState.AllCategory ||
             category == "键盘" && item.Action.Kind is ActionKind.Keyboard or ActionKind.None ||
             item.Category == category) &&
            (search.Length == 0 || item.Name.Contains(search, StringComparison.CurrentCultureIgnoreCase))).ToArray();
    }

    public static ActionOption? Find(ActionDescriptor action) =>
        All.FirstOrDefault(item => item.Action.Kind == action.Kind && item.Action.Code == action.Code);

    public static string Describe(ActionDescriptor action)
    {
        var exact = All.FirstOrDefault(item => item.Action.Kind == action.Kind && item.Action.Code == action.Code);
        var name = exact?.Name ?? $"{action.Kind} {action.Code}";
        if (action.Kind != ActionKind.Keyboard || action.Modifiers == 0) return name;
        var parts = new List<string>();
        if ((action.Modifiers & 0x11) != 0) parts.Add("Ctrl");
        if ((action.Modifiers & 0x22) != 0) parts.Add("Shift");
        if ((action.Modifiers & 0x44) != 0) parts.Add("Alt");
        if ((action.Modifiers & 0x88) != 0) parts.Add("Win");
        parts.Add(name);
        return string.Join("+", parts);
    }

    private static IReadOnlyList<ActionOption> Build()
    {
        var result = new List<ActionOption> { new("常用", "无动作", new() { Kind = ActionKind.None }) };
        for (var index = 0; index < 26; index++) AddKey(result, "字母", ((char)('A' + index)).ToString(), (ushort)(0x04 + index));
        for (var digit = 1; digit <= 9; digit++) AddKey(result, "数字", digit.ToString(), (ushort)(0x1D + digit));
        AddKey(result, "数字", "0", 0x27);
        (string Name, ushort Code)[] common =
        [
            ("Enter",0x28),("Escape",0x29),("Backspace",0x2A),("Tab",0x2B),("Space",0x2C),("-",0x2D),("=",0x2E),("[",0x2F),("]",0x30),("\\",0x31),(";",0x33),("'",0x34),("`",0x35),(",",0x36),(".",0x37),("/",0x38),
            ("Home",0x4A),("Page Up",0x4B),("Delete",0x4C),("End",0x4D),("Page Down",0x4E),("Right",0x4F),("Left",0x50),("Down",0x51),("Up",0x52),("Num Lock",0x53),
            ("KP /",0x54),("KP *",0x55),("KP -",0x56),("KP +",0x57),("KP Enter",0x58),("KP 1",0x59),("KP 2",0x5A),("KP 3",0x5B),("KP 4",0x5C),("KP 5",0x5D),("KP 6",0x5E),("KP 7",0x5F),("KP 8",0x60),("KP 9",0x61),("KP 0",0x62),("KP .",0x63),
        ];
        foreach (var item in common) AddKey(result, item.Code >= 0x54 ? "小键盘" : "常用", item.Name, item.Code);
        for (var key = 1; key <= 12; key++) AddKey(result, "功能键", $"F{key}", (ushort)(0x39 + key));
        for (var key = 13; key <= 24; key++) AddKey(result, "功能键", $"F{key}", (ushort)(0x68 + key - 13));

        Add(result, "媒体", ActionKind.Consumer, 1, "音量增加"); Add(result, "媒体", ActionKind.Consumer, 2, "音量降低");
        Add(result, "媒体", ActionKind.Consumer, 3, "静音"); Add(result, "媒体", ActionKind.Consumer, 4, "播放/暂停");
        Add(result, "媒体", ActionKind.Consumer, 5, "下一首"); Add(result, "媒体", ActionKind.Consumer, 6, "上一首");
        string[] mouse = ["鼠标左键", "鼠标右键", "鼠标中键", "鼠标键4", "鼠标键5", "滚轮上", "滚轮下", "滚轮左", "滚轮右"];
        for (ushort code = 1; code <= mouse.Length; code++) Add(result, "鼠标", ActionKind.Mouse, code, mouse[code - 1]);
        string[] firmware = ["循环切层", "前往 Codex 层", "前往数字层", "前往导航层", "前往系统层", "RGB 开关", "上一灯效", "下一灯效", "RGB 亮度降低", "RGB 亮度增加", "OLED 亮度降低", "OLED 亮度增加", "RGB 冷白纯色", "按住进入 Bootloader", "NO LINK 提示", "按住归档（保留行为）", "原生旋钮逆时针", "原生旋钮顺时针", "原生旋钮按压"];
        for (ushort code = 1; code <= firmware.Length; code++) Add(result, "设备", ActionKind.Firmware, code, firmware[code - 1]);
        return result;
    }

    private static void AddKey(List<ActionOption> items, string category, string name, ushort code) => Add(items, category, ActionKind.Keyboard, code, name);
    private static void Add(List<ActionOption> items, string category, ActionKind kind, ushort code, string name) => items.Add(new(category, name, new() { Kind = kind, Code = code }));
}

public sealed record ActionEditorState(
    string Category,
    string Search,
    IReadOnlyList<ActionOption> Options,
    ActionOption? SelectedOption)
{
    public const string AllCategory = "全部";

    public static ActionEditorState ForControl(ActionDescriptor action) =>
        new(AllCategory, string.Empty, ActionCatalog.All, ActionCatalog.Find(action));
}
