namespace SoloRig.Ui;

public sealed record PedalControlDescriptor(
    string Type,
    string DisplayName,
    string Category,
    double DefaultWidth,
    double DefaultHeight,
    int DefaultDetents,
    string VectorGeometry);

public static class PedalControlCatalog
{
    public static IReadOnlyList<PedalControlDescriptor> Controls { get; } =
    [
        Control("knob.continuous", "无级旋钮", "控制器", 46, 46, 0, "knob"),
        Control("knob.detented", "档位旋钮", "控制器", 46, 46, 12, "knob-detented"),
        Control("fader.continuous", "无级推子", "控制器", 28, 94, 0, "fader"),
        Control("fader.detented", "档位推子", "控制器", 28, 94, 8, "fader-detented"),
        Control("switch.toggle", "拨档开关", "控制器", 38, 52, 3, "toggle"),
        Control("button.momentary", "瞬时按钮", "控制器", 38, 38, 0, "button"),
        Control("button.latching", "自锁按钮", "控制器", 38, 38, 2, "button-latching"),
        Control("footswitch", "踩钉", "控制器", 42, 42, 2, "footswitch"),
        Control("indicator.led", "LED 指示灯", "指示", 16, 16, 0, "led"),
        Control("connector.ts", "TS 输入/输出", "接口", 36, 36, 0, "jack-ts"),
        Control("connector.trs", "TRS 输入/输出", "接口", 36, 36, 0, "jack-trs"),
        Control("connector.xlr", "XLR 接口", "接口", 44, 44, 0, "xlr"),
        Control("connector.send", "Send / Return", "接口", 42, 42, 0, "send-return"),
        Control("connector.dc", "DC 供电", "接口", 34, 34, 0, "dc"),
        Control("connector.iec", "IEC 电源", "接口", 46, 46, 0, "iec"),
        Control("connector.midi-din", "MIDI DIN", "接口", 44, 44, 0, "midi-din"),
        Control("connector.usb", "USB 接口", "接口", 42, 42, 0, "usb"),
        Control("connector.speaker", "扬声器输出", "接口", 42, 42, 0, "speaker-out"),
        Control("selector.impedance", "阻抗选择器", "接口", 48, 48, 4, "selector"),
        Control("label", "文字标签", "标注", 96, 24, 0, "label"),
    ];

    private static PedalControlDescriptor Control(
        string type, string displayName, string category, double width, double height, int detents, string geometry) =>
        new(type, displayName, category, width, height, detents, geometry);

    public static PedalControlDescriptor Get(string type) =>
        Controls.FirstOrDefault(control => string.Equals(control.Type, type, StringComparison.Ordinal)) ?? Controls[0];

    public static bool Contains(string type) =>
        Controls.Any(control => string.Equals(control.Type, type, StringComparison.Ordinal));
}

public sealed class PedalSkinElementDraft : ModuleAuthoringDraft
{
    private string id;
    private string type;
    private string binding;
    private double x;
    private double y;
    private double width;
    private double height;
    private double rotation;
    private int detents;
    private int zIndex;
    private string label;
    private string style;

    public PedalSkinElementDraft(string id, string type, string binding, double x, double y)
    {
        this.id = id;
        this.type = type;
        this.binding = binding;
        this.x = x;
        this.y = y;
        var descriptor = PedalControlCatalog.Get(type);
        width = descriptor.DefaultWidth;
        height = descriptor.DefaultHeight;
        detents = descriptor.DefaultDetents;
        label = string.Empty;
        style = "{}";
    }

    public string Id { get => id; set => Set(ref id, value); }
    public string Type
    {
        get => type;
        set
        {
            if (Set(ref type, value) && PedalControlCatalog.Contains(value))
            {
                var descriptor = PedalControlCatalog.Get(value);
                Width = descriptor.DefaultWidth;
                Height = descriptor.DefaultHeight;
                if (Detents == 0) Detents = descriptor.DefaultDetents;
                NotifyPropertyChanged(nameof(DisplayName));
                NotifyPropertyChanged(nameof(IsKnob));
                NotifyPropertyChanged(nameof(IsFader));
                NotifyPropertyChanged(nameof(IsSwitch));
                NotifyPropertyChanged(nameof(IsButton));
                NotifyPropertyChanged(nameof(IsFootswitch));
                NotifyPropertyChanged(nameof(IsLed));
                NotifyPropertyChanged(nameof(IsConnector));
                NotifyPropertyChanged(nameof(IsLabel));
            }
        }
    }
    public string Binding { get => binding; set => Set(ref binding, value); }
    public double X { get => x; set => Set(ref x, value); }
    public double Y { get => y; set => Set(ref y, value); }
    public double Width { get => width; set => Set(ref width, value); }
    public double Height { get => height; set => Set(ref height, value); }
    public double Rotation { get => rotation; set => Set(ref rotation, value); }
    public int Detents { get => detents; set => Set(ref detents, value); }
    public int ZIndex { get => zIndex; set => Set(ref zIndex, value); }
    public string Label { get => label; set => Set(ref label, value); }
    public string Style { get => style; set => Set(ref style, value); }
    public string DisplayName => PedalControlCatalog.Get(Type).DisplayName;
    public string GeometryKind => PedalControlCatalog.Get(Type).VectorGeometry;
    public bool IsKnob => Type.StartsWith("knob.", StringComparison.Ordinal);
    public bool IsFader => Type.StartsWith("fader.", StringComparison.Ordinal);
    public bool IsSwitch => Type == "switch.toggle";
    public bool IsButton => Type.StartsWith("button.", StringComparison.Ordinal);
    public bool IsFootswitch => Type == "footswitch";
    public bool IsLed => Type == "indicator.led";
    public bool IsConnector => Type.StartsWith("connector.", StringComparison.Ordinal) || Type == "selector.impedance";
    public bool IsLabel => Type == "label";
}
