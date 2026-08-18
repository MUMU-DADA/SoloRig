using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;

namespace SoloRig.Ui;

public enum ModuleAuthoringIssueTarget
{
    Project,
    ModulePort,
    Parameter,
    Logic,
    AnalogCircuit,
    AnalogPort,
    AnalogSupply,
    AnalogBinding,
    AnalogComponent,
    AnalogPin,
    AnalogProperty,
    Skin,
    NativeRuntime,
}

public sealed record ModuleAuthoringIssue(
    string Message,
    ModuleAuthoringIssueTarget Target,
    string TargetId = "",
    string FieldId = "",
    bool IsWarning = false,
    object? TargetObject = null)
{
    public string SeverityText => IsWarning ? "警告" : "错误";
    public bool IsError => !IsWarning;
    public string LocationText => string.IsNullOrWhiteSpace(TargetId) ? TargetDisplayName(Target) :
        $"{TargetDisplayName(Target)} · {TargetId}";

    private static string TargetDisplayName(ModuleAuthoringIssueTarget target) => target switch
    {
        ModuleAuthoringIssueTarget.Project => "项目信息",
        ModuleAuthoringIssueTarget.ModulePort => "模块端口",
        ModuleAuthoringIssueTarget.Parameter => "公开参数",
        ModuleAuthoringIssueTarget.Logic => "功能逻辑",
        ModuleAuthoringIssueTarget.AnalogCircuit => "模拟电路",
        ModuleAuthoringIssueTarget.AnalogPort => "电路端口",
        ModuleAuthoringIssueTarget.AnalogSupply => "电源",
        ModuleAuthoringIssueTarget.AnalogBinding => "参数绑定",
        ModuleAuthoringIssueTarget.AnalogComponent => "元件",
        ModuleAuthoringIssueTarget.AnalogPin => "引脚",
        ModuleAuthoringIssueTarget.AnalogProperty => "元件属性",
        ModuleAuthoringIssueTarget.Skin => "外观",
        ModuleAuthoringIssueTarget.NativeRuntime => "原生运行时",
        _ => "项目",
    };
}

public enum ModuleAuthoringKind
{
    DigitalAlgorithm,
    AnalogCircuit,
    NeuralAmpModel,
    ImpulseResponse,
}

public sealed record ModuleAuthoringKindOption(ModuleAuthoringKind Kind, string DisplayName, string Description);
public sealed record ModuleAuthoringTemplateOption(
    string Id, ModuleAuthoringKind Kind, string DisplayName, string Description);

public abstract class ModuleAuthoringDraft : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;
    internal event Action? Changed;

    protected bool Set<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }
        field = value;
        NotifyPropertyChanged(propertyName);
        NotifyChanged();
        return true;
    }

    protected void NotifyPropertyChanged(string? propertyName) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));

    protected void NotifyChanged() => Changed?.Invoke();
}

public sealed class ModulePortDraft : ModuleAuthoringDraft
{
    private string id;
    private string direction;
    private int maxChannels;

    public ModulePortDraft(string id, string direction, int maxChannels)
    {
        this.id = id;
        this.direction = direction;
        this.maxChannels = maxChannels;
    }

    public string Id { get => id; set => Set(ref id, value); }
    public string Direction { get => direction; set => Set(ref direction, value); }
    public int MaxChannels { get => maxChannels; set => Set(ref maxChannels, value); }
    public bool IsInput => string.Equals(Direction, "input", StringComparison.Ordinal);
}

public sealed class ModuleParameterDraft : ModuleAuthoringDraft
{
    private string id;
    private string displayName;
    private string unit;
    private double minimum;
    private double maximum;
    private double defaultValue;
    private string controlKind;

    public ModuleParameterDraft(string id, string displayName, string unit, double minimum, double maximum,
        double defaultValue, string controlKind = "knob")
    {
        this.id = id;
        this.displayName = displayName;
        this.unit = unit;
        this.minimum = minimum;
        this.maximum = maximum;
        this.defaultValue = defaultValue;
        this.controlKind = controlKind;
    }

    public string Id { get => id; set => Set(ref id, value); }
    public string DisplayName { get => displayName; set => Set(ref displayName, value); }
    public string Unit { get => unit; set => Set(ref unit, value); }
    public double Minimum { get => minimum; set => Set(ref minimum, value); }
    public double Maximum { get => maximum; set => Set(ref maximum, value); }
    public double DefaultValue { get => defaultValue; set => Set(ref defaultValue, value); }
    public string ControlKind { get => controlKind; set => Set(ref controlKind, value); }
}

public sealed class DigitalOperationDraft : ModuleAuthoringDraft
{
    private string type;
    private string parameterId;

    public DigitalOperationDraft(string type, string parameterId = "")
    {
        this.type = type;
        this.parameterId = parameterId;
    }

    public string Type { get => type; set => Set(ref type, value); }
    public string ParameterId { get => parameterId; set => Set(ref parameterId, value); }
}

public sealed class PedalSkinDraft : ModuleAuthoringDraft
{
    private string id = "solorig.custom-pedal";
    private string knobId = "standard-knob";
    private string toggleId = "standard-toggle";
    private string buttonId = "standard-button";
    private string footswitchId = "standard-footswitch";
    private string knobImage = string.Empty;
    private string toggleImage = string.Empty;
    private string buttonImage = string.Empty;
    private string footswitchImage = string.Empty;
    private double width = 196.0;
    private double height = 154.0;
    private string background = "#26313C";
    private string labelColor = "#EEF3F8";
    private bool useCustomFootswitchPosition;
    private double footswitchX = 98.0;
    private double footswitchY = 126.0;

    public PedalSkinDraft()
    {
        ParameterPlacements.CollectionChanged += ParameterPlacementsChanged;
        Elements.CollectionChanged += ElementsChanged;
        Elements.Add(new PedalSkinElementDraft("default-input", "connector.ts", "input", 24.0, 78.0));
        Elements.Add(new PedalSkinElementDraft("default-output", "connector.ts", "output", 172.0, 78.0));
        Elements.Add(new PedalSkinElementDraft("default-dc", "connector.dc", "", 24.0, 28.0));
        Elements.Add(new PedalSkinElementDraft("default-led", "indicator.led", "", 172.0, 28.0));
        Elements.Add(new PedalSkinElementDraft("default-footswitch", "footswitch", "", 98.0, 126.0));
    }

    public string Id { get => id; set => Set(ref id, value); }
    public string KnobId { get => knobId; set => Set(ref knobId, value); }
    public string ToggleId { get => toggleId; set => Set(ref toggleId, value); }
    public string ButtonId { get => buttonId; set => Set(ref buttonId, value); }
    public string FootswitchId { get => footswitchId; set => Set(ref footswitchId, value); }
    public string KnobImage { get => knobImage; set => Set(ref knobImage, value); }
    public string ToggleImage { get => toggleImage; set => Set(ref toggleImage, value); }
    public string ButtonImage { get => buttonImage; set => Set(ref buttonImage, value); }
    public string FootswitchImage { get => footswitchImage; set => Set(ref footswitchImage, value); }
    public double Width
    {
        get => width;
        set
        {
            if (Set(ref width, value))
            {
                NotifyPropertyChanged(nameof(PreviewFootswitchLeft));
            }
        }
    }
    public double Height
    {
        get => height;
        set
        {
            if (Set(ref height, value))
            {
                NotifyPropertyChanged(nameof(PreviewFootswitchTop));
            }
        }
    }
    public string Background
    {
        get => background;
        set
        {
            if (Set(ref background, value))
            {
                NotifyPropertyChanged(nameof(PreviewBackground));
            }
        }
    }
    public string LabelColor
    {
        get => labelColor;
        set
        {
            if (Set(ref labelColor, value))
            {
                NotifyPropertyChanged(nameof(PreviewLabelColor));
            }
        }
    }
    public bool UseCustomFootswitchPosition
    {
        get => useCustomFootswitchPosition;
        set
        {
            if (Set(ref useCustomFootswitchPosition, value))
            {
                NotifyPropertyChanged(nameof(PreviewFootswitchLeft));
                NotifyPropertyChanged(nameof(PreviewFootswitchTop));
            }
        }
    }
    public double FootswitchX
    {
        get => footswitchX;
        set
        {
            if (Set(ref footswitchX, value))
            {
                NotifyPropertyChanged(nameof(PreviewFootswitchLeft));
            }
        }
    }
    public double FootswitchY
    {
        get => footswitchY;
        set
        {
            if (Set(ref footswitchY, value))
            {
                NotifyPropertyChanged(nameof(PreviewFootswitchTop));
            }
        }
    }
    public ObservableCollection<PedalSkinParameterPlacementDraft> ParameterPlacements { get; } = [];
    public ObservableCollection<PedalSkinElementDraft> Elements { get; } = [];
    public string PreviewBackground => IsPreviewColor(Background) ? Background : "#26313C";
    public string PreviewLabelColor => IsPreviewColor(LabelColor) ? LabelColor : "#EEF3F8";
    public double PreviewFootswitchLeft =>
        (UseCustomFootswitchPosition ? FootswitchX : Width * 0.5) - 17.0;
    public double PreviewFootswitchTop =>
        (UseCustomFootswitchPosition ? FootswitchY : Height - 28.0) - 17.0;

    private void ParameterPlacementsChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        if (args.OldItems is not null)
        {
            foreach (PedalSkinParameterPlacementDraft placement in args.OldItems)
            {
                placement.Changed -= ParameterPlacementChanged;
            }
        }
        if (args.NewItems is not null)
        {
            foreach (PedalSkinParameterPlacementDraft placement in args.NewItems)
            {
                placement.Changed += ParameterPlacementChanged;
            }
        }
        NotifyChanged();
    }

    private void ParameterPlacementChanged() => NotifyChanged();

    private void ElementsChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        if (args.OldItems is not null)
        {
            foreach (PedalSkinElementDraft element in args.OldItems)
            {
                element.Changed -= ElementChanged;
            }
        }
        if (args.NewItems is not null)
        {
            foreach (PedalSkinElementDraft element in args.NewItems)
            {
                element.Changed += ElementChanged;
            }
        }
        NotifyChanged();
    }

    private void ElementChanged() => NotifyChanged();

    private static bool IsPreviewColor(string value) =>
        !string.IsNullOrWhiteSpace(value) && value.Length == 7 && value[0] == '#' &&
        value.Skip(1).All(Uri.IsHexDigit);
}

public sealed class PedalSkinParameterPlacementDraft : ModuleAuthoringDraft
{
    private string parameterId;
    private double x;
    private double y;
    private string controlKind = "knob";
    private bool isSelected;

    public PedalSkinParameterPlacementDraft(string parameterId, double x, double y)
    {
        this.parameterId = parameterId;
        this.x = x;
        this.y = y;
    }

    public string ParameterId { get => parameterId; set => Set(ref parameterId, value); }
    public string ControlKind => controlKind;
    public string ControlKindDisplay => controlKind switch
    {
        "toggle" => "拨档",
        "button" => "按钮",
        "footswitch" => "踩钉",
        _ => "旋钮",
    };
    public bool IsKnob => controlKind == "knob";
    public bool IsToggle => controlKind == "toggle";
    public bool IsButton => controlKind == "button";
    public bool IsFootswitch => controlKind == "footswitch";
    public bool IsSelected
    {
        get => isSelected;
        set
        {
            if (isSelected == value) return;
            isSelected = value;
            NotifyPropertyChanged(nameof(IsSelected));
        }
    }
    public double X
    {
        get => x;
        set
        {
            if (Set(ref x, value))
            {
                NotifyPropertyChanged(nameof(PreviewLeft));
            }
        }
    }
    public double Y
    {
        get => y;
        set
        {
            if (Set(ref y, value))
            {
                NotifyPropertyChanged(nameof(PreviewTop));
            }
        }
    }
    public double PreviewLeft => X - 26.0;
    public double PreviewTop => Y - 26.0;

    internal void SetControlKind(string value)
    {
        value = value is "toggle" or "button" or "footswitch" ? value : "knob";
        if (controlKind == value) return;
        controlKind = value;
        NotifyPropertyChanged(nameof(ControlKind));
        NotifyPropertyChanged(nameof(ControlKindDisplay));
        NotifyPropertyChanged(nameof(IsKnob));
        NotifyPropertyChanged(nameof(IsToggle));
        NotifyPropertyChanged(nameof(IsButton));
        NotifyPropertyChanged(nameof(IsFootswitch));
    }
}

public sealed class AnalogPinDraft : ModuleAuthoringDraft
{
    private string id;
    private string displayName;
    private string node;
    private double anchorX;
    private double anchorY;
    private string side;
    private string electricalRole;

    public AnalogPinDraft(
        string id,
        string displayName,
        string node,
        double anchorX = 0.0,
        double anchorY = 0.0,
        string side = "left",
        string electricalRole = "passive")
    {
        this.id = id;
        this.displayName = displayName;
        this.node = node;
        this.anchorX = anchorX;
        this.anchorY = anchorY;
        this.side = side;
        this.electricalRole = electricalRole;
    }

    public string Id { get => id; set => Set(ref id, value); }
    public string DisplayName { get => displayName; set => Set(ref displayName, value); }
    public string Node { get => node; set => Set(ref node, value); }
    public double AnchorX => anchorX;
    public double AnchorY => anchorY;
    public double VisualLeft => anchorX - 6.0;
    public double VisualTop => anchorY - 6.0;
    public string Side => side;
    public string ElectricalRole => electricalRole;

    internal void ApplyDescriptor(AnalogPinDescriptor descriptor)
    {
        id = descriptor.Id;
        displayName = descriptor.DisplayName;
        anchorX = descriptor.AnchorX;
        anchorY = descriptor.AnchorY;
        side = descriptor.Side;
        electricalRole = descriptor.ElectricalRole;
        NotifyPropertyChanged(nameof(Id));
        NotifyPropertyChanged(nameof(DisplayName));
        NotifyPropertyChanged(nameof(AnchorX));
        NotifyPropertyChanged(nameof(AnchorY));
        NotifyPropertyChanged(nameof(VisualLeft));
        NotifyPropertyChanged(nameof(VisualTop));
        NotifyPropertyChanged(nameof(Side));
        NotifyPropertyChanged(nameof(ElectricalRole));
    }
}

public sealed class AnalogJsonFieldDraft : ModuleAuthoringDraft
{
    private string name;
    private string jsonValue;

    public AnalogJsonFieldDraft(string name, string jsonValue)
    {
        this.name = name;
        this.jsonValue = jsonValue;
    }

    public string Name { get => name; set => Set(ref name, value); }
    public string JsonValue
    {
        get => jsonValue;
        set
        {
            if (Set(ref jsonValue, value))
            {
                NotifyTypedValues();
            }
        }
    }
    public string Kind => InferKind(JsonValue);
    public bool IsNumber => Kind == "number";
    public bool IsBoolean => Kind == "boolean";
    public bool IsText => Kind == "text";
    public bool IsJson => Kind == "json";
    public double NumberValue
    {
        get => double.TryParse(JsonValue, System.Globalization.NumberStyles.Float,
            System.Globalization.CultureInfo.InvariantCulture, out var value) ? value : 0.0;
        set => JsonValue = value.ToString("R", System.Globalization.CultureInfo.InvariantCulture);
    }
    public bool BooleanValue
    {
        get => string.Equals(JsonValue.Trim(), "true", StringComparison.OrdinalIgnoreCase);
        set => JsonValue = value ? "true" : "false";
    }
    public string TextValue
    {
        get
        {
            try
            {
                return JsonSerializer.Deserialize<string>(JsonValue) ?? string.Empty;
            }
            catch (JsonException)
            {
                return JsonValue;
            }
        }
        set => JsonValue = JsonSerializer.Serialize(value ?? string.Empty);
    }

    private static string InferKind(string source)
    {
        try
        {
            using var document = JsonDocument.Parse(source);
            return document.RootElement.ValueKind switch
            {
                JsonValueKind.Number => "number",
                JsonValueKind.True or JsonValueKind.False => "boolean",
                JsonValueKind.String => "text",
                _ => "json",
            };
        }
        catch (JsonException)
        {
            return "json";
        }
    }

    private void NotifyTypedValues()
    {
        NotifyPropertyChanged(nameof(Kind));
        NotifyPropertyChanged(nameof(IsNumber));
        NotifyPropertyChanged(nameof(IsBoolean));
        NotifyPropertyChanged(nameof(IsText));
        NotifyPropertyChanged(nameof(IsJson));
        NotifyPropertyChanged(nameof(NumberValue));
        NotifyPropertyChanged(nameof(BooleanValue));
        NotifyPropertyChanged(nameof(TextValue));
    }
}

public sealed class AnalogComponentPropertyDraft : ModuleAuthoringDraft
{
    private string id;
    private string jsonValue;
    private string displayName;
    private string kind;
    private string unit;
    private double? minimum;
    private double? maximum;
    private double? increment;
    private IReadOnlyList<string> options = [];
    private bool synchronizingJsonFields;

    public AnalogComponentPropertyDraft(string id, string jsonValue)
    {
        this.id = id;
        this.jsonValue = jsonValue;
        displayName = id;
        kind = "json";
        unit = string.Empty;
        JsonFields.CollectionChanged += JsonFieldsChanged;
        RefreshJsonFields();
    }

    public string Id { get => id; set => Set(ref id, value); }
    public string JsonValue
    {
        get => jsonValue;
        set
        {
            if (Set(ref jsonValue, value))
            {
                if (!synchronizingJsonFields)
                {
                    RefreshJsonFields();
                }
                NotifyTypedValues();
            }
        }
    }
    public string DisplayName => displayName;
    public string Kind => kind;
    public string Unit => unit;
    public double NumberMinimum => minimum ?? -1.0e12;
    public double NumberMaximum => maximum ?? 1.0e12;
    public double NumberIncrement => increment ?? 0.01;
    public IReadOnlyList<string> Options => options;
    public ObservableCollection<AnalogJsonFieldDraft> JsonFields { get; } = [];
    public bool HasJsonFields => IsJson && JsonFields.Count > 0;
    public bool IsNumber => Kind == "number";
    public bool IsBoolean => Kind == "boolean";
    public bool IsModel => Kind == "model";
    public bool IsText => Kind is "text" or "choice";
    public bool IsJson => !IsNumber && !IsBoolean && !IsText && !IsModel;
    public double NumberValue
    {
        get => double.TryParse(JsonValue, System.Globalization.NumberStyles.Float,
            System.Globalization.CultureInfo.InvariantCulture, out var value) ? value : 0.0;
        set => JsonValue = value.ToString("R", System.Globalization.CultureInfo.InvariantCulture);
    }
    public bool BooleanValue
    {
        get => string.Equals(JsonValue.Trim(), "true", StringComparison.OrdinalIgnoreCase);
        set => JsonValue = value ? "true" : "false";
    }
    public string TextValue
    {
        get
        {
            try
            {
                return JsonSerializer.Deserialize<string>(JsonValue) ?? string.Empty;
            }
            catch (JsonException)
            {
                return JsonValue;
            }
        }
        set => JsonValue = JsonSerializer.Serialize(value ?? string.Empty);
    }

    internal void ApplyDescriptor(AnalogPropertyDescriptor? descriptor)
    {
        displayName = descriptor?.DisplayName ?? Id;
        kind = descriptor?.Kind ?? "json";
        unit = descriptor?.Unit ?? string.Empty;
        minimum = descriptor?.Minimum;
        maximum = descriptor?.Maximum;
        increment = descriptor?.Increment;
        options = descriptor?.Options ?? [];
        NotifyPropertyChanged(nameof(DisplayName));
        NotifyPropertyChanged(nameof(Kind));
        NotifyPropertyChanged(nameof(Unit));
        NotifyPropertyChanged(nameof(NumberMinimum));
        NotifyPropertyChanged(nameof(NumberMaximum));
        NotifyPropertyChanged(nameof(NumberIncrement));
        NotifyPropertyChanged(nameof(Options));
        NotifyPropertyChanged(nameof(IsNumber));
        NotifyPropertyChanged(nameof(IsBoolean));
        NotifyPropertyChanged(nameof(IsModel));
        NotifyPropertyChanged(nameof(IsText));
        NotifyPropertyChanged(nameof(IsJson));
        RefreshJsonFields();
        NotifyTypedValues();
    }

    private void NotifyTypedValues()
    {
        NotifyPropertyChanged(nameof(NumberValue));
        NotifyPropertyChanged(nameof(BooleanValue));
        NotifyPropertyChanged(nameof(TextValue));
        NotifyPropertyChanged(nameof(HasJsonFields));
    }

    private void RefreshJsonFields()
    {
        synchronizingJsonFields = true;
        try
        {
            JsonFields.Clear();
            if (Kind != "json")
            {
                return;
            }
            using var document = JsonDocument.Parse(JsonValue);
            if (document.RootElement.ValueKind != JsonValueKind.Object)
            {
                return;
            }
            foreach (var property in document.RootElement.EnumerateObject())
            {
                JsonFields.Add(new AnalogJsonFieldDraft(property.Name, property.Value.GetRawText()));
            }
        }
        catch (JsonException)
        {
        }
        finally
        {
            synchronizingJsonFields = false;
            NotifyPropertyChanged(nameof(HasJsonFields));
        }
    }

    private void JsonFieldsChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        if (args.OldItems is not null)
        {
            foreach (AnalogJsonFieldDraft field in args.OldItems)
            {
                field.Changed -= JsonFieldChanged;
            }
        }
        if (args.NewItems is not null)
        {
            foreach (AnalogJsonFieldDraft field in args.NewItems)
            {
                field.Changed += JsonFieldChanged;
            }
        }
        if (!synchronizingJsonFields)
        {
            UpdateJsonFromFields();
        }
        NotifyPropertyChanged(nameof(HasJsonFields));
    }

    private void JsonFieldChanged() => UpdateJsonFromFields();

    private void UpdateJsonFromFields()
    {
        if (synchronizingJsonFields)
        {
            return;
        }
        var value = new JsonObject();
        try
        {
            foreach (var field in JsonFields)
            {
                value[field.Name] = JsonNode.Parse(field.JsonValue);
            }
        }
        catch (JsonException)
        {
            return;
        }
        synchronizingJsonFields = true;
        try
        {
            JsonValue = value.ToJsonString();
        }
        finally
        {
            synchronizingJsonFields = false;
        }
    }
}

public sealed class AnalogComponentDraft : ModuleAuthoringDraft
{
    private string id;
    private string type;
    private double canvasX;
    private double canvasY;

    public AnalogComponentDraft(string id, string type, string pinA, string pinB, double value, string model = "")
        : this(id, type,
            [new AnalogPinDraft("pin-a", "引脚 A", pinA), new AnalogPinDraft("pin-b", "引脚 B", pinB)],
            BuildLegacyProperties(type, value, model))
    {
    }

    public AnalogComponentDraft(
        string id,
        string type,
        IEnumerable<AnalogPinDraft> pins,
        IEnumerable<AnalogComponentPropertyDraft>? properties = null,
        double canvasX = 0.0,
        double canvasY = 0.0)
    {
        this.id = id;
        this.type = type;
        this.canvasX = canvasX;
        this.canvasY = canvasY;
        Pins.CollectionChanged += PinsChanged;
        Properties.CollectionChanged += PropertiesChanged;
        foreach (var pin in pins)
        {
            Pins.Add(pin);
        }
        if (properties is not null)
        {
            foreach (var property in properties)
            {
                Properties.Add(property);
            }
        }
        RefreshPinDescriptors();
        RefreshPropertyDescriptors();
    }

    public string Id { get => id; set => Set(ref id, value); }
    public string Type
    {
        get => type;
        set
        {
            AnalogComponentCatalog.TryGet(type, out var previousDescriptor);
            if (!Set(ref type, value))
            {
                return;
            }
            ApplyDescriptor(value, previousDescriptor);
            NotifyPropertyChanged(nameof(Descriptor));
            NotifyPropertyChanged(nameof(SelectedDescriptor));
        }
    }
    public AnalogComponentDescriptor Descriptor => AnalogComponentCatalog.Get(Type);
    public AnalogComponentDescriptor SelectedDescriptor
    {
        get => Descriptor;
        set
        {
            if (value is not null)
            {
                Type = value.Type;
            }
        }
    }
    public ObservableCollection<AnalogPinDraft> Pins { get; } = [];
    public ObservableCollection<AnalogComponentPropertyDraft> Properties { get; } = [];
    public double CanvasX { get => canvasX; set => Set(ref canvasX, value); }
    public double CanvasY { get => canvasY; set => Set(ref canvasY, value); }

    public string PinA
    {
        get => Pins.ElementAtOrDefault(0)?.Node ?? string.Empty;
        set => SetPinNode(0, value);
    }
    public string PinB
    {
        get => Pins.ElementAtOrDefault(1)?.Node ?? string.Empty;
        set => SetPinNode(1, value);
    }
    public double Value
    {
        get => TryGetProperty("value", out var json) && double.TryParse(json,
            System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var parsed)
            ? parsed
            : 0.0;
        set => SetJsonProperty("value", value.ToString("R", System.Globalization.CultureInfo.InvariantCulture));
    }
    public string Model
    {
        get
        {
            if (!TryGetProperty("model", out var json))
            {
                return string.Empty;
            }
            try
            {
                return JsonSerializer.Deserialize<string>(json) ?? string.Empty;
            }
            catch (JsonException)
            {
                return string.Empty;
            }
        }
        set
        {
            if (string.IsNullOrWhiteSpace(value))
            {
                RemoveJsonProperty("model");
            }
            else
            {
                SetJsonProperty("model", JsonSerializer.Serialize(value.Trim()));
            }
        }
    }

    public bool TryGetProperty(string propertyId, out string jsonValue)
    {
        var property = Properties.FirstOrDefault(candidate =>
            string.Equals(candidate.Id, propertyId, StringComparison.Ordinal));
        jsonValue = property?.JsonValue ?? string.Empty;
        return property is not null;
    }

    public void SetJsonProperty(string propertyId, string jsonValue)
    {
        var property = Properties.FirstOrDefault(candidate =>
            string.Equals(candidate.Id, propertyId, StringComparison.Ordinal));
        if (property is null)
        {
            property = new AnalogComponentPropertyDraft(propertyId, jsonValue);
            Properties.Add(property);
        }
        else
        {
            property.JsonValue = jsonValue;
        }
        ApplyPropertyDescriptor(property);
        NotifyCompatibilityProperties(propertyId);
    }

    public bool RemoveJsonProperty(string propertyId)
    {
        var property = Properties.FirstOrDefault(candidate =>
            string.Equals(candidate.Id, propertyId, StringComparison.Ordinal));
        if (property is null || !Properties.Remove(property))
        {
            return false;
        }
        NotifyCompatibilityProperties(propertyId);
        return true;
    }

    private static IEnumerable<AnalogComponentPropertyDraft> BuildLegacyProperties(
        string type, double value, string model)
    {
        if (type is "resistor" or "capacitor" or "inductor" or "voltage-source" or "potentiometer")
        {
            yield return new AnalogComponentPropertyDraft("value",
                value.ToString("R", System.Globalization.CultureInfo.InvariantCulture));
        }
        if (!string.IsNullOrWhiteSpace(model))
        {
            yield return new AnalogComponentPropertyDraft("model", JsonSerializer.Serialize(model.Trim()));
        }
    }

    private void ApplyDescriptor(string componentType, AnalogComponentDescriptor? previousDescriptor)
    {
        if (!AnalogComponentCatalog.TryGet(componentType, out var descriptor))
        {
            return;
        }
        while (Pins.Count < descriptor.Pins.Count)
        {
            var pin = descriptor.Pins[Pins.Count];
            Pins.Add(new AnalogPinDraft(pin.Id, pin.DisplayName, string.Empty));
        }
        for (var index = 0; index < descriptor.Pins.Count; ++index)
        {
            Pins[index].ApplyDescriptor(descriptor.Pins[index]);
        }
        while (Pins.Count > descriptor.Pins.Count)
        {
            Pins.RemoveAt(Pins.Count - 1);
        }
        if (previousDescriptor is not null)
        {
            var retained = descriptor.Properties.Select(property => property.Id).ToHashSet(StringComparer.Ordinal);
            var previous = previousDescriptor.Properties.Select(property => property.Id).ToHashSet(StringComparer.Ordinal);
            foreach (var property in Properties.Where(property =>
                         previous.Contains(property.Id) && !retained.Contains(property.Id)).ToArray())
            {
                Properties.Remove(property);
            }
        }
        foreach (var property in descriptor.Properties)
        {
            if (!TryGetProperty(property.Id, out _))
            {
                Properties.Add(new AnalogComponentPropertyDraft(property.Id, AnalogComponentCatalog.DefaultJson(property)));
            }
        }
        RefreshPropertyDescriptors();
        NotifyPropertyChanged(nameof(PinA));
        NotifyPropertyChanged(nameof(PinB));
    }

    private void RefreshPinDescriptors()
    {
        if (!AnalogComponentCatalog.TryGet(Type, out var descriptor))
        {
            return;
        }
        for (var index = 0; index < Math.Min(Pins.Count, descriptor.Pins.Count); ++index)
        {
            Pins[index].ApplyDescriptor(descriptor.Pins[index]);
        }
    }

    private void SetPinNode(int index, string value)
    {
        while (Pins.Count <= index)
        {
            Pins.Add(new AnalogPinDraft($"pin-{Pins.Count + 1}", $"引脚 {Pins.Count + 1}", string.Empty));
        }
        Pins[index].Node = value;
        NotifyPropertyChanged(index == 0 ? nameof(PinA) : nameof(PinB));
    }

    private void PinsChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        SubscribeDraftItems(args, PinChanged);
        NotifyPropertyChanged(nameof(PinA));
        NotifyPropertyChanged(nameof(PinB));
        NotifyChanged();
    }

    private void PropertiesChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        SubscribeDraftItems(args, NestedPropertyChanged);
        NotifyPropertyChanged(nameof(Value));
        NotifyPropertyChanged(nameof(Model));
        NotifyChanged();
    }

    private static void SubscribeDraftItems(NotifyCollectionChangedEventArgs args, Action handler)
    {
        if (args.OldItems is not null)
        {
            foreach (ModuleAuthoringDraft item in args.OldItems)
            {
                item.Changed -= handler;
            }
        }
        if (args.NewItems is not null)
        {
            foreach (ModuleAuthoringDraft item in args.NewItems)
            {
                item.Changed += handler;
            }
        }
    }

    private void PinChanged()
    {
        NotifyPropertyChanged(nameof(PinA));
        NotifyPropertyChanged(nameof(PinB));
        NotifyChanged();
    }

    private void NestedPropertyChanged()
    {
        RefreshPropertyDescriptors();
        NotifyPropertyChanged(nameof(Value));
        NotifyPropertyChanged(nameof(Model));
        NotifyChanged();
    }

    private void NotifyCompatibilityProperties(string propertyId)
    {
        if (propertyId == "value")
        {
            NotifyPropertyChanged(nameof(Value));
        }
        else if (propertyId == "model")
        {
            NotifyPropertyChanged(nameof(Model));
        }
    }

    private void RefreshPropertyDescriptors()
    {
        foreach (var property in Properties)
        {
            ApplyPropertyDescriptor(property);
        }
    }

    private void ApplyPropertyDescriptor(AnalogComponentPropertyDraft property)
    {
        var descriptor = AnalogComponentCatalog.TryGet(Type, out var componentDescriptor)
            ? componentDescriptor.Properties.FirstOrDefault(candidate =>
                string.Equals(candidate.Id, property.Id, StringComparison.Ordinal))
            : null;
        property.ApplyDescriptor(descriptor);
    }
}

public sealed class AnalogCircuitPortDraft : ModuleAuthoringDraft
{
    private string id;
    private string direction;
    private string node;

    public AnalogCircuitPortDraft(string id, string direction, string node)
    {
        this.id = id;
        this.direction = direction;
        this.node = node;
    }

    public string Id { get => id; set => Set(ref id, value); }
    public string Direction { get => direction; set => Set(ref direction, value); }
    public string Node { get => node; set => Set(ref node, value); }
}

public sealed class AnalogSupplyDraft : ModuleAuthoringDraft
{
    private string id;
    private string positiveNode;
    private string negativeNode;
    private double voltage;

    public AnalogSupplyDraft(string id, string positiveNode, string negativeNode, double voltage)
    {
        this.id = id;
        this.positiveNode = positiveNode;
        this.negativeNode = negativeNode;
        this.voltage = voltage;
        Properties.CollectionChanged += PropertiesChanged;
    }

    public string Id { get => id; set => Set(ref id, value); }
    public string PositiveNode { get => positiveNode; set => Set(ref positiveNode, value); }
    public string NegativeNode { get => negativeNode; set => Set(ref negativeNode, value); }
    public double Voltage { get => voltage; set => Set(ref voltage, value); }
    public ObservableCollection<AnalogComponentPropertyDraft> Properties { get; } = [];

    private void PropertiesChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        if (args.OldItems is not null)
        {
            foreach (AnalogComponentPropertyDraft item in args.OldItems)
            {
                item.Changed -= NestedPropertyChanged;
            }
        }
        if (args.NewItems is not null)
        {
            foreach (AnalogComponentPropertyDraft item in args.NewItems)
            {
                item.Changed += NestedPropertyChanged;
            }
        }
        NotifyChanged();
    }

    private void NestedPropertyChanged() => NotifyChanged();
}

public sealed class AnalogParameterBindingDraft : ModuleAuthoringDraft
{
    private int modelVersion;
    private string parameterId;
    private string targetId;
    private string property;
    private double smoothingSeconds;

    public AnalogParameterBindingDraft(
        int modelVersion, string parameterId, string targetId, string property, double smoothingSeconds)
    {
        this.modelVersion = modelVersion;
        this.parameterId = parameterId;
        this.targetId = targetId;
        this.property = property;
        this.smoothingSeconds = smoothingSeconds;
    }

    public int ModelVersion { get => modelVersion; set => Set(ref modelVersion, value); }
    public string ParameterId { get => parameterId; set => Set(ref parameterId, value); }
    public string TargetId { get => targetId; set => Set(ref targetId, value); }
    public string Property { get => property; set => Set(ref property, value); }
    public double SmoothingSeconds { get => smoothingSeconds; set => Set(ref smoothingSeconds, value); }
}

public sealed class AnalogWirePointDraft : ModuleAuthoringDraft
{
    private double x;
    private double y;

    public AnalogWirePointDraft(double x, double y)
    {
        this.x = x;
        this.y = y;
    }

    public double X { get => x; set => Set(ref x, value); }
    public double Y { get => y; set => Set(ref y, value); }
}

public sealed class AnalogWireRouteDraft : ModuleAuthoringDraft
{
    private string node;

    public AnalogWireRouteDraft(string node)
    {
        this.node = node;
        Points.CollectionChanged += PointsChanged;
    }

    public string Node { get => node; set => Set(ref node, value); }
    public ObservableCollection<AnalogWirePointDraft> Points { get; } = [];

    private void PointsChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        if (args.OldItems is not null)
        {
            foreach (AnalogWirePointDraft point in args.OldItems)
            {
                point.Changed -= PointChanged;
            }
        }
        if (args.NewItems is not null)
        {
            foreach (AnalogWirePointDraft point in args.NewItems)
            {
                point.Changed += PointChanged;
            }
        }
        NotifyChanged();
    }

    private void PointChanged() => NotifyChanged();
}

public sealed class AnalogCircuitDraft : ModuleAuthoringDraft
{
    private string referenceGround = "0";
    private string inputNode = "in";
    private string outputNode = "out";
    private int oversampling = 1;
    private int maxIterations = 16;
    private double absoluteTolerance = 1e-9;
    private double relativeTolerance = 1e-6;

    public AnalogCircuitDraft()
    {
        Nodes.CollectionChanged += CollectionChanged;
        Ports.CollectionChanged += PortsChanged;
        Supplies.CollectionChanged += DraftsChanged;
        ParameterBindings.CollectionChanged += DraftsChanged;
        Components.CollectionChanged += DraftsChanged;
        WireRoutes.CollectionChanged += DraftsChanged;
        RootProperties.CollectionChanged += DraftsChanged;
        SolverProperties.CollectionChanged += DraftsChanged;
        ConvergenceProperties.CollectionChanged += DraftsChanged;
        Nodes.Add("in");
        Nodes.Add("out");
        Nodes.Add("0");
        Ports.Add(new AnalogCircuitPortDraft("input", "input", "in"));
        Ports.Add(new AnalogCircuitPortDraft("output", "output", "out"));
    }

    public string ReferenceGround { get => referenceGround; set => Set(ref referenceGround, value); }
    public string InputNode
    {
        get => inputNode;
        set
        {
            if (Set(ref inputNode, value))
            {
                SetCompatibilityPortNode("input", "input", value);
            }
        }
    }
    public string OutputNode
    {
        get => outputNode;
        set
        {
            if (Set(ref outputNode, value))
            {
                SetCompatibilityPortNode("output", "output", value);
            }
        }
    }
    public int Oversampling { get => oversampling; set => Set(ref oversampling, value); }
    public int MaxIterations { get => maxIterations; set => Set(ref maxIterations, value); }
    public double AbsoluteTolerance { get => absoluteTolerance; set => Set(ref absoluteTolerance, value); }
    public double RelativeTolerance { get => relativeTolerance; set => Set(ref relativeTolerance, value); }
    public ObservableCollection<string> Nodes { get; } = [];
    public ObservableCollection<AnalogCircuitPortDraft> Ports { get; } = [];
    public ObservableCollection<AnalogSupplyDraft> Supplies { get; } = [];
    public ObservableCollection<AnalogParameterBindingDraft> ParameterBindings { get; } = [];
    public ObservableCollection<AnalogComponentDraft> Components { get; } = [];
    public ObservableCollection<AnalogWireRouteDraft> WireRoutes { get; } = [];
    public ObservableCollection<AnalogComponentPropertyDraft> RootProperties { get; } = [];
    public ObservableCollection<AnalogComponentPropertyDraft> SolverProperties { get; } = [];
    public ObservableCollection<AnalogComponentPropertyDraft> ConvergenceProperties { get; } = [];

    private void SetCompatibilityPortNode(string preferredId, string direction, string value)
    {
        var port = Ports.FirstOrDefault(candidate => string.Equals(candidate.Id, preferredId, StringComparison.Ordinal)) ??
                   Ports.FirstOrDefault(candidate => string.Equals(candidate.Direction, direction, StringComparison.Ordinal));
        if (port is not null && !string.Equals(port.Node, value, StringComparison.Ordinal))
        {
            port.Node = value;
        }
    }

    private void SynchronizeCompatibilityNodes()
    {
        var nextInput = Ports.FirstOrDefault(port => string.Equals(port.Id, "input", StringComparison.Ordinal)) ??
                        Ports.FirstOrDefault(port => string.Equals(port.Direction, "input", StringComparison.Ordinal));
        var nextOutput = Ports.FirstOrDefault(port => string.Equals(port.Id, "output", StringComparison.Ordinal)) ??
                         Ports.FirstOrDefault(port => string.Equals(port.Direction, "output", StringComparison.Ordinal));
        if (nextInput is not null && !string.Equals(inputNode, nextInput.Node, StringComparison.Ordinal))
        {
            inputNode = nextInput.Node;
            NotifyPropertyChanged(nameof(InputNode));
        }
        if (nextOutput is not null && !string.Equals(outputNode, nextOutput.Node, StringComparison.Ordinal))
        {
            outputNode = nextOutput.Node;
            NotifyPropertyChanged(nameof(OutputNode));
        }
    }

    private void CollectionChanged(object? sender, NotifyCollectionChangedEventArgs args) => NotifyChanged();

    private void PortsChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        UpdateSubscriptions(args, PortChanged);
        SynchronizeCompatibilityNodes();
        NotifyChanged();
    }

    private void DraftsChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        UpdateSubscriptions(args, DraftChanged);
        NotifyChanged();
    }

    private static void UpdateSubscriptions(NotifyCollectionChangedEventArgs args, Action handler)
    {
        if (args.OldItems is not null)
        {
            foreach (ModuleAuthoringDraft item in args.OldItems)
            {
                item.Changed -= handler;
            }
        }
        if (args.NewItems is not null)
        {
            foreach (ModuleAuthoringDraft item in args.NewItems)
            {
                item.Changed += handler;
            }
        }
    }

    private void PortChanged()
    {
        SynchronizeCompatibilityNodes();
        NotifyChanged();
    }

    private void DraftChanged() => NotifyChanged();
}

public sealed class ModuleAuthoringDocument : INotifyPropertyChanged
{
    private sealed record HistoryEntry(LoadedState State, string Fingerprint);

    private sealed record LoadedState(
        ModuleAuthoringKind Kind,
        string ModuleId,
        string DisplayName,
        string PackageVersion,
        string Author,
        string Description,
        uint DeclaredLatencySamples,
        string LogicFile,
        string? SkinFile,
        string LogicSource,
        string? SourceAssetPath,
        IReadOnlyList<ModulePortDraft> Ports,
        IReadOnlyList<ModuleParameterDraft> Parameters,
        string AlgorithmId,
        IReadOnlyList<DigitalOperationDraft> DigitalOperations,
        bool CustomSkinEnabled,
        PedalSkinDraft Skin,
        bool AnalogStructuredMode,
        AnalogCircuitDraft AnalogCircuit);

    private static readonly Regex ModuleIdPattern = new("^[A-Za-z0-9][A-Za-z0-9._-]*$", RegexOptions.CultureInvariant);
    private static readonly Regex SkinColorPattern = new("^#[0-9A-Fa-f]{6}$", RegexOptions.CultureInvariant);
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };

    private string moduleId = string.Empty;
    private string displayName = string.Empty;
    private string packageVersion = "1.0.0";
    private string author = string.Empty;
    private string description = string.Empty;
    private uint declaredLatencySamples;
    private ModuleAuthoringKind kind;
    private string logicFile = string.Empty;
    private string? skinFile;
    private string logicSource = string.Empty;
    private string? sourceAssetPath;
    private string algorithmId = string.Empty;
    private bool customSkinEnabled;
    private bool analogStructuredMode;
    private string? projectDirectory;
    private string status = string.Empty;
    private bool isDirty;
    private bool suppressChanges;
    private IReadOnlyList<string> validationMessages = [];
    private IReadOnlyList<ModuleAuthoringIssue> validationIssues = [];
    private string? nativeValidationMessage;
    private string whiteboxGraphStatus = "尚未运行原生白盒验证";
    private string whiteboxRuntimeStatus = "尚未运行原生白盒验证";
    private string whiteboxEvidenceStatus = "尚未读取白盒证据状态";
    private string whiteboxDiagnostic = "校验项目后读取当前 generic white-box 边界";
    private string whiteboxRealtimeStatus = "尚未读取实时线程契约";
    private string auditionSummary = "尚未运行离线试听";
    private string auditionDetails = "";
    private int auditionStimulusIndex;
    private string auditionInputPortId = "input";
    private string auditionOutputPortId = "output";
    private double auditionAmplitude = 0.2;
    private double auditionStartFrequencyHz = 220.0;
    private double auditionEndFrequencyHz = 4000.0;
    private uint auditionSampleRateHz = 48000;
    private uint auditionBlockSize = 1024;
    private AuditionWaveFileSnapshot? auditionWaveFile;
    private ModuleAuditionAnalysisSnapshot? auditionAnalysis;
    private ModuleFrequencyResponseSnapshot? frequencyResponse;
    private string frequencyResponseSummary = "尚未测量频响";
    private string auditionParameterId = string.Empty;
    private double auditionParameterValueA;
    private double auditionParameterValueB = 1.0;
    private ModuleParameterSweepSnapshot? parameterSweep;
    private string parameterSweepSummary = "尚未运行参数扫动";
    private ModuleAuditionComparisonSnapshot? auditionComparison;
    private string auditionComparisonSummary = "尚未运行 A/B 对比";
    private readonly List<HistoryEntry> undoHistory = [];
    private readonly List<HistoryEntry> redoHistory = [];
    private HistoryEntry? currentHistory;
    private string? savedFingerprint;
    private int editTransactionDepth;
    private bool editTransactionChanged;
    private const int HistoryLimit = 100;
    private const double CircuitAuthoringDuplicateOffset = 32.0;

    public ModuleAuthoringDocument()
    {
        Ports.CollectionChanged += DraftCollectionChanged;
        Parameters.CollectionChanged += DraftCollectionChanged;
        DigitalOperations.CollectionChanged += DraftCollectionChanged;
        Skin.Changed += DraftChanged;
        AnalogCircuit.Changed += AnalogCircuitDraftChanged;
        NewProject(ModuleAuthoringKind.DigitalAlgorithm);
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    public event EventHandler? AnalogCircuitChanged;

    public static IReadOnlyList<ModuleAuthoringKindOption> KindOptions { get; } =
    [
        new(ModuleAuthoringKind.DigitalAlgorithm, "数字效果器", "使用受支持的数字处理操作创建模块"),
        new(ModuleAuthoringKind.AnalogCircuit, "白盒模拟电路", "使用 circuit.json v2 元件级网表创建模块"),
        new(ModuleAuthoringKind.NeuralAmpModel, "NAM / NAM A2", "将 NAM 或 NAM A2 模型封装为可安装模块"),
        new(ModuleAuthoringKind.ImpulseResponse, "IR 箱体", "将 WAV 脉冲响应封装为卷积模块"),
    ];
    public static IReadOnlyList<ModuleAuthoringTemplateOption> TemplateOptions { get; } =
    [
        new("digital-blank", ModuleAuthoringKind.DigitalAlgorithm, "空白数字效果器", "仅保留直通操作，从空白算法结构开始"),
        new("digital-gain", ModuleAuthoringKind.DigitalAlgorithm, "示例：数字增益", "单参数线性增益示例"),
        new("digital-delay", ModuleAuthoringKind.DigitalAlgorithm, "示例：数字延迟", "时间、反馈与干湿比延迟链示例"),
        new("analog-blank", ModuleAuthoringKind.AnalogCircuit, "空白白盒电路", "仅保留输入、输出和参考地，从元件级电路开始"),
        new("analog-divider", ModuleAuthoringKind.AnalogCircuit, "示例：模拟分压器", "双电阻 Circuit v2 白盒示例"),
        new("analog-diode-clipper", ModuleAuthoringKind.AnalogCircuit, "示例：二极管削波", "电阻与 Shockley 二极管削波示例"),
        new("nam-wrapper", ModuleAuthoringKind.NeuralAmpModel, "空白 NAM 模块", "导入 NAM 或 NAM A2 模型"),
        new("ir-cabinet", ModuleAuthoringKind.ImpulseResponse, "空白 IR 箱体", "导入 WAV 脉冲响应"),
    ];

    public static IReadOnlyList<ModuleAuthoringTemplateOption> TemplatesForKind(ModuleAuthoringKind projectKind) =>
        TemplateOptions.Where(option => option.Kind == projectKind).ToArray();
    public static IReadOnlyList<string> PortDirections { get; } = ["input", "output"];
    public static IReadOnlyList<string> ControlKinds { get; } = ["knob", "toggle", "button", "footswitch"];
    public static IReadOnlyList<string> DigitalOperationTypes { get; } =
        ["gain", "delay", "feedback", "mix", "filter", "waveshaper", "dynamics", "pitch-shift", "bypass"];
    public static IReadOnlyList<string> AnalogComponentTypes { get; } =
        AnalogComponentCatalog.Components.Select(component => component.Type).ToArray();
    public static IReadOnlyList<string> AnalogBindingProperties { get; } =
        ["potentiometer.position", "switch.state", "supply.voltage"];
    public static IReadOnlyList<string> AuditionStimulusOptions { get; } =
        ["正弦", "脉冲", "对数扫频", "WAV 文件"];

    public ObservableCollection<ModulePortDraft> Ports { get; } = [];
    public ObservableCollection<ModuleParameterDraft> Parameters { get; } = [];
    public ObservableCollection<DigitalOperationDraft> DigitalOperations { get; } = [];
    public PedalSkinDraft Skin { get; } = new();
    public AnalogCircuitDraft AnalogCircuit { get; } = new();

    public string ModuleId { get => moduleId; set => SetDocument(ref moduleId, value); }
    public string DisplayName { get => displayName; set => SetDocument(ref displayName, value); }
    public string PackageVersion { get => packageVersion; set => SetDocument(ref packageVersion, value); }
    public string Author { get => author; set => SetDocument(ref author, value); }
    public string Description { get => description; set => SetDocument(ref description, value); }
    public uint DeclaredLatencySamples { get => declaredLatencySamples; set => SetDocument(ref declaredLatencySamples, value); }
    public ModuleAuthoringKind Kind => kind;
    public string KindDisplayName => KindOptions.First(option => option.Kind == Kind).DisplayName;
    public string LogicFile => logicFile;
    public string? SkinFile { get => skinFile; set => SetDocument(ref skinFile, NormalizeOptional(value)); }
    public string LogicSource { get => logicSource; set => SetDocument(ref logicSource, value); }
    public string? SourceAssetPath { get => sourceAssetPath; set => SetDocument(ref sourceAssetPath, NormalizeOptional(value)); }
    public string AlgorithmId { get => algorithmId; set => SetDocument(ref algorithmId, value); }
    public bool CustomSkinEnabled { get => customSkinEnabled; set => SetDocument(ref customSkinEnabled, value); }
    public bool AnalogStructuredMode
    {
        get => analogStructuredMode;
        set => SetAnalogStructuredMode(value);
    }
    public string? ProjectDirectory => projectDirectory;
    public bool IsDirty => isDirty;
    public bool HasProjectDirectory => !string.IsNullOrWhiteSpace(ProjectDirectory);
    public bool UsesStructuredDigitalLogic => Kind == ModuleAuthoringKind.DigitalAlgorithm;
    public bool IsAnalogLogic => Kind == ModuleAuthoringKind.AnalogCircuit;
    public bool UsesStructuredAnalogLogic => Kind == ModuleAuthoringKind.AnalogCircuit && AnalogStructuredMode;
    public bool UsesTextLogic => Kind == ModuleAuthoringKind.AnalogCircuit && !AnalogStructuredMode;
    public bool UsesJsonLogic => UsesStructuredDigitalLogic || UsesStructuredAnalogLogic || UsesTextLogic;
    public bool UsesExternalAsset => !UsesJsonLogic;
    public string AnalogEditorModeText => AnalogStructuredMode ? "高级 JSON" : "结构化网表";
    public string Status { get => status; private set => SetField(ref status, value); }
    public IReadOnlyList<string> ValidationMessages => validationMessages;
    public IReadOnlyList<ModuleAuthoringIssue> ValidationIssues => validationIssues;
    public string WhiteboxGraphStatus => whiteboxGraphStatus;
    public string WhiteboxRuntimeStatus => whiteboxRuntimeStatus;
    public string WhiteboxEvidenceStatus => whiteboxEvidenceStatus;
    public string WhiteboxDiagnostic => whiteboxDiagnostic;
    public string WhiteboxRealtimeStatus => whiteboxRealtimeStatus;
    public bool HasValidationIssues => validationIssues.Count > 0;
    public bool HasNoValidationIssues => validationIssues.Count == 0;
    public bool IsValid => validationIssues.All(issue => issue.IsWarning);
    public string ValidationSummary
    {
        get
        {
            var errorCount = validationIssues.Count(issue => !issue.IsWarning);
            var warningCount = validationIssues.Count - errorCount;
            return (errorCount, warningCount) switch
            {
                (0, 0) => "项目结构有效",
                (0, _) => $"项目有效 · {warningCount} 个警告",
                (_, 0) => $"发现 {errorCount} 个错误",
                _ => $"发现 {errorCount} 个错误 · {warningCount} 个警告",
            };
        }
    }
    public string FileStatus => ProjectDirectory is null
        ? $"未保存项目{(IsDirty ? " *" : string.Empty)}"
        : $"{ProjectDirectory}{(IsDirty ? " *" : string.Empty)}";
    public string WindowTitle => $"{DisplayName} - 效果器工坊{(IsDirty ? " *" : string.Empty)}";
    public string AuditionSummary { get => auditionSummary; private set => SetField(ref auditionSummary, value); }
    public string AuditionDetails { get => auditionDetails; private set => SetField(ref auditionDetails, value); }
    public int AuditionStimulusIndex
    {
        get => auditionStimulusIndex;
        set => SetAuditionSetting(ref auditionStimulusIndex, value);
    }
    public string AuditionInputPortId
    {
        get => auditionInputPortId;
        set => SetAuditionSetting(ref auditionInputPortId, value ?? string.Empty);
    }
    public string AuditionOutputPortId
    {
        get => auditionOutputPortId;
        set => SetAuditionSetting(ref auditionOutputPortId, value ?? string.Empty);
    }
    public double AuditionAmplitude
    {
        get => auditionAmplitude;
        set => SetAuditionSetting(ref auditionAmplitude, value);
    }
    public double AuditionStartFrequencyHz
    {
        get => auditionStartFrequencyHz;
        set => SetAuditionSetting(ref auditionStartFrequencyHz, value);
    }
    public double AuditionEndFrequencyHz
    {
        get => auditionEndFrequencyHz;
        set => SetAuditionSetting(ref auditionEndFrequencyHz, value);
    }
    public uint AuditionSampleRateHz
    {
        get => auditionSampleRateHz;
        set => SetAuditionSetting(ref auditionSampleRateHz, value);
    }
    public uint AuditionBlockSize
    {
        get => auditionBlockSize;
        set => SetAuditionSetting(ref auditionBlockSize, value);
    }
    public string AuditionFilePath => auditionWaveFile?.SourcePath ?? string.Empty;
    public bool HasAuditionFile => auditionWaveFile is not null;
    public string AuditionFileSummary => auditionWaveFile is null
        ? "未选择 WAV 文件"
        : $"{Path.GetFileName(auditionWaveFile.SourcePath)} · {auditionWaveFile.Encoding} · " +
          $"{auditionWaveFile.SourceChannelCount} ch · {auditionWaveFile.SampleRateHz} Hz · " +
          $"{auditionWaveFile.FrameCount} frames" +
          (auditionWaveFile.SourceChannelCount > 1 ? " · 试听时下混为单声道" : string.Empty);
    public ModuleAuditionAnalysisSnapshot? AuditionAnalysis => auditionAnalysis;
    public ModuleFrequencyResponseSnapshot? FrequencyResponse => frequencyResponse;
    public string FrequencyResponseSummary
    {
        get => frequencyResponseSummary;
        private set => SetField(ref frequencyResponseSummary, value);
    }
    public IReadOnlyList<string> AuditionParameterOptions => Parameters.Select(parameter => parameter.Id).ToArray();
    public bool HasAuditionParameterOptions => AuditionParameterOptions.Count > 0;
    public string AuditionParameterId
    {
        get => auditionParameterId;
        set
        {
            if (!SetAuditionSetting(ref auditionParameterId, value ?? string.Empty)) return;
            var parameter = SelectedAuditionParameter;
            OnPropertiesChanged(nameof(SelectedAuditionParameter));
            if (parameter is not null)
            {
                auditionParameterValueA = parameter.Minimum;
                auditionParameterValueB = parameter.Maximum;
                OnPropertiesChanged(nameof(AuditionParameterValueA), nameof(AuditionParameterValueB));
            }
        }
    }
    public double AuditionParameterValueA
    {
        get => auditionParameterValueA;
        set => SetAuditionSetting(ref auditionParameterValueA, value);
    }
    public double AuditionParameterValueB
    {
        get => auditionParameterValueB;
        set => SetAuditionSetting(ref auditionParameterValueB, value);
    }
    public ModuleParameterDraft? SelectedAuditionParameter => Parameters.FirstOrDefault(parameter =>
        string.Equals(parameter.Id, AuditionParameterId, StringComparison.Ordinal));
    public ModuleParameterSweepSnapshot? ParameterSweep => parameterSweep;
    public string ParameterSweepSummary
    {
        get => parameterSweepSummary;
        private set => SetField(ref parameterSweepSummary, value);
    }
    public ModuleAuditionComparisonSnapshot? AuditionComparison => auditionComparison;
    public string AuditionComparisonSummary
    {
        get => auditionComparisonSummary;
        private set => SetField(ref auditionComparisonSummary, value);
    }
    public IReadOnlyList<string> AuditionInputPortOptions => IsAnalogLogic
        ? AnalogCircuit.Ports.Where(port => port.Direction == "input").Select(port => port.Id).ToArray()
        : Ports.Where(port => port.Direction == "input").Select(port => port.Id).ToArray();
    public IReadOnlyList<string> AuditionOutputPortOptions => IsAnalogLogic
        ? AnalogCircuit.Ports.Where(port => port.Direction == "output").Select(port => port.Id).ToArray()
        : Ports.Where(port => port.Direction == "output").Select(port => port.Id).ToArray();
    public bool CanUndo => undoHistory.Count > 0;
    public bool CanRedo => redoHistory.Count > 0;

    public void BeginEditTransaction()
    {
        editTransactionDepth++;
    }

    public void EndEditTransaction()
    {
        if (editTransactionDepth == 0)
        {
            return;
        }
        editTransactionDepth--;
        if (editTransactionDepth != 0 || !editTransactionChanged)
        {
            return;
        }
        editTransactionChanged = false;
        CommitHistoryChange();
        MarkDirty();
        RefreshValidation();
    }

    public void NewProject(ModuleAuthoringKind projectKind)
    {
        suppressChanges = true;
        try
        {
            kind = projectKind;
            moduleId = "org.example.solorig.new-module";
            displayName = "New SoloRig Module";
            packageVersion = "1.0.0";
            author = string.Empty;
            description = string.Empty;
            declaredLatencySamples = 0;
            skinFile = null;
            projectDirectory = null;
            sourceAssetPath = null;
            algorithmId = projectKind == ModuleAuthoringKind.DigitalAlgorithm ? "solorig.gain" : string.Empty;
            customSkinEnabled = false;
            ApplySkin(new PedalSkinDraft());
            analogStructuredMode = projectKind == ModuleAuthoringKind.AnalogCircuit;
            ApplyAnalogCircuit(DefaultAnalogCircuit());
            nativeValidationMessage = null;
            whiteboxGraphStatus = "尚未运行原生白盒验证";
            whiteboxRuntimeStatus = "尚未运行原生白盒验证";
            whiteboxEvidenceStatus = "尚未读取白盒证据状态";
            whiteboxDiagnostic = "校验项目后读取当前 generic white-box 边界";
            whiteboxRealtimeStatus = "尚未读取实时线程契约";
            logicFile = DefaultLogicFile(projectKind);
            logicSource = DefaultLogicSource(projectKind);
            ReplaceDrafts(Ports,
            [
                new ModulePortDraft("input", "input", 2),
                new ModulePortDraft("output", "output", 2),
            ]);
            ReplaceDrafts(Parameters, DefaultParameters(projectKind));
            ReplaceDrafts(DigitalOperations, projectKind == ModuleAuthoringKind.DigitalAlgorithm
                ? [new DigitalOperationDraft("gain", "gain")]
                : []);
            ResetAuditionParameterSelection();
            auditionWaveFile = null;
            if (auditionStimulusIndex == 3) auditionStimulusIndex = 0;
            isDirty = false;
            status = $"已创建{KindOptions.First(option => option.Kind == projectKind).DisplayName}项目";
            auditionSummary = "尚未运行离线试听";
            auditionDetails = string.Empty;
            auditionAnalysis = null;
            frequencyResponse = null;
            frequencyResponseSummary = "尚未测量频响";
        }
        finally
        {
            suppressChanges = false;
        }
        RefreshValidation();
        ResetHistory(true);
        OnPropertiesChanged(string.Empty);
    }

    public void NewProjectFromTemplate(string templateId)
    {
        var template = TemplateOptions.FirstOrDefault(option =>
            string.Equals(option.Id, templateId, StringComparison.Ordinal)) ?? TemplateOptions[0];
        NewProject(template.Kind);
        suppressChanges = true;
        try
        {
            switch (template.Id)
            {
                case "digital-blank":
                    moduleId = "org.example.solorig.new-digital-effect";
                    displayName = "New Digital Effect";
                    description = "Blank digital effect with a pass-through operation";
                    algorithmId = "solorig.custom";
                    ReplaceDrafts(Parameters, []);
                    ReplaceDrafts(DigitalOperations, [new DigitalOperationDraft("bypass")]);
                    break;
                case "digital-delay":
                    moduleId = "org.example.solorig.new-delay";
                    displayName = "New Digital Delay";
                    description = "Digital delay with time, feedback and wet mix controls";
                    algorithmId = "solorig.delay";
                    ReplaceDrafts(Parameters,
                    [
                        new ModuleParameterDraft("time", "Time", "s", 0.001, 2.0, 0.25),
                        new ModuleParameterDraft("feedback", "Feedback", "%", 0.0, 0.95, 0.25),
                        new ModuleParameterDraft("mix", "Mix", "%", 0.0, 1.0, 0.5),
                    ]);
                    ReplaceDrafts(DigitalOperations,
                    [
                        new DigitalOperationDraft("delay", "time"),
                        new DigitalOperationDraft("feedback", "feedback"),
                        new DigitalOperationDraft("mix", "mix"),
                    ]);
                    break;
                case "analog-blank":
                    moduleId = "org.example.solorig.new-analog-effect";
                    displayName = "New Analog Effect";
                    description = "Blank component-level Circuit v2 effect";
                    ReplaceDrafts(AnalogCircuit.Components, []);
                    break;
                case "analog-divider":
                    moduleId = "org.example.solorig.new-divider";
                    displayName = "New Analog Divider";
                    description = "Component-level resistor divider using Circuit v2 MNA";
                    break;
                case "analog-diode-clipper":
                    moduleId = "org.example.solorig.new-diode-clipper";
                    displayName = "New Diode Clipper";
                    description = "Component-level diode clipper using Circuit v2 MNA";
                    var diode = AddAnalogComponent("diode", 600.0, 120.0);
                    diode.Id = "clipper";
                    diode.Pins[0].Node = "out";
                    diode.Pins[1].Node = "0";
                    break;
                case "nam-wrapper":
                    moduleId = "org.example.solorig.new-nam";
                    displayName = "New NAM Module";
                    break;
                case "ir-cabinet":
                    moduleId = "org.example.solorig.new-ir-cabinet";
                    displayName = "New IR Cabinet";
                    break;
                case "digital-gain":
                    moduleId = "org.example.solorig.new-gain";
                    displayName = "New Digital Gain";
                    break;
            }
            status = $"已从模板创建：{template.DisplayName}";
            ResetAuditionParameterSelection();
        }
        finally
        {
            suppressChanges = false;
        }
        RefreshValidation();
        ResetHistory(true);
        OnPropertiesChanged(string.Empty);
    }

    public bool OpenFromDirectory(string directory)
    {
        try
        {
            var fullDirectory = Path.GetFullPath(directory);
            var state = ReadProject(fullDirectory);
            suppressChanges = true;
            try
            {
                kind = state.Kind;
                moduleId = state.ModuleId;
                displayName = state.DisplayName;
                packageVersion = state.PackageVersion;
                author = state.Author;
                description = state.Description;
                declaredLatencySamples = state.DeclaredLatencySamples;
                logicFile = state.LogicFile;
                skinFile = state.SkinFile;
                logicSource = state.LogicSource;
                sourceAssetPath = state.SourceAssetPath;
                algorithmId = state.AlgorithmId;
                customSkinEnabled = state.CustomSkinEnabled;
                ApplySkin(state.Skin);
                analogStructuredMode = state.AnalogStructuredMode;
                ApplyAnalogCircuit(state.AnalogCircuit);
                nativeValidationMessage = null;
                projectDirectory = fullDirectory;
                ReplaceDrafts(Ports, state.Ports);
                ReplaceDrafts(Parameters, state.Parameters);
                ReplaceDrafts(DigitalOperations, state.DigitalOperations);
                ResetAuditionParameterSelection();
                auditionWaveFile = null;
                if (auditionStimulusIndex == 3) auditionStimulusIndex = 0;
                isDirty = false;
                status = $"已打开 {fullDirectory}";
                auditionSummary = "尚未运行离线试听";
                auditionDetails = string.Empty;
                auditionAnalysis = null;
                frequencyResponse = null;
                frequencyResponseSummary = "尚未测量频响";
            }
            finally
            {
                suppressChanges = false;
            }
            RefreshValidation();
            ResetHistory(true);
            OnPropertiesChanged(string.Empty);
            return true;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException or InvalidDataException)
        {
            Status = $"打开失败：{exception.Message}";
            return false;
        }
    }

    public bool Save()
    {
        if (ProjectDirectory is null)
        {
            Status = "请先选择项目目录";
            return false;
        }
        return SaveToDirectory(ProjectDirectory);
    }

    public bool SaveToDirectory(string directory)
    {
        RefreshValidation();
        if (!IsValid)
        {
            Status = $"保存失败：{ValidationSummary}";
            return false;
        }

        string? stagingDirectory = null;
        try
        {
            var fullDirectory = Path.GetFullPath(directory);
            stagingDirectory = ModulePackageWorkflow.CreateStagingDirectory(fullDirectory, preserveExisting: true);
            var destinationLogicFile = LogicFile;
            if (UsesExternalAsset)
            {
                var extension = Path.GetExtension(SourceAssetPath!);
                destinationLogicFile = Kind == ModuleAuthoringKind.NeuralAmpModel
                    ? $"logic/model{extension.ToLowerInvariant()}"
                    : "logic/impulse.wav";
            }
            if (!IsSafeRelativePath(destinationLogicFile))
            {
                throw new InvalidDataException("逻辑文件路径不安全");
            }

            var effectiveSkinFile = CustomSkinEnabled ? "ui/pedal.json" : SkinFile;
            var manifest = new
            {
                schemaVersion = 1,
                moduleId = ModuleId.Trim(),
                displayName = DisplayName.Trim(),
                packageVersion = PackageVersion.Trim(),
                author = Author.Trim(),
                description = Description.Trim(),
                category = CategoryText(Kind),
                logicFile = destinationLogicFile.Replace('\\', '/'),
                skinFile = string.IsNullOrWhiteSpace(effectiveSkinFile) ? null : effectiveSkinFile!.Replace('\\', '/'),
                declaredLatencySamples = DeclaredLatencySamples,
                ports = Ports.Select(port => new
                {
                    id = port.Id.Trim(),
                    direction = port.Direction,
                    maxChannels = port.MaxChannels,
                }),
                parameters = Parameters.Select(parameter => new
                {
                    id = parameter.Id.Trim(),
                    displayName = parameter.DisplayName.Trim(),
                    unit = parameter.Unit.Trim(),
                    minimum = parameter.Minimum,
                    maximum = parameter.Maximum,
                    @default = parameter.DefaultValue,
                    control = parameter.ControlKind,
                }),
            };

            AtomicWriteText(Path.Combine(stagingDirectory, "manifest.json"), JsonSerializer.Serialize(manifest, JsonOptions));
            var stagedLogicDestination = Path.Combine(stagingDirectory,
                destinationLogicFile.Replace('/', Path.DirectorySeparatorChar));
            if (UsesJsonLogic)
            {
                var serializedLogic = UsesStructuredDigitalLogic
                    ? BuildDigitalLogicSource()
                    : UsesStructuredAnalogLogic ? BuildAnalogCircuitSource() : LogicSource;
                AtomicWriteText(stagedLogicDestination, serializedLogic);
                logicSource = serializedLogic;
            }
            else
            {
                AtomicCopyFile(SourceAssetPath!, stagedLogicDestination);
            }
            if (CustomSkinEnabled)
            {
                AtomicWriteText(Path.Combine(stagingDirectory, "ui", "pedal.json"), BuildSkinSource());
            }
            if (UsesStructuredAnalogLogic)
            {
                AtomicWriteText(Path.Combine(stagingDirectory, ".solorig", "workshop.json"),
                    BuildWorkshopMetadataSource());
            }
            ModulePackageWorkflow.CommitStagedDirectory(stagingDirectory, fullDirectory);
            stagingDirectory = null;

            suppressChanges = true;
            logicFile = destinationLogicFile.Replace('\\', '/');
            skinFile = effectiveSkinFile;
            sourceAssetPath = UsesExternalAsset
                ? Path.Combine(fullDirectory, destinationLogicFile.Replace('/', Path.DirectorySeparatorChar))
                : null;
            projectDirectory = fullDirectory;
            isDirty = false;
            suppressChanges = false;
            MarkSavedHistory();
            Status = $"已保存 {fullDirectory}";
            OnPropertiesChanged(nameof(LogicFile), nameof(SourceAssetPath), nameof(ProjectDirectory),
                nameof(HasProjectDirectory), nameof(IsDirty), nameof(FileStatus), nameof(WindowTitle),
                nameof(CanUndo), nameof(CanRedo));
            return true;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            if (stagingDirectory is not null)
            {
                try
                {
                    if (Directory.Exists(stagingDirectory)) Directory.Delete(stagingDirectory, true);
                }
                catch (Exception cleanupException) when (cleanupException is IOException or UnauthorizedAccessException)
                {
                }
            }
            suppressChanges = false;
            Status = $"保存失败：{exception.Message}";
            return false;
        }
    }

    public ModulePortDraft AddPort(string direction)
    {
        var prefix = string.Equals(direction, "output", StringComparison.Ordinal) ? "output" : "input";
        var id = UniqueId(prefix, Ports.Select(port => port.Id));
        var port = new ModulePortDraft(id, prefix, 2);
        Ports.Add(port);
        return port;
    }

    public bool RemovePort(ModulePortDraft port) => Ports.Remove(port);

    public ModuleParameterDraft AddParameter()
    {
        var id = UniqueId("parameter", Parameters.Select(parameter => parameter.Id));
        var parameter = new ModuleParameterDraft(id, id, string.Empty, 0.0, 1.0, 0.5);
        Parameters.Add(parameter);
        return parameter;
    }

    public bool RemoveParameter(ModuleParameterDraft parameter) => Parameters.Remove(parameter);

    public PedalSkinParameterPlacementDraft AddSkinParameterPlacement()
    {
        var parameterId = Parameters.Select(parameter => parameter.Id)
            .FirstOrDefault(id => Skin.ParameterPlacements.All(placement =>
                !string.Equals(placement.ParameterId, id, StringComparison.Ordinal)))
            ?? UniqueId("parameter", Skin.ParameterPlacements.Select(placement => placement.ParameterId));
        var index = Skin.ParameterPlacements.Count;
        var placement = new PedalSkinParameterPlacementDraft(parameterId,
            Math.Clamp(54.0 + index * 62.0, 26.0, Math.Max(26.0, Skin.Width - 26.0)),
            Math.Clamp(Skin.Height * 0.48, 42.0, Math.Max(42.0, Skin.Height - 58.0)));
        Skin.ParameterPlacements.Add(placement);
        var controlType = parameterId == string.Empty ? "knob.continuous" :
            Parameters.FirstOrDefault(parameter => string.Equals(parameter.Id, parameterId, StringComparison.Ordinal))?.ControlKind switch
            {
                "toggle" => "switch.toggle",
                "button" => "button.momentary",
                "footswitch" => "footswitch",
                _ => "knob.continuous",
            };
        if (Skin.Elements.All(element => !string.Equals(element.Binding, parameterId, StringComparison.Ordinal)))
        {
            Skin.Elements.Add(new PedalSkinElementDraft(
                $"parameter-{parameterId}", controlType, parameterId, placement.X, placement.Y));
        }
        return placement;
    }

    public bool RemoveSkinParameterPlacement(PedalSkinParameterPlacementDraft placement) =>
        Skin.ParameterPlacements.Remove(placement);

    public bool Undo()
    {
        if (undoHistory.Count == 0)
        {
            return false;
        }
        var target = undoHistory[^1];
        undoHistory.RemoveAt(undoHistory.Count - 1);
        if (currentHistory is not null)
        {
            PushHistory(redoHistory, currentHistory);
        }
        RestoreHistory(target, "已撤销上一步修改");
        return true;
    }

    public bool Redo()
    {
        if (redoHistory.Count == 0)
        {
            return false;
        }
        var target = redoHistory[^1];
        redoHistory.RemoveAt(redoHistory.Count - 1);
        if (currentHistory is not null)
        {
            PushHistory(undoHistory, currentHistory);
        }
        RestoreHistory(target, "已重做上一步修改");
        return true;
    }

    public DigitalOperationDraft AddDigitalOperation()
    {
        var operation = new DigitalOperationDraft("gain");
        DigitalOperations.Add(operation);
        return operation;
    }

    public bool RemoveDigitalOperation(DigitalOperationDraft operation) => DigitalOperations.Remove(operation);

    public AnalogComponentDraft AddAnalogComponent() => AddAnalogComponent("resistor");

    public AnalogComponentDraft AddAnalogComponent(string componentType, double canvasX = 0.0, double canvasY = 0.0)
    {
        var id = UniqueId("component", AnalogCircuit.Components.Select(component => component.Id));
        if (canvasX == 0.0 && canvasY == 0.0)
        {
            var index = AnalogCircuit.Components.Count;
            canvasX = 64.0 + index % 4 * 224.0;
            canvasY = 72.0 + index / 4 * 176.0;
        }
        var descriptor = AnalogComponentCatalog.Get(componentType);
        var pins = descriptor.Pins.Select((pin, index) => new AnalogPinDraft(
            pin.Id,
            pin.DisplayName,
            index switch
            {
                0 => AnalogCircuit.InputNode,
                1 => AnalogCircuit.OutputNode,
                _ => AnalogCircuit.ReferenceGround,
            })).ToArray();
        var properties = descriptor.Properties
            .Select(property => new AnalogComponentPropertyDraft(property.Id, AnalogComponentCatalog.DefaultJson(property)))
            .ToArray();
        var component = new AnalogComponentDraft(id, descriptor.Type, pins, properties, canvasX, canvasY);
        AnalogCircuit.Components.Add(component);
        return component;
    }

    public bool RemoveAnalogComponent(AnalogComponentDraft component) => AnalogCircuit.Components.Remove(component);

    public AnalogComponentDraft DuplicateAnalogComponent(
        AnalogComponentDraft source, double offset = CircuitAuthoringDuplicateOffset)
    {
        var duplicate = new AnalogComponentDraft(
            UniqueId(source.Id, AnalogCircuit.Components.Select(component => component.Id)),
            source.Type,
            source.Pins.Select(pin => new AnalogPinDraft(pin.Id, pin.DisplayName, pin.Node)),
            source.Properties.Select(property => new AnalogComponentPropertyDraft(property.Id, property.JsonValue)),
            Math.Min(source.CanvasX + offset, 2200.0),
            Math.Min(source.CanvasY + offset, 1460.0));
        AnalogCircuit.Components.Add(duplicate);
        return duplicate;
    }

    public bool ConnectAnalogPins(AnalogPinDraft first, AnalogPinDraft second)
    {
        if (ReferenceEquals(first, second))
        {
            return false;
        }
        var firstNode = first.Node.Trim();
        var secondNode = second.Node.Trim();
        var targetNode = !string.IsNullOrWhiteSpace(firstNode)
            ? firstNode
            : !string.IsNullOrWhiteSpace(secondNode) ? secondNode : NextAnalogNodeId();
        BeginEditTransaction();
        try
        {
            first.Node = targetNode;
            if (!string.IsNullOrWhiteSpace(secondNode) && !string.Equals(secondNode, targetNode, StringComparison.Ordinal))
            {
                ReplaceAnalogNode(secondNode, targetNode);
            }
            second.Node = targetNode;
            if (!AnalogCircuit.Nodes.Contains(targetNode, StringComparer.Ordinal))
            {
                AnalogCircuit.Nodes.Add(targetNode);
            }
        }
        finally
        {
            EndEditTransaction();
        }
        return true;
    }

    public bool DisconnectAnalogPin(AnalogPinDraft pin)
    {
        var node = NextAnalogNodeId();
        BeginEditTransaction();
        try
        {
            pin.Node = node;
            AnalogCircuit.Nodes.Add(node);
        }
        finally
        {
            EndEditTransaction();
        }
        return true;
    }

    public AnalogWireRouteDraft GetOrCreateAnalogWireRoute(string node)
    {
        var normalized = node.Trim();
        var existing = AnalogCircuit.WireRoutes.FirstOrDefault(route =>
            string.Equals(route.Node, normalized, StringComparison.Ordinal));
        if (existing is not null)
        {
            return existing;
        }
        var route = new AnalogWireRouteDraft(normalized);
        AnalogCircuit.WireRoutes.Add(route);
        return route;
    }

    public bool RemoveAnalogWireRoute(string node)
    {
        var route = AnalogCircuit.WireRoutes.FirstOrDefault(candidate =>
            string.Equals(candidate.Node, node.Trim(), StringComparison.Ordinal));
        return route is not null && AnalogCircuit.WireRoutes.Remove(route);
    }

    private string NextAnalogNodeId() => UniqueId("node", AnalogCircuit.Nodes
        .Concat(AnalogCircuit.Components.SelectMany(component => component.Pins.Select(pin => pin.Node)))
        .Concat(AnalogCircuit.Ports.Select(port => port.Node))
        .Concat(AnalogCircuit.Supplies.SelectMany(supply => new[] { supply.PositiveNode, supply.NegativeNode })));

    private void ReplaceAnalogNode(string previous, string replacement)
    {
        foreach (var pin in AnalogCircuit.Components.SelectMany(component => component.Pins)
                     .Where(pin => string.Equals(pin.Node, previous, StringComparison.Ordinal)))
        {
            pin.Node = replacement;
        }
        foreach (var port in AnalogCircuit.Ports.Where(port =>
                     string.Equals(port.Node, previous, StringComparison.Ordinal)))
        {
            port.Node = replacement;
        }
        foreach (var supply in AnalogCircuit.Supplies)
        {
            if (string.Equals(supply.PositiveNode, previous, StringComparison.Ordinal))
            {
                supply.PositiveNode = replacement;
            }
            if (string.Equals(supply.NegativeNode, previous, StringComparison.Ordinal))
            {
                supply.NegativeNode = replacement;
            }
        }
        var previousRoute = AnalogCircuit.WireRoutes.FirstOrDefault(route =>
            string.Equals(route.Node, previous, StringComparison.Ordinal));
        var replacementRoute = AnalogCircuit.WireRoutes.FirstOrDefault(route =>
            string.Equals(route.Node, replacement, StringComparison.Ordinal));
        if (previousRoute is not null && replacementRoute is null)
        {
            previousRoute.Node = replacement;
        }
        else if (previousRoute is not null && replacementRoute is not null)
        {
            AnalogCircuit.WireRoutes.Remove(previousRoute);
        }
    }

    public AnalogCircuitPortDraft AddAnalogCircuitPort(string direction = "input")
    {
        var normalizedDirection = direction == "output" ? "output" : "input";
        var id = UniqueId(normalizedDirection, AnalogCircuit.Ports.Select(port => port.Id));
        var port = new AnalogCircuitPortDraft(id, normalizedDirection,
            normalizedDirection == "output" ? AnalogCircuit.OutputNode : AnalogCircuit.InputNode);
        AnalogCircuit.Ports.Add(port);
        return port;
    }

    public bool RemoveAnalogCircuitPort(AnalogCircuitPortDraft port) => AnalogCircuit.Ports.Remove(port);

    public AnalogSupplyDraft AddAnalogSupply()
    {
        var id = UniqueId("supply", AnalogCircuit.Supplies.Select(supply => supply.Id));
        var supply = new AnalogSupplyDraft(id, "vcc", AnalogCircuit.ReferenceGround, 9.0);
        supply.Properties.Add(new AnalogComponentPropertyDraft("kind", "\"ideal\""));
        supply.Properties.Add(new AnalogComponentPropertyDraft("modelVersion", "1"));
        AnalogCircuit.Supplies.Add(supply);
        if (!AnalogCircuit.Nodes.Contains("vcc", StringComparer.Ordinal))
        {
            AnalogCircuit.Nodes.Add("vcc");
        }
        return supply;
    }

    public bool RemoveAnalogSupply(AnalogSupplyDraft supply) => AnalogCircuit.Supplies.Remove(supply);

    public AnalogParameterBindingDraft AddAnalogParameterBinding()
    {
        var parameterId = Parameters.FirstOrDefault()?.Id ?? string.Empty;
        var targetId = AnalogCircuit.Components.FirstOrDefault(component => component.Type == "potentiometer")?.Id ??
                       AnalogCircuit.Components.FirstOrDefault()?.Id ?? string.Empty;
        var binding = new AnalogParameterBindingDraft(1, parameterId, targetId,
            "potentiometer.position", 0.01);
        AnalogCircuit.ParameterBindings.Add(binding);
        return binding;
    }

    public bool RemoveAnalogParameterBinding(AnalogParameterBindingDraft binding) =>
        AnalogCircuit.ParameterBindings.Remove(binding);

    public bool SetAnalogStructuredMode(bool structured)
    {
        if (Kind != ModuleAuthoringKind.AnalogCircuit || analogStructuredMode == structured)
        {
            return Kind == ModuleAuthoringKind.AnalogCircuit;
        }
        if (structured)
        {
            if (!TryReadStructuredAnalog(LogicSource, out var parsed))
            {
                Status = "当前 JSON 包含结构化编辑器不能无损表示的电路，请继续使用高级 JSON 模式";
                return false;
            }
            suppressChanges = true;
            ApplyAnalogCircuit(parsed);
            analogStructuredMode = true;
            suppressChanges = false;
        }
        else
        {
            logicSource = BuildAnalogCircuitSource();
            analogStructuredMode = false;
            OnPropertiesChanged(nameof(LogicSource));
        }
        CommitHistoryChange();
        MarkDirty();
        RefreshValidation();
        OnPropertiesChanged(nameof(AnalogStructuredMode), nameof(UsesStructuredAnalogLogic),
            nameof(UsesTextLogic), nameof(UsesJsonLogic), nameof(AnalogEditorModeText));
        return true;
    }

    public void SetSourceAsset(string path)
    {
        SourceAssetPath = Path.GetFullPath(path);
        Status = $"已选择资源 {SourceAssetPath}";
    }

    public void ReportStatus(string message) => Status = message;

    public void SetNativeValidationResult(bool valid, string message)
    {
        nativeValidationMessage = valid ? null : message;
        RefreshValidation();
        Status = valid ? "原生模块加载器校验通过" : $"原生校验失败：{message}";
    }

    public void SetWhiteboxRuntimeStatus(WhiteboxStatusSnapshot whitebox,
        RealtimeContractSnapshot realtime)
    {
        whiteboxGraphStatus = whitebox.GraphStatus;
        whiteboxRuntimeStatus = whitebox.RuntimeStatus;
        whiteboxEvidenceStatus = whitebox.EvidenceStatus;
        whiteboxDiagnostic = whitebox.Diagnostic;
        whiteboxRealtimeStatus = realtime.IsRealtimeSafe
            ? $"实时线程契约：已满足 · {realtime.SampleFormatText} · 最大过采样 {realtime.MaximumOversampling}x"
            : "实时线程契约：未满足或尚未准备具体运行时";
        OnPropertiesChanged(nameof(WhiteboxGraphStatus), nameof(WhiteboxRuntimeStatus),
            nameof(WhiteboxEvidenceStatus), nameof(WhiteboxDiagnostic), nameof(WhiteboxRealtimeStatus));
    }

    public void SetAuditionResult(ModuleAuditionSnapshot result)
    {
        nativeValidationMessage = null;
        auditionAnalysis = null;
        AuditionSummary = result.OutputFinite ? "离线试听通过" : "离线试听输出异常";
        AuditionDetails = $"{result.SampleRateHz} Hz · {result.ChannelCount} ch · {result.BlockSize} samples · " +
                          $"Peak {result.OutputPeak:F4} · RMS {result.OutputRms:F4} · " +
                          $"Latency {result.DeclaredLatencySamples} samples · CPU {result.ProcessingCpuPercent:F2}%";
        OnPropertiesChanged(nameof(AuditionAnalysis));
        Status = AuditionSummary;
        RefreshValidation();
    }

    public ModuleAuditionRequest CreateAuditionRequest()
    {
        var inputOptions = AuditionInputPortOptions;
        var outputOptions = AuditionOutputPortOptions;
        var inputPort = inputOptions.Contains(AuditionInputPortId, StringComparer.Ordinal)
            ? AuditionInputPortId : inputOptions.FirstOrDefault() ?? string.Empty;
        var outputPort = outputOptions.Contains(AuditionOutputPortId, StringComparer.Ordinal)
            ? AuditionOutputPortId : outputOptions.FirstOrDefault() ?? string.Empty;
        var stimulus = AuditionStimulusIndex switch
        {
            1 => ModuleAuditionStimulus.Impulse,
            2 => ModuleAuditionStimulus.LogarithmicSweep,
            3 => ModuleAuditionStimulus.CallerWaveform,
            _ => ModuleAuditionStimulus.Sine,
        };
        if (stimulus == ModuleAuditionStimulus.CallerWaveform && auditionWaveFile is null)
        {
            throw new InvalidOperationException("请先选择用于文件试听的 WAV 文件");
        }
        return new ModuleAuditionRequest(AuditionSampleRateHz, AuditionBlockSize, 1U, stimulus,
            inputPort, outputPort, AuditionAmplitude, AuditionStartFrequencyHz, AuditionEndFrequencyHz,
            auditionWaveFile?.MonoSamples);
    }

    public void SetAuditionWaveFile(AuditionWaveFileSnapshot file)
    {
        ArgumentNullException.ThrowIfNull(file);
        auditionWaveFile = file;
        auditionStimulusIndex = 3;
        auditionSampleRateHz = file.SampleRateHz;
        auditionBlockSize = file.FrameCount;
        auditionAnalysis = null;
        frequencyResponse = null;
        frequencyResponseSummary = "试听文件已修改，需要重新测量频响";
        parameterSweep = null;
        parameterSweepSummary = "试听文件已修改，需要重新运行参数扫动";
        auditionComparison = null;
        auditionComparisonSummary = "试听文件已修改，需要重新运行 A/B 对比";
        auditionSummary = "WAV 文件已载入，等待离线试听";
        auditionDetails = AuditionFileSummary;
        OnPropertiesChanged(nameof(AuditionStimulusIndex), nameof(AuditionSampleRateHz),
            nameof(AuditionBlockSize), nameof(AuditionFilePath), nameof(HasAuditionFile),
            nameof(AuditionFileSummary), nameof(AuditionAnalysis), nameof(AuditionSummary),
            nameof(AuditionDetails), nameof(FrequencyResponse), nameof(FrequencyResponseSummary),
            nameof(ParameterSweep), nameof(ParameterSweepSummary),
            nameof(AuditionComparison), nameof(AuditionComparisonSummary));
        Status = $"已载入试听文件 {Path.GetFileName(file.SourcePath)}";
    }

    public void SetAuditionResult(ModuleAuditionAnalysisSnapshot result)
    {
        nativeValidationMessage = null;
        auditionAnalysis = result;
        AuditionSummary = result.OutputFinite ? "离线试听与测量通过" : "离线试听输出异常";
        AuditionDetails = $"{result.SampleRateHz} Hz · {result.ChannelCount} ch · {result.BlockSize} samples · " +
                          $"输入 Peak {result.InputPeak:F4} / RMS {result.InputRms:F4} / DC {result.InputDc:F5} · " +
                          $"输出 Peak {result.OutputPeak:F4} / RMS {result.OutputRms:F4} / DC {result.OutputDc:F5} · " +
                          $"Latency {result.DeclaredLatencySamples} samples · CPU {result.ProcessingCpuPercent:F2}%" +
                          (result.HasSolverDiagnostics
                              ? $" · Newton 平均 {result.AverageSolverIterations:F2} / 最大 {result.SolverMaximumIterations} · " +
                                $"失败 {result.SolverFailureCount}"
                              : string.Empty);
        OnPropertiesChanged(nameof(AuditionAnalysis));
        Status = AuditionSummary;
        RefreshValidation();
    }

    public void SetAuditionFailure(string message)
    {
        nativeValidationMessage = message;
        auditionAnalysis = null;
        AuditionSummary = "离线试听失败";
        AuditionDetails = message;
        OnPropertiesChanged(nameof(AuditionAnalysis));
        Status = $"离线试听失败：{message}";
        RefreshValidation();
    }

    public void SetFrequencyResponseResult(ModuleFrequencyResponseSnapshot result)
    {
        frequencyResponse = result;
        var minimum = result.Points.Count == 0 ? 0.0 : result.Points.Min(point => point.MagnitudeDb);
        var maximum = result.Points.Count == 0 ? 0.0 : result.Points.Max(point => point.MagnitudeDb);
        FrequencyResponseSummary = result.Points.Count == 0
            ? "频响测量没有返回采样点"
            : $"频响测量完成 · {result.Points.Count} 点 · {minimum:F1} 至 {maximum:F1} dB · " +
              $"平均 CPU {result.AverageCpuPercent:F2}%";
        OnPropertiesChanged(nameof(FrequencyResponse));
        Status = FrequencyResponseSummary;
    }

    public void SetFrequencyResponseFailure(string message)
    {
        frequencyResponse = null;
        FrequencyResponseSummary = $"频响测量失败：{message}";
        OnPropertiesChanged(nameof(FrequencyResponse));
        Status = FrequencyResponseSummary;
    }

    public void SetParameterSweepResult(ModuleParameterSweepSnapshot result)
    {
        parameterSweep = result;
        var minimumGain = result.Points.Min(point => point.GainDb);
        var maximumGain = result.Points.Max(point => point.GainDb);
        ParameterSweepSummary = $"参数扫动完成 · {result.ParameterId} · {result.Points.Count} 点 · " +
                                $"增益 {minimumGain:F1} 至 {maximumGain:F1} dB";
        OnPropertiesChanged(nameof(ParameterSweep));
        Status = ParameterSweepSummary;
    }

    public void SetParameterSweepFailure(string message)
    {
        parameterSweep = null;
        ParameterSweepSummary = $"参数扫动失败：{message}";
        OnPropertiesChanged(nameof(ParameterSweep));
        Status = ParameterSweepSummary;
    }

    public void SetAuditionComparisonResult(ModuleAuditionComparisonSnapshot result)
    {
        auditionComparison = result;
        AuditionComparisonSummary = $"A/B 完成 · {result.ParameterId} {result.ValueA:F3} → {result.ValueB:F3} · " +
                                    $"输出 RMS 差 {result.OutputRmsDeltaDb:+0.00;-0.00;0.00} dB";
        OnPropertiesChanged(nameof(AuditionComparison));
        Status = AuditionComparisonSummary;
    }

    public void SetAuditionComparisonFailure(string message)
    {
        auditionComparison = null;
        AuditionComparisonSummary = $"A/B 失败：{message}";
        OnPropertiesChanged(nameof(AuditionComparison));
        Status = AuditionComparisonSummary;
    }

    public IReadOnlyList<string> RefreshValidation()
    {
        SyncSkinPlacementControlKinds();
        var errors = new List<string>();
        var issues = new List<ModuleAuthoringIssue>();
        void AddIssue(string message, ModuleAuthoringIssueTarget target, string targetId = "",
            string fieldId = "", bool warning = false, object? targetObject = null)
        {
            errors.Add(message);
            issues.Add(new ModuleAuthoringIssue(message, target, targetId, fieldId, warning, targetObject));
        }
        if (string.IsNullOrWhiteSpace(ModuleId))
        {
            AddIssue("模块 ID 不能为空", ModuleAuthoringIssueTarget.Project, fieldId: "moduleId");
        }
        else if (!ModuleIdPattern.IsMatch(ModuleId.Trim()))
        {
            AddIssue("模块 ID 只能包含字母、数字、点、下划线和连字符",
                ModuleAuthoringIssueTarget.Project, fieldId: "moduleId");
        }
        if (string.IsNullOrWhiteSpace(DisplayName))
        {
            AddIssue("显示名称不能为空", ModuleAuthoringIssueTarget.Project, fieldId: "displayName");
        }
        if (string.IsNullOrWhiteSpace(PackageVersion))
        {
            AddIssue("版本不能为空", ModuleAuthoringIssueTarget.Project, fieldId: "packageVersion");
        }
        if (Ports.Count == 0 || !Ports.Any(port => port.IsInput) || !Ports.Any(port => !port.IsInput))
        {
            AddIssue("至少需要一个输入端口和一个输出端口", ModuleAuthoringIssueTarget.ModulePort);
        }
        foreach (var port in Ports)
        {
            if (string.IsNullOrWhiteSpace(port.Id))
            {
                AddIssue("端口 ID 不能为空", ModuleAuthoringIssueTarget.ModulePort,
                    fieldId: "id", targetObject: port);
            }
            if (port.Direction is not ("input" or "output"))
            {
                AddIssue($"端口 {port.Id} 的方向无效", ModuleAuthoringIssueTarget.ModulePort,
                    port.Id, "direction", targetObject: port);
            }
            if (port.MaxChannels is < 1 or > 64)
            {
                AddIssue($"端口 {port.Id} 的通道数必须在 1 到 64 之间",
                    ModuleAuthoringIssueTarget.ModulePort, port.Id, "maxChannels", targetObject: port);
            }
        }
        foreach (var duplicate in Ports.Where(port => !string.IsNullOrWhiteSpace(port.Id))
                     .GroupBy(port => port.Id.Trim(), StringComparer.Ordinal).Where(group => group.Count() > 1))
        {
            AddIssue($"端口 ID 重复：{duplicate.Key}", ModuleAuthoringIssueTarget.ModulePort,
                duplicate.Key, "id", targetObject: duplicate.First());
        }
        foreach (var parameter in Parameters)
        {
            if (string.IsNullOrWhiteSpace(parameter.Id))
            {
                AddIssue("参数 ID 不能为空", ModuleAuthoringIssueTarget.Parameter,
                    fieldId: "id", targetObject: parameter);
                continue;
            }
            if (!double.IsFinite(parameter.Minimum) || !double.IsFinite(parameter.Maximum) ||
                !double.IsFinite(parameter.DefaultValue) || parameter.Maximum <= parameter.Minimum)
            {
                AddIssue($"参数 {parameter.Id} 的范围无效", ModuleAuthoringIssueTarget.Parameter,
                    parameter.Id, "range", targetObject: parameter);
            }
            else if (parameter.DefaultValue < parameter.Minimum || parameter.DefaultValue > parameter.Maximum)
            {
                AddIssue($"参数 {parameter.Id} 的默认值不在范围内", ModuleAuthoringIssueTarget.Parameter,
                    parameter.Id, "default", targetObject: parameter);
            }
        }
        foreach (var duplicate in Parameters.Where(parameter => !string.IsNullOrWhiteSpace(parameter.Id))
                     .GroupBy(parameter => parameter.Id.Trim(), StringComparer.Ordinal).Where(group => group.Count() > 1))
        {
            AddIssue($"参数 ID 重复：{duplicate.Key}", ModuleAuthoringIssueTarget.Parameter,
                duplicate.Key, "id", targetObject: duplicate.First());
        }
        foreach (var parameter in Parameters)
        {
            if (!ControlKinds.Contains(parameter.ControlKind, StringComparer.Ordinal))
            {
                AddIssue($"参数 {parameter.Id} 的控件类型无效", ModuleAuthoringIssueTarget.Parameter,
                    parameter.Id, "control", targetObject: parameter);
            }
        }
        if (UsesStructuredDigitalLogic)
        {
            if (string.IsNullOrWhiteSpace(AlgorithmId))
            {
                AddIssue("数字算法 ID 不能为空", ModuleAuthoringIssueTarget.Logic, fieldId: "algorithmId");
            }
            if (DigitalOperations.Count == 0)
            {
                AddIssue("数字算法至少需要一个处理操作", ModuleAuthoringIssueTarget.Logic,
                    fieldId: "operations");
            }
            var parameterIds = Parameters.Select(parameter => parameter.Id.Trim()).ToHashSet(StringComparer.Ordinal);
            foreach (var operation in DigitalOperations)
            {
                if (!DigitalOperationTypes.Contains(operation.Type, StringComparer.Ordinal))
                {
                    AddIssue($"不支持的数字操作：{operation.Type}", ModuleAuthoringIssueTarget.Logic,
                        operation.Type, "type", targetObject: operation);
                }
                if (!string.IsNullOrWhiteSpace(operation.ParameterId) && !parameterIds.Contains(operation.ParameterId.Trim()))
                {
                    AddIssue($"操作 {operation.Type} 引用了未声明参数：{operation.ParameterId}",
                        ModuleAuthoringIssueTarget.Logic, operation.Type, "parameter", targetObject: operation);
                }
            }
        }
        else if (UsesStructuredAnalogLogic)
        {
            if (new[] { AnalogCircuit.ReferenceGround, AnalogCircuit.InputNode, AnalogCircuit.OutputNode }
                .Any(string.IsNullOrWhiteSpace))
            {
                AddIssue("模拟电路的参考地、输入节点和输出节点不能为空",
                    ModuleAuthoringIssueTarget.AnalogCircuit, fieldId: "nodes");
            }
            if (AnalogCircuit.InputNode == AnalogCircuit.OutputNode ||
                AnalogCircuit.InputNode == AnalogCircuit.ReferenceGround ||
                AnalogCircuit.OutputNode == AnalogCircuit.ReferenceGround)
            {
                AddIssue("模拟电路输入、输出和参考地节点必须互不相同",
                    ModuleAuthoringIssueTarget.AnalogCircuit, fieldId: "nodes");
            }
            if (AnalogCircuit.Oversampling is not (1 or 2 or 4 or 8 or 16))
            {
                AddIssue("模拟电路过采样只能是 1、2、4、8 或 16",
                    ModuleAuthoringIssueTarget.AnalogCircuit, fieldId: "oversampling");
            }
            if (AnalogCircuit.MaxIterations is < 1 or > 1024 ||
                !double.IsFinite(AnalogCircuit.AbsoluteTolerance) || AnalogCircuit.AbsoluteTolerance <= 0.0 ||
                !double.IsFinite(AnalogCircuit.RelativeTolerance) || AnalogCircuit.RelativeTolerance <= 0.0)
            {
                AddIssue("模拟电路收敛参数无效", ModuleAuthoringIssueTarget.AnalogCircuit,
                    fieldId: "convergence");
            }
            if (AnalogCircuit.Ports.Count == 0 ||
                !AnalogCircuit.Ports.Any(port => port.Direction == "input") ||
                !AnalogCircuit.Ports.Any(port => port.Direction == "output"))
            {
                AddIssue("模拟电路至少需要一个输入端口和一个输出端口",
                    ModuleAuthoringIssueTarget.AnalogPort);
            }
            foreach (var port in AnalogCircuit.Ports)
            {
                if (string.IsNullOrWhiteSpace(port.Id) || string.IsNullOrWhiteSpace(port.Node) ||
                    port.Direction is not ("input" or "output"))
                {
                    AddIssue($"模拟电路端口 {port.Id} 无效", ModuleAuthoringIssueTarget.AnalogPort,
                        port.Id, targetObject: port);
                }
            }
            foreach (var supply in AnalogCircuit.Supplies)
            {
                if (string.IsNullOrWhiteSpace(supply.Id) || string.IsNullOrWhiteSpace(supply.PositiveNode) ||
                    string.IsNullOrWhiteSpace(supply.NegativeNode) || supply.PositiveNode == supply.NegativeNode ||
                    !double.IsFinite(supply.Voltage))
                {
                    AddIssue($"模拟电源 {supply.Id} 无效", ModuleAuthoringIssueTarget.AnalogSupply,
                        supply.Id, targetObject: supply);
                }
                ValidateJsonProperties(message => AddIssue(message, ModuleAuthoringIssueTarget.AnalogSupply,
                    supply.Id, targetObject: supply), supply.Properties, $"电源 {supply.Id}");
            }
            if (AnalogCircuit.Components.Count == 0)
            {
                AddIssue("模拟电路至少需要一个元件", ModuleAuthoringIssueTarget.AnalogCircuit,
                    fieldId: "components", warning: true);
            }
            foreach (var component in AnalogCircuit.Components)
            {
                if (string.IsNullOrWhiteSpace(component.Id) || component.Pins.Any(pin => string.IsNullOrWhiteSpace(pin.Node)))
                {
                    AddIssue($"模拟元件 {component.Id} 的 ID 或引脚无效",
                        ModuleAuthoringIssueTarget.AnalogComponent, component.Id, targetObject: component);
                }
                if (!AnalogComponentCatalog.TryGet(component.Type, out var descriptor))
                {
                    AddIssue($"结构化编辑器不支持元件类型：{component.Type}",
                        ModuleAuthoringIssueTarget.AnalogComponent, component.Id, "type", targetObject: component);
                }
                else if (component.Pins.Count != descriptor.Pins.Count && component.Type != "relay")
                {
                    AddIssue($"元件 {component.Id} 需要 {descriptor.Pins.Count} 个引脚",
                        ModuleAuthoringIssueTarget.AnalogComponent, component.Id, "pins", targetObject: component);
                }
                if (component.Type is "resistor" or "capacitor" or "inductor" &&
                    (!double.IsFinite(component.Value) || component.Value <= 0.0))
                {
                    AddIssue($"元件 {component.Id} 需要正有限数值",
                        ModuleAuthoringIssueTarget.AnalogProperty, component.Id, "value", targetObject: component);
                }
                if (component.Type == "diode" && string.IsNullOrWhiteSpace(component.Model) &&
                    !component.TryGetProperty("diodeModel", out _))
                {
                    AddIssue($"二极管 {component.Id} 必须填写模型 ID 或内联模型",
                        ModuleAuthoringIssueTarget.AnalogProperty, component.Id, "model", targetObject: component);
                }
                if (!string.IsNullOrWhiteSpace(component.Model))
                {
                    AddIssue($"元件 {component.Id} 使用模型引用 {component.Model}；运行前必须由 ModelDatabase 授权",
                        ModuleAuthoringIssueTarget.AnalogProperty, component.Id, "model", warning: true,
                        targetObject: component);
                }
                foreach (var property in component.Properties)
                {
                    if (string.IsNullOrWhiteSpace(property.Id))
                    {
                        AddIssue($"元件 {component.Id} 包含空属性名",
                            ModuleAuthoringIssueTarget.AnalogProperty, component.Id, targetObject: component);
                        continue;
                    }
                    try
                    {
                        _ = JsonNode.Parse(property.JsonValue);
                    }
                    catch (JsonException exception)
                    {
                        AddIssue($"元件 {component.Id} 的属性 {property.Id} 不是有效 JSON：{exception.Message}",
                            ModuleAuthoringIssueTarget.AnalogProperty, component.Id, property.Id,
                            targetObject: component);
                    }
                }
                foreach (var duplicate in component.Properties.Where(property => !string.IsNullOrWhiteSpace(property.Id))
                             .GroupBy(property => property.Id.Trim(), StringComparer.Ordinal)
                             .Where(group => group.Count() > 1))
                {
                    AddIssue($"元件 {component.Id} 的属性重复：{duplicate.Key}",
                        ModuleAuthoringIssueTarget.AnalogProperty, component.Id, duplicate.Key,
                        targetObject: component);
                }
            }
            foreach (var duplicate in AnalogCircuit.Components.Where(component => !string.IsNullOrWhiteSpace(component.Id))
                         .GroupBy(component => component.Id.Trim(), StringComparer.Ordinal).Where(group => group.Count() > 1))
            {
                AddIssue($"模拟元件 ID 重复：{duplicate.Key}", ModuleAuthoringIssueTarget.AnalogComponent,
                    duplicate.Key, "id", targetObject: duplicate.First());
            }
            var allCircuitIds = AnalogCircuit.Ports.Select(port => port.Id)
                .Concat(AnalogCircuit.Supplies.Select(supply => supply.Id))
                .Concat(AnalogCircuit.Components.Select(component => component.Id))
                .Where(id => !string.IsNullOrWhiteSpace(id));
            foreach (var duplicate in allCircuitIds.GroupBy(id => id.Trim(), StringComparer.Ordinal)
                         .Where(group => group.Count() > 1))
            {
                object? targetObject = AnalogCircuit.Components.FirstOrDefault(component =>
                    component.Id.Trim() == duplicate.Key);
                targetObject ??= AnalogCircuit.Supplies.FirstOrDefault(supply => supply.Id.Trim() == duplicate.Key);
                targetObject ??= AnalogCircuit.Ports.FirstOrDefault(port => port.Id.Trim() == duplicate.Key);
                AddIssue($"模拟电路对象 ID 重复：{duplicate.Key}", ModuleAuthoringIssueTarget.AnalogCircuit,
                    duplicate.Key, "id", targetObject: targetObject);
            }
            var parameterIds = Parameters.Select(parameter => parameter.Id.Trim()).ToHashSet(StringComparer.Ordinal);
            var componentTypes = AnalogCircuit.Components.Where(component => !string.IsNullOrWhiteSpace(component.Id))
                .GroupBy(component => component.Id.Trim(), StringComparer.Ordinal)
                .ToDictionary(group => group.Key, group => group.First().Type, StringComparer.Ordinal);
            var supplyIds = AnalogCircuit.Supplies.Where(supply => !string.IsNullOrWhiteSpace(supply.Id))
                .Select(supply => supply.Id.Trim()).ToHashSet(StringComparer.Ordinal);
            foreach (var binding in AnalogCircuit.ParameterBindings)
            {
                if (binding.ModelVersion != 1 || !parameterIds.Contains(binding.ParameterId.Trim()) ||
                    !AnalogBindingProperties.Contains(binding.Property, StringComparer.Ordinal) ||
                    !double.IsFinite(binding.SmoothingSeconds) || binding.SmoothingSeconds < 0.0)
                {
                    AddIssue($"参数绑定 {binding.ParameterId} 无效", ModuleAuthoringIssueTarget.AnalogBinding,
                        binding.ParameterId, targetObject: binding);
                    continue;
                }
                var target = binding.TargetId.Trim();
                var validTarget = binding.Property switch
                {
                    "potentiometer.position" => componentTypes.TryGetValue(target, out var type) && type == "potentiometer",
                    "switch.state" => componentTypes.TryGetValue(target, out var type) && type == "switch",
                    "supply.voltage" => supplyIds.Contains(target),
                    _ => false,
                };
                if (!validTarget)
                {
                    AddIssue($"参数绑定 {binding.ParameterId} 的目标 {binding.TargetId} 与属性不匹配",
                        ModuleAuthoringIssueTarget.AnalogBinding, binding.ParameterId, "target",
                        targetObject: binding);
                }
            }
            var connectedNodes = AnalogCircuit.Ports.Select(port => port.Node.Trim())
                .Concat(AnalogCircuit.Supplies.SelectMany(supply =>
                    new[] { supply.PositiveNode.Trim(), supply.NegativeNode.Trim() }))
                .Append(AnalogCircuit.ReferenceGround.Trim())
                .Where(node => !string.IsNullOrWhiteSpace(node))
                .ToHashSet(StringComparer.Ordinal);
            var pinNodes = AnalogCircuit.Components.SelectMany(component => component.Pins.Select(pin =>
                (Component: component, Pin: pin, Node: pin.Node.Trim())))
                .Where(entry => !string.IsNullOrWhiteSpace(entry.Node))
                .ToArray();
            foreach (var controlOnlyNode in pinNodes.GroupBy(entry => entry.Node, StringComparer.Ordinal)
                         .Where(group => group.Count() > 1 &&
                                         group.All(entry => string.Equals(entry.Pin.ElectricalRole, "control",
                                             StringComparison.Ordinal))))
            {
                var entry = controlOnlyNode.First();
                AddIssue($"网络 {controlOnlyNode.Key} 仅连接控制引脚，可能缺少信号端或参考节点",
                    ModuleAuthoringIssueTarget.AnalogPin, entry.Component.Id, entry.Pin.Id,
                    warning: true, targetObject: entry.Component);
            }
            var declaredNodes = AnalogCircuit.Nodes
                .Where(node => !string.IsNullOrWhiteSpace(node))
                .Select(node => node.Trim())
                .ToHashSet(StringComparer.Ordinal);
            foreach (var entry in pinNodes.Where(entry => !declaredNodes.Contains(entry.Node)))
            {
                AddIssue($"引脚 {entry.Component.Id}.{entry.Pin.Id} 使用了未声明网络 {entry.Node}",
                    ModuleAuthoringIssueTarget.AnalogPin, entry.Component.Id, entry.Pin.Id,
                    warning: true, targetObject: entry.Component);
            }
            foreach (var duplicateNode in AnalogCircuit.Nodes.Where(node => !string.IsNullOrWhiteSpace(node))
                         .GroupBy(node => node.Trim(), StringComparer.Ordinal).Where(group => group.Count() > 1))
            {
                AddIssue($"网络声明重复：{duplicateNode.Key}", ModuleAuthoringIssueTarget.AnalogCircuit,
                    duplicateNode.Key, "nodes", warning: true);
            }
            foreach (var component in AnalogCircuit.Components.Where(component => component.Pins.Count == 2))
            {
                var firstNode = component.Pins[0].Node.Trim();
                var secondNode = component.Pins[1].Node.Trim();
                if (!string.IsNullOrWhiteSpace(firstNode) && string.Equals(firstNode, secondNode, StringComparison.Ordinal))
                {
                    foreach (var pin in component.Pins)
                    {
                        AddIssue($"元件 {component.Id} 的两个引脚都连接到 {firstNode}，可能形成短接",
                            ModuleAuthoringIssueTarget.AnalogPin, component.Id, pin.Id,
                            warning: true, targetObject: component);
                    }
                }
            }
            foreach (var component in AnalogCircuit.Components.Where(component => component.Pins.Count > 2))
            {
                foreach (var sharedNode in component.Pins
                             .Where(pin => !string.IsNullOrWhiteSpace(pin.Node))
                             .GroupBy(pin => pin.Node.Trim(), StringComparer.Ordinal)
                             .Where(group => group.Count() > 1))
                {
                    var pinNames = string.Join("、", sharedNode.Select(pin => pin.DisplayName));
                    foreach (var pin in sharedNode)
                    {
                        AddIssue($"元件 {component.Id} 的引脚 {pinNames} 共用网络 {sharedNode.Key}，可能形成角色短接",
                            ModuleAuthoringIssueTarget.AnalogPin, component.Id, pin.Id,
                            warning: true, targetObject: component);
                    }
                }
            }
            foreach (var group in pinNodes.GroupBy(entry => entry.Node, StringComparer.Ordinal)
                         .Where(group => group.Count() == 1 && !connectedNodes.Contains(group.Key)))
            {
                var entry = group.First();
                AddIssue($"节点 {group.Key} 只连接到一个元件引脚，可能悬空",
                    ModuleAuthoringIssueTarget.AnalogPin, entry.Component.Id, entry.Pin.Id,
                    warning: true, targetObject: entry.Component);
            }
            foreach (var port in AnalogCircuit.Ports.Where(port =>
                         pinNodes.All(entry => !string.Equals(entry.Node, port.Node.Trim(), StringComparison.Ordinal))))
            {
                AddIssue($"电路端口 {port.Id} 的节点 {port.Node} 未连接任何元件引脚",
                    ModuleAuthoringIssueTarget.AnalogPort, port.Id, "node", warning: true, targetObject: port);
            }
            var ground = AnalogCircuit.ReferenceGround.Trim();
            if (!string.IsNullOrWhiteSpace(ground) && pinNodes.All(entry => entry.Node != ground) &&
                AnalogCircuit.Supplies.All(supply => supply.NegativeNode.Trim() != ground &&
                                                     supply.PositiveNode.Trim() != ground))
            {
                AddIssue($"参考地节点 {ground} 未连接到任何元件或电源",
                    ModuleAuthoringIssueTarget.AnalogCircuit, ground, "referenceGround",
                    warning: AnalogCircuit.Components.Count == 0);
            }
            var usedNodes = pinNodes.Select(entry => entry.Node)
                .Concat(AnalogCircuit.Ports.Select(port => port.Node.Trim()))
                .Concat(AnalogCircuit.Supplies.SelectMany(supply =>
                    new[] { supply.PositiveNode.Trim(), supply.NegativeNode.Trim() }))
                .Append(ground)
                .Where(node => !string.IsNullOrWhiteSpace(node))
                .ToHashSet(StringComparer.Ordinal);
            foreach (var node in AnalogCircuit.Nodes.Where(node =>
                         !string.IsNullOrWhiteSpace(node) && !usedNodes.Contains(node.Trim())))
            {
                AddIssue($"声明节点 {node} 未被端口、电源或元件使用，运行时可能形成奇异矩阵",
                    ModuleAuthoringIssueTarget.AnalogCircuit, node.Trim(), "nodes", warning: true);
            }
            foreach (var duplicateRoute in AnalogCircuit.WireRoutes.Where(route => !string.IsNullOrWhiteSpace(route.Node))
                         .GroupBy(route => route.Node.Trim(), StringComparer.Ordinal).Where(group => group.Count() > 1))
            {
                AddIssue($"网络 {duplicateRoute.Key} 存在重复人工路由",
                    ModuleAuthoringIssueTarget.AnalogCircuit, duplicateRoute.Key, "wireRoutes",
                    warning: true, targetObject: duplicateRoute.First());
            }
            foreach (var route in AnalogCircuit.WireRoutes)
            {
                var routeNode = route.Node.Trim();
                if (string.IsNullOrWhiteSpace(routeNode) || !pinNodes.Any(entry => entry.Node == routeNode))
                {
                    AddIssue($"人工路由 {routeNode} 没有对应的元件网络",
                        ModuleAuthoringIssueTarget.AnalogCircuit, routeNode, "wireRoutes",
                        warning: true, targetObject: route);
                }
                foreach (var point in route.Points.Where(point =>
                             !double.IsFinite(point.X) || !double.IsFinite(point.Y) || point.X < 0.0 || point.Y < 0.0))
                {
                    AddIssue($"网络 {routeNode} 的折点坐标无效：{point.X:0.##},{point.Y:0.##}",
                        ModuleAuthoringIssueTarget.AnalogCircuit, routeNode, "wireRoutes",
                        warning: true, targetObject: route);
                }
            }
            ValidateJsonProperties(message => AddIssue(message, ModuleAuthoringIssueTarget.AnalogCircuit,
                fieldId: "root"), AnalogCircuit.RootProperties, "电路根对象");
            ValidateJsonProperties(message => AddIssue(message, ModuleAuthoringIssueTarget.AnalogCircuit,
                fieldId: "solver"), AnalogCircuit.SolverProperties, "求解器");
            ValidateJsonProperties(message => AddIssue(message, ModuleAuthoringIssueTarget.AnalogCircuit,
                fieldId: "convergence"), AnalogCircuit.ConvergenceProperties, "收敛设置");
        }
        else if (UsesTextLogic)
        {
            try
            {
                using var logic = JsonDocument.Parse(LogicSource);
                if (logic.RootElement.ValueKind != JsonValueKind.Object)
                {
                    errors.Add("逻辑文件必须是 JSON 对象");
                }
            }
            catch (JsonException exception)
            {
                errors.Add($"逻辑 JSON 无效：{exception.Message}");
            }
        }
        else if (string.IsNullOrWhiteSpace(SourceAssetPath) || !File.Exists(SourceAssetPath))
        {
            errors.Add(Kind == ModuleAuthoringKind.NeuralAmpModel ? "请选择有效的 NAM/NAM A2 文件" : "请选择有效的 WAV 文件");
        }
        else
        {
            var extension = Path.GetExtension(SourceAssetPath).ToLowerInvariant();
            if (Kind == ModuleAuthoringKind.NeuralAmpModel && extension is not (".nam" or ".nam-a2"))
            {
                errors.Add("NAM 模块只接受 .nam 或 .nam-a2 文件");
            }
            if (Kind == ModuleAuthoringKind.ImpulseResponse && extension != ".wav")
            {
                errors.Add("IR 模块只接受 .wav 文件");
            }
        }
        if (!string.IsNullOrWhiteSpace(SkinFile) && !IsSafeRelativePath(SkinFile))
        {
            errors.Add("外观文件必须是项目内的安全相对路径");
        }
        if (CustomSkinEnabled)
        {
            if (new[] { Skin.Id, Skin.KnobId, Skin.ToggleId, Skin.ButtonId, Skin.FootswitchId }
                .Any(string.IsNullOrWhiteSpace))
            {
                errors.Add("自定义外观和四类控件 ID 均不能为空");
            }
            foreach (var imagePath in new[] { Skin.KnobImage, Skin.ToggleImage, Skin.ButtonImage, Skin.FootswitchImage }
                         .Where(path => !string.IsNullOrWhiteSpace(path)))
            {
                if (!IsSafeRelativePath(imagePath))
                {
                    errors.Add($"外观图像路径不安全：{imagePath}");
                }
                else if (ProjectDirectory is not null)
                {
                    var skinRelative = CustomSkinEnabled ? "ui/pedal.json" : SkinFile ?? "ui/pedal.json";
                    var skinDirectory = Path.GetDirectoryName(Path.Combine(ProjectDirectory,
                        skinRelative.Replace('/', Path.DirectorySeparatorChar))) ?? ProjectDirectory;
                    if (!File.Exists(Path.Combine(skinDirectory,
                            imagePath.Replace('/', Path.DirectorySeparatorChar))))
                    {
                        errors.Add($"外观图像不存在：{imagePath}");
                    }
                }
            }
            if (!double.IsFinite(Skin.Width) || Skin.Width < 160.0 || Skin.Width > 520.0 ||
                !double.IsFinite(Skin.Height) || Skin.Height < 140.0 || Skin.Height > 520.0)
            {
                errors.Add("踏板尺寸必须位于宽 160-520、高 140-520");
            }
            foreach (var (name, color) in new[] { ("背景色", Skin.Background), ("标签色", Skin.LabelColor) })
            {
                if (!string.IsNullOrWhiteSpace(color) && !SkinColorPattern.IsMatch(color.Trim()))
                {
                    errors.Add($"{name}必须使用 #RRGGBB 格式");
                }
            }
            foreach (var duplicate in Skin.ParameterPlacements
                         .Where(placement => !string.IsNullOrWhiteSpace(placement.ParameterId))
                         .GroupBy(placement => placement.ParameterId.Trim(), StringComparer.Ordinal)
                         .Where(group => group.Count() > 1))
            {
                errors.Add($"外观参数坐标重复：{duplicate.Key}");
            }
            foreach (var placement in Skin.ParameterPlacements)
            {
                var parameterId = placement.ParameterId.Trim();
                if (string.IsNullOrWhiteSpace(parameterId) ||
                    Parameters.All(parameter => !string.Equals(parameter.Id.Trim(), parameterId, StringComparison.Ordinal)))
                {
                    errors.Add($"外观参数坐标引用了未知参数：{placement.ParameterId}");
                }
                if (!double.IsFinite(placement.X) || !double.IsFinite(placement.Y) ||
                    placement.X < 0.0 || placement.X > Skin.Width || placement.Y < 0.0 || placement.Y > Skin.Height)
                {
                    errors.Add($"外观参数 {placement.ParameterId} 的坐标超出踏板范围");
                }
            }
            foreach (var duplicate in Skin.Elements
                         .Where(element => !string.IsNullOrWhiteSpace(element.Id))
                         .GroupBy(element => element.Id.Trim(), StringComparer.Ordinal)
                         .Where(group => group.Count() > 1))
            {
                errors.Add($"外观元素 ID 重复：{duplicate.Key}");
            }
            foreach (var element in Skin.Elements)
            {
                if (string.IsNullOrWhiteSpace(element.Id))
                {
                    errors.Add("外观元素 ID 不能为空");
                }
                if (!PedalControlCatalog.Contains(element.Type))
                {
                    errors.Add($"外观元素类型未知：{element.Type}");
                }
                if (!string.IsNullOrWhiteSpace(element.Binding) &&
                    Parameters.All(parameter => !string.Equals(parameter.Id.Trim(), element.Binding.Trim(), StringComparison.Ordinal)) &&
                    Ports.All(port => !string.Equals(port.Id.Trim(), element.Binding.Trim(), StringComparison.Ordinal)))
                {
                    errors.Add($"外观元素 {element.Id} 引用了未知参数：{element.Binding}");
                }
                if (!double.IsFinite(element.X) || !double.IsFinite(element.Y) ||
                    !double.IsFinite(element.Width) || !double.IsFinite(element.Height) ||
                    !double.IsFinite(element.Rotation) || element.Width <= 0.0 || element.Height <= 0.0 ||
                    element.X < 0.0 || element.X > Skin.Width || element.Y < 0.0 || element.Y > Skin.Height ||
                    element.X + element.Width > Skin.Width || element.Y + element.Height > Skin.Height)
                {
                    errors.Add($"外观元素 {element.Id} 的位置或尺寸超出踏板范围");
                }
                if (element.Rotation < -360.0 || element.Rotation > 360.0 || element.Detents < 0 || element.Detents > 128)
                {
                    errors.Add($"外观元素 {element.Id} 的旋转或档位数无效");
                }
                if (string.IsNullOrWhiteSpace(element.Style))
                {
                    errors.Add($"外观元素 {element.Id} 的样式不能为空");
                }
            }
            if (Skin.UseCustomFootswitchPosition &&
                (!double.IsFinite(Skin.FootswitchX) || !double.IsFinite(Skin.FootswitchY) ||
                 Skin.FootswitchX < 0.0 || Skin.FootswitchX > Skin.Width ||
                 Skin.FootswitchY < 0.0 || Skin.FootswitchY > Skin.Height))
            {
                errors.Add("踩钉坐标超出踏板范围");
            }
        }
        if (!string.IsNullOrWhiteSpace(nativeValidationMessage))
        {
            var nativeTarget = ClassifyNativeIssue(nativeValidationMessage);
            AddIssue($"原生模块加载器：{nativeValidationMessage}", nativeTarget.Target,
                nativeTarget.TargetId, nativeTarget.FieldId, targetObject: nativeTarget.TargetObject);
        }

        foreach (var message in errors.Where(message => issues.All(issue => issue.Message != message)))
        {
            issues.Add(new ModuleAuthoringIssue(message, ModuleAuthoringIssueTarget.Project));
        }
        validationMessages = errors;
        validationIssues = issues;
        OnPropertiesChanged(nameof(ValidationMessages), nameof(ValidationIssues),
            nameof(HasValidationIssues), nameof(HasNoValidationIssues), nameof(IsValid), nameof(ValidationSummary));
        return validationMessages;
    }

    private (ModuleAuthoringIssueTarget Target, string TargetId, string FieldId, object? TargetObject)
        ClassifyNativeIssue(string message)
    {
        var normalized = message.ToLowerInvariant();
        if (normalized.Contains("model") || normalized.Contains("authorization"))
        {
            var component = AnalogCircuit.Components.FirstOrDefault(candidate => !string.IsNullOrWhiteSpace(candidate.Model));
            return (component is null ? ModuleAuthoringIssueTarget.NativeRuntime : ModuleAuthoringIssueTarget.AnalogProperty,
                component?.Id ?? string.Empty, "model", component);
        }
        if (normalized.Contains("singular") || normalized.Contains("conver") || normalized.Contains("prepare") ||
            normalized.Contains("runtime"))
        {
            return (ModuleAuthoringIssueTarget.AnalogCircuit, string.Empty, "solver", null);
        }
        return (ModuleAuthoringIssueTarget.NativeRuntime, string.Empty, string.Empty, null);
    }

    private static LoadedState ReadProject(string directory)
    {
        var manifestPath = Path.Combine(directory, "manifest.json");
        using var manifest = JsonDocument.Parse(File.ReadAllText(manifestPath));
        var root = manifest.RootElement;
        if (root.ValueKind != JsonValueKind.Object || root.GetProperty("schemaVersion").GetUInt32() != 1)
        {
            throw new InvalidDataException("只支持 schemaVersion 1 模块包");
        }
        var category = root.GetProperty("category").GetString();
        var projectKind = category switch
        {
            "digital-algorithm" => ModuleAuthoringKind.DigitalAlgorithm,
            "analog-circuit" => ModuleAuthoringKind.AnalogCircuit,
            "neural-amp-model" => ModuleAuthoringKind.NeuralAmpModel,
            "impulse-response" => ModuleAuthoringKind.ImpulseResponse,
            _ => throw new InvalidDataException($"不支持的模块类别：{category}"),
        };
        var loadedLogicFile = root.GetProperty("logicFile").GetString() ?? throw new InvalidDataException("缺少 logicFile");
        if (!IsSafeRelativePath(loadedLogicFile))
        {
            throw new InvalidDataException("logicFile 不是安全相对路径");
        }
        var logicPath = Path.Combine(directory, loadedLogicFile.Replace('/', Path.DirectorySeparatorChar));
        var loadedPorts = root.GetProperty("ports").EnumerateArray().Select(port => new ModulePortDraft(
            port.GetProperty("id").GetString() ?? string.Empty,
            port.GetProperty("direction").GetString() ?? string.Empty,
            port.GetProperty("maxChannels").GetInt32())).ToArray();
        var loadedParameters = root.TryGetProperty("parameters", out var parametersElement)
            ? parametersElement.EnumerateArray().Select(parameter => new ModuleParameterDraft(
                parameter.GetProperty("id").GetString() ?? string.Empty,
                ReadOptionalString(parameter, "displayName"),
                ReadOptionalString(parameter, "unit"),
                parameter.GetProperty("minimum").GetDouble(),
                parameter.GetProperty("maximum").GetDouble(),
                parameter.GetProperty("default").GetDouble(),
                ReadOptionalString(parameter, "control", "knob"))).ToArray()
            : [];
        var loadedLogicSource = projectKind is ModuleAuthoringKind.DigitalAlgorithm or ModuleAuthoringKind.AnalogCircuit
            ? File.ReadAllText(logicPath)
            : string.Empty;
        var loadedAlgorithmId = string.Empty;
        IReadOnlyList<DigitalOperationDraft> loadedOperations = [];
        if (projectKind == ModuleAuthoringKind.DigitalAlgorithm)
        {
            using var logicDocument = JsonDocument.Parse(loadedLogicSource);
            var logicRoot = logicDocument.RootElement;
            loadedAlgorithmId = logicRoot.GetProperty("algorithm").GetString() ?? string.Empty;
            loadedOperations = logicRoot.GetProperty("operations").EnumerateArray().Select(operation =>
                new DigitalOperationDraft(
                    operation.GetProperty("type").GetString() ?? string.Empty,
                    ReadOptionalString(operation, "parameter"))).ToArray();
        }
        var loadedAnalog = new AnalogCircuitDraft();
        var loadedAnalogStructuredMode = projectKind == ModuleAuthoringKind.AnalogCircuit &&
                                         TryReadStructuredAnalog(loadedLogicSource, out loadedAnalog);
        if (loadedAnalogStructuredMode)
        {
            ReadWorkshopMetadata(directory, loadedAnalog);
        }
        var loadedSkinFile = root.TryGetProperty("skinFile", out var skin) && skin.ValueKind == JsonValueKind.String
            ? skin.GetString()
            : null;
        var loadedSkin = !string.IsNullOrWhiteSpace(loadedSkinFile)
            ? ReadSkin(Path.Combine(directory, loadedSkinFile!.Replace('/', Path.DirectorySeparatorChar)))
            : new PedalSkinDraft();
        return new LoadedState(
            projectKind,
            root.GetProperty("moduleId").GetString() ?? string.Empty,
            root.GetProperty("displayName").GetString() ?? string.Empty,
            ReadOptionalString(root, "packageVersion", "1.0.0"),
            ReadOptionalString(root, "author"),
            ReadOptionalString(root, "description"),
            root.TryGetProperty("declaredLatencySamples", out var latency) ? latency.GetUInt32() : 0,
            loadedLogicFile.Replace('\\', '/'),
            loadedSkinFile,
            loadedLogicSource,
            projectKind is ModuleAuthoringKind.NeuralAmpModel or ModuleAuthoringKind.ImpulseResponse ? logicPath : null,
            loadedPorts,
            loadedParameters,
            loadedAlgorithmId,
            loadedOperations,
            !string.IsNullOrWhiteSpace(loadedSkinFile),
            loadedSkin,
            loadedAnalogStructuredMode,
            loadedAnalog);
    }

    private static string ReadOptionalString(JsonElement element, string name, string fallback = "") =>
        element.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString() ?? fallback
            : fallback;

    private static void ValidateJsonProperties(
        Action<string> addError,
        IEnumerable<AnalogComponentPropertyDraft> properties,
        string owner)
    {
        var values = properties.ToArray();
        foreach (var property in values)
        {
            if (string.IsNullOrWhiteSpace(property.Id))
            {
                addError($"{owner}包含空属性名");
                continue;
            }
            try
            {
                _ = JsonNode.Parse(property.JsonValue);
            }
            catch (JsonException exception)
            {
                addError($"{owner}的属性 {property.Id} 不是有效 JSON：{exception.Message}");
            }
        }
        foreach (var duplicate in values.Where(property => !string.IsNullOrWhiteSpace(property.Id))
                     .GroupBy(property => property.Id.Trim(), StringComparer.Ordinal).Where(group => group.Count() > 1))
        {
            addError($"{owner}的属性重复：{duplicate.Key}");
        }
    }

    private static string DefaultLogicFile(ModuleAuthoringKind projectKind) => projectKind switch
    {
        ModuleAuthoringKind.DigitalAlgorithm => "logic/algorithm.json",
        ModuleAuthoringKind.AnalogCircuit => "logic/circuit.json",
        ModuleAuthoringKind.NeuralAmpModel => "logic/model.nam",
        ModuleAuthoringKind.ImpulseResponse => "logic/impulse.wav",
        _ => throw new ArgumentOutOfRangeException(nameof(projectKind)),
    };

    private static string DefaultLogicSource(ModuleAuthoringKind projectKind) => projectKind switch
    {
        ModuleAuthoringKind.DigitalAlgorithm => """
            {
              "schemaVersion": 1,
              "kind": "digital-algorithm",
              "algorithm": "solorig.gain",
              "operations": [
                { "type": "gain", "parameter": "gain" }
              ]
            }
            """,
        ModuleAuthoringKind.AnalogCircuit => """
            {
              "version": 2,
              "schema": "circuit",
              "referenceGround": "0",
              "nodes": ["in", "out", "0"],
              "ports": [
                { "id": "input", "direction": "input", "node": "in" },
                { "id": "output", "direction": "output", "node": "out" }
              ],
              "supplies": [],
              "components": [
                { "id": "series", "type": "resistor", "pins": ["in", "out"], "value": 1000 },
                { "id": "load", "type": "resistor", "pins": ["out", "0"], "value": 1000 }
              ],
              "solver": {
                "method": "modified-nodal-analysis",
                "integration": "backward-euler",
                "oversampling": 1,
                "convergence": {
                  "maxIterations": 16,
                  "absoluteTolerance": 1e-9,
                  "relativeTolerance": 1e-6
                }
              }
            }
            """,
        _ => string.Empty,
    };

    private static IReadOnlyList<ModuleParameterDraft> DefaultParameters(ModuleAuthoringKind projectKind) =>
        projectKind == ModuleAuthoringKind.DigitalAlgorithm
            ? [new ModuleParameterDraft("gain", "Gain", "x", 0.0, 2.0, 1.0)]
            : [];

    private string BuildDigitalLogicSource()
    {
        var logic = new
        {
            schemaVersion = 1,
            kind = "digital-algorithm",
            algorithm = AlgorithmId.Trim(),
            operations = DigitalOperations.Select(operation => new
            {
                type = operation.Type,
                parameter = string.IsNullOrWhiteSpace(operation.ParameterId) ? null : operation.ParameterId.Trim(),
            }),
        };
        return JsonSerializer.Serialize(logic, JsonOptions);
    }

    private string BuildAnalogCircuitSource()
    {
        var nodes = AnalogCircuit.Nodes
            .Concat(AnalogCircuit.Components.SelectMany(component => component.Pins.Select(pin => pin.Node)))
            .Concat(AnalogCircuit.Ports.Select(port => port.Node))
            .Concat(AnalogCircuit.Supplies.SelectMany(supply => new[] { supply.PositiveNode, supply.NegativeNode }))
            .Append(AnalogCircuit.ReferenceGround)
            .Where(node => !string.IsNullOrWhiteSpace(node))
            .Select(node => node.Trim())
            .Distinct(StringComparer.Ordinal)
            .ToArray();
        var root = new JsonObject();
        foreach (var property in AnalogCircuit.RootProperties)
        {
            root[property.Id.Trim()] = ParseDraftJson(property.JsonValue);
        }
        root["version"] = 2;
        root["schema"] = "circuit";
        root["referenceGround"] = AnalogCircuit.ReferenceGround.Trim();
        root["nodes"] = JsonSerializer.SerializeToNode(nodes);
        root["ports"] = new JsonArray(AnalogCircuit.Ports.Select(port =>
            (JsonNode)new JsonObject
            {
                ["id"] = port.Id.Trim(),
                ["direction"] = port.Direction,
                ["node"] = port.Node.Trim(),
            }).ToArray());
        root["supplies"] = new JsonArray(AnalogCircuit.Supplies.Select(supply =>
        {
            var value = new JsonObject
            {
                ["id"] = supply.Id.Trim(),
                ["positive"] = supply.PositiveNode.Trim(),
                ["negative"] = supply.NegativeNode.Trim(),
                ["voltage"] = supply.Voltage,
            };
            foreach (var property in supply.Properties)
            {
                value[property.Id.Trim()] = ParseDraftJson(property.JsonValue);
            }
            return (JsonNode)value;
        }).ToArray());
        root["components"] = new JsonArray(AnalogCircuit.Components.Select(component =>
        {
            var value = new JsonObject
            {
                ["id"] = component.Id.Trim(),
                ["type"] = component.Descriptor.RuntimeType,
                ["pins"] = JsonSerializer.SerializeToNode(component.Pins.Select(pin => pin.Node.Trim()).ToArray()),
            };
            var modelReference = component.Properties.FirstOrDefault(property => property.Id == "model")?.TextValue.Trim();
            foreach (var property in component.Properties)
            {
                if (property.Id == "model" && string.IsNullOrWhiteSpace(modelReference))
                {
                    continue;
                }
                if (property.Id == "diodeModel")
                {
                    if (component.Type == "diode" && string.IsNullOrWhiteSpace(modelReference))
                    {
                        value["model"] = ParseDraftJson(property.JsonValue);
                    }
                    continue;
                }
                if (!string.IsNullOrWhiteSpace(modelReference) && IsInlineModelProperty(property.Id))
                {
                    continue;
                }
                value[property.Id.Trim()] = ParseDraftJson(property.JsonValue);
            }
            return (JsonNode)value;
        }).ToArray());
        if (AnalogCircuit.ParameterBindings.Count > 0)
        {
            root["parameterBindings"] = new JsonArray(AnalogCircuit.ParameterBindings.Select(binding =>
                (JsonNode)new JsonObject
                {
                    ["modelVersion"] = binding.ModelVersion,
                    ["parameter"] = binding.ParameterId.Trim(),
                    ["target"] = binding.TargetId.Trim(),
                    ["property"] = binding.Property,
                    ["smoothingSeconds"] = binding.SmoothingSeconds,
                }).ToArray());
        }
        else
        {
            root.Remove("parameterBindings");
        }
        var convergence = new JsonObject();
        foreach (var property in AnalogCircuit.ConvergenceProperties)
        {
            convergence[property.Id.Trim()] = ParseDraftJson(property.JsonValue);
        }
        convergence["maxIterations"] = AnalogCircuit.MaxIterations;
        convergence["absoluteTolerance"] = AnalogCircuit.AbsoluteTolerance;
        convergence["relativeTolerance"] = AnalogCircuit.RelativeTolerance;
        var solver = new JsonObject();
        foreach (var property in AnalogCircuit.SolverProperties)
        {
            solver[property.Id.Trim()] = ParseDraftJson(property.JsonValue);
        }
        solver["method"] = "modified-nodal-analysis";
        solver["integration"] = "backward-euler";
        solver["oversampling"] = AnalogCircuit.Oversampling;
        solver["convergence"] = convergence;
        root["solver"] = solver;
        return root.ToJsonString(JsonOptions);
    }

    private static bool IsInlineModelProperty(string propertyId) => propertyId is
        "amplifierModel" or "bjtModel" or "jfetModel" or "mosfetModel" or "triodeModel" or
        "pentodeModel" or "beamTetrodeModel" or "variMuModel" or "coupledInductorModel" or
        "speakerModel";

    private static JsonNode? ParseDraftJson(string source)
    {
        var node = JsonNode.Parse(source);
        if (node is null && !string.Equals(source.Trim(), "null", StringComparison.Ordinal))
        {
            throw new JsonException("JSON 属性不能为空");
        }
        return node;
    }

    private static AnalogCircuitDraft DefaultAnalogCircuit()
    {
        var circuit = new AnalogCircuitDraft();
        circuit.Components.Add(new AnalogComponentDraft("series", "resistor", "in", "out", 1000.0)
            { CanvasX = 80.0, CanvasY = 120.0 });
        circuit.Components.Add(new AnalogComponentDraft("load", "resistor", "out", "0", 1000.0)
            { CanvasX = 360.0, CanvasY = 120.0 });
        return circuit;
    }

    private static bool TryReadStructuredAnalog(string source, out AnalogCircuitDraft circuit)
    {
        circuit = new AnalogCircuitDraft();
        try
        {
            using var document = JsonDocument.Parse(source);
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object || root.GetProperty("version").GetInt32() != 2 ||
                root.GetProperty("schema").GetString() != "circuit")
            {
                return false;
            }
            circuit.ReferenceGround = root.GetProperty("referenceGround").GetString() ?? string.Empty;
            circuit.Nodes.Clear();
            foreach (var node in root.GetProperty("nodes").EnumerateArray())
            {
                circuit.Nodes.Add(node.GetString() ?? string.Empty);
            }
            circuit.Ports.Clear();
            foreach (var port in root.GetProperty("ports").EnumerateArray())
            {
                circuit.Ports.Add(new AnalogCircuitPortDraft(
                    port.GetProperty("id").GetString() ?? string.Empty,
                    port.GetProperty("direction").GetString() ?? string.Empty,
                    port.GetProperty("node").GetString() ?? string.Empty));
            }
            circuit.Supplies.Clear();
            if (root.TryGetProperty("supplies", out var supplies))
            {
                foreach (var supply in supplies.EnumerateArray())
                {
                    var draft = new AnalogSupplyDraft(
                        supply.GetProperty("id").GetString() ?? string.Empty,
                        supply.GetProperty("positive").GetString() ?? string.Empty,
                        supply.GetProperty("negative").GetString() ?? string.Empty,
                        supply.GetProperty("voltage").GetDouble());
                    foreach (var property in supply.EnumerateObject().Where(property =>
                                 property.Name is not ("id" or "positive" or "negative" or "voltage")))
                    {
                        draft.Properties.Add(new AnalogComponentPropertyDraft(
                            property.Name, property.Value.GetRawText()));
                    }
                    circuit.Supplies.Add(draft);
                }
            }
            circuit.ParameterBindings.Clear();
            if (root.TryGetProperty("parameterBindings", out var parameterBindings))
            {
                foreach (var binding in parameterBindings.EnumerateArray())
                {
                    circuit.ParameterBindings.Add(new AnalogParameterBindingDraft(
                        binding.GetProperty("modelVersion").GetInt32(),
                        binding.GetProperty("parameter").GetString() ?? string.Empty,
                        binding.GetProperty("target").GetString() ?? string.Empty,
                        binding.GetProperty("property").GetString() ?? string.Empty,
                        binding.GetProperty("smoothingSeconds").GetDouble()));
                }
            }
            var solver = root.GetProperty("solver");
            if (solver.GetProperty("method").GetString() != "modified-nodal-analysis" ||
                solver.GetProperty("integration").GetString() != "backward-euler")
            {
                return false;
            }
            circuit.Oversampling = solver.TryGetProperty("oversampling", out var oversampling)
                ? oversampling.GetInt32()
                : 1;
            circuit.SolverProperties.Clear();
            foreach (var property in solver.EnumerateObject().Where(property =>
                         property.Name is not ("method" or "integration" or "oversampling" or "convergence")))
            {
                circuit.SolverProperties.Add(new AnalogComponentPropertyDraft(
                    property.Name, property.Value.GetRawText()));
            }
            circuit.ConvergenceProperties.Clear();
            if (solver.TryGetProperty("convergence", out var convergence))
            {
                circuit.MaxIterations = convergence.GetProperty("maxIterations").GetInt32();
                circuit.AbsoluteTolerance = convergence.GetProperty("absoluteTolerance").GetDouble();
                circuit.RelativeTolerance = convergence.GetProperty("relativeTolerance").GetDouble();
                foreach (var property in convergence.EnumerateObject().Where(property =>
                             property.Name is not ("maxIterations" or "absoluteTolerance" or "relativeTolerance")))
                {
                    circuit.ConvergenceProperties.Add(new AnalogComponentPropertyDraft(
                        property.Name, property.Value.GetRawText()));
                }
            }
            circuit.RootProperties.Clear();
            var modeledRootProperties = new HashSet<string>(
                ["version", "schema", "referenceGround", "nodes", "ports", "supplies", "components",
                 "parameterBindings", "solver"], StringComparer.Ordinal);
            foreach (var property in root.EnumerateObject().Where(property => !modeledRootProperties.Contains(property.Name)))
            {
                circuit.RootProperties.Add(new AnalogComponentPropertyDraft(
                    property.Name, property.Value.GetRawText()));
            }
            foreach (var component in root.GetProperty("components").EnumerateArray())
            {
                var runtimeType = component.GetProperty("type").GetString() ?? string.Empty;
                var type = ResolveAuthoringComponentType(runtimeType, component);
                if (!AnalogComponentCatalog.TryGet(type, out var descriptor))
                {
                    return false;
                }
                var pins = component.GetProperty("pins").EnumerateArray().Select(pin => pin.GetString() ?? string.Empty).ToArray();
                if (pins.Length != descriptor.Pins.Count && runtimeType != "relay")
                {
                    return false;
                }
                var pinDrafts = pins.Select((pin, index) =>
                {
                    var pinDescriptor = index < descriptor.Pins.Count
                        ? descriptor.Pins[index]
                        : new AnalogPinDescriptor($"pin-{index + 1}", $"引脚 {index + 1}",
                            0.0, 0.0, "left", "passive");
                    return new AnalogPinDraft(pinDescriptor.Id, pinDescriptor.DisplayName, pin,
                        pinDescriptor.AnchorX, pinDescriptor.AnchorY, pinDescriptor.Side, pinDescriptor.ElectricalRole);
                }).ToArray();
                var properties = component.EnumerateObject()
                    .Where(property => property.Name is not ("id" or "type" or "pins"))
                    .Select(property => new AnalogComponentPropertyDraft(
                        runtimeType == "diode" && property.Name == "model" && property.Value.ValueKind == JsonValueKind.Object
                            ? "diodeModel"
                            : property.Name,
                        property.Value.GetRawText()))
                    .ToList();
                foreach (var property in descriptor.Properties.Where(property =>
                             properties.All(candidate => !string.Equals(candidate.Id, property.Id, StringComparison.Ordinal))))
                {
                    properties.Add(new AnalogComponentPropertyDraft(
                        property.Id, AnalogComponentCatalog.DefaultJson(property)));
                }
                circuit.Components.Add(new AnalogComponentDraft(
                    component.GetProperty("id").GetString() ?? string.Empty,
                    type,
                    pinDrafts,
                    properties));
            }
            return true;
        }
        catch (Exception exception) when (exception is JsonException or InvalidOperationException or KeyNotFoundException)
        {
            circuit = new AnalogCircuitDraft();
            return false;
        }
    }

    private static string ResolveAuthoringComponentType(string runtimeType, JsonElement component)
    {
        var modelText = component.GetRawText().ToLowerInvariant();
        return runtimeType switch
        {
            "bjt" => modelText.Contains("pnp", StringComparison.Ordinal) ? "bjt-pnp" : "bjt-npn",
            "jfet" => modelText.Contains("p-channel", StringComparison.Ordinal) ||
                      modelText.Contains("dynamic.p", StringComparison.Ordinal) ? "jfet-p" : "jfet-n",
            "mosfet" => modelText.Contains("p-channel", StringComparison.Ordinal) ||
                        modelText.Contains("dynamic.p", StringComparison.Ordinal) ? "mosfet-p" : "mosfet-n",
            _ => runtimeType,
        };
    }

    private string BuildSkinSource()
    {
        static object Control(string id, string image) => string.IsNullOrWhiteSpace(image)
            ? id.Trim()
            : new { id = id.Trim(), image = image.Trim().Replace('\\', '/') };
        var skin = new
        {
            schemaVersion = 2,
            id = Skin.Id.Trim(),
            controls = new
            {
                knob = Control(Skin.KnobId, Skin.KnobImage),
                toggle = Control(Skin.ToggleId, Skin.ToggleImage),
                button = Control(Skin.ButtonId, Skin.ButtonImage),
                footswitch = Control(Skin.FootswitchId, Skin.FootswitchImage),
            },
            layout = new
            {
                width = Skin.Width,
                height = Skin.Height,
                background = string.IsNullOrWhiteSpace(Skin.Background) ? null : Skin.Background.Trim(),
                labelColor = string.IsNullOrWhiteSpace(Skin.LabelColor) ? null : Skin.LabelColor.Trim(),
                parameters = Skin.ParameterPlacements.Select(placement => new
                {
                    id = placement.ParameterId.Trim(),
                    x = placement.X,
                    y = placement.Y,
                }),
                elements = Skin.Elements.Select(element => new
                {
                    id = element.Id.Trim(),
                    type = element.Type.Trim(),
                    binding = string.IsNullOrWhiteSpace(element.Binding) ? null : element.Binding.Trim(),
                    x = element.X,
                    y = element.Y,
                    width = element.Width,
                    height = element.Height,
                    rotation = element.Rotation,
                    detents = element.Detents,
                    zIndex = element.ZIndex,
                    label = string.IsNullOrWhiteSpace(element.Label) ? null : element.Label,
                    style = string.IsNullOrWhiteSpace(element.Style) ? null : element.Style,
                }),
                footswitch = Skin.UseCustomFootswitchPosition
                    ? new { x = Skin.FootswitchX, y = Skin.FootswitchY }
                    : null,
            },
        };
        return JsonSerializer.Serialize(skin, JsonOptions);
    }

    private string BuildWorkshopMetadataSource()
    {
        var metadata = new
        {
            schemaVersion = 2,
            analogCircuit = new
            {
                components = AnalogCircuit.Components.Select(component => new
                {
                    id = component.Id.Trim(),
                    x = component.CanvasX,
                    y = component.CanvasY,
                }),
                routes = AnalogCircuit.WireRoutes.Select(route => new
                {
                    node = route.Node.Trim(),
                    points = route.Points.Select(point => new { x = point.X, y = point.Y }),
                }),
            },
        };
        return JsonSerializer.Serialize(metadata, JsonOptions);
    }

    private static void ReadWorkshopMetadata(string directory, AnalogCircuitDraft circuit)
    {
        var path = Path.Combine(directory, ".solorig", "workshop.json");
        if (!File.Exists(path))
        {
            return;
        }
        using var document = JsonDocument.Parse(File.ReadAllText(path));
        var root = document.RootElement;
        var schemaVersion = root.GetProperty("schemaVersion").GetInt32();
        if (schemaVersion is not (1 or 2) ||
            !root.TryGetProperty("analogCircuit", out var analogCircuit) ||
            !analogCircuit.TryGetProperty("components", out var components))
        {
            return;
        }
        foreach (var layout in components.EnumerateArray())
        {
            var id = ReadOptionalString(layout, "id");
            var component = circuit.Components.FirstOrDefault(candidate =>
                string.Equals(candidate.Id, id, StringComparison.Ordinal));
            if (component is null)
            {
                continue;
            }
            component.CanvasX = layout.TryGetProperty("x", out var x) ? x.GetDouble() : 0.0;
            component.CanvasY = layout.TryGetProperty("y", out var y) ? y.GetDouble() : 0.0;
        }
        if (schemaVersion >= 2 && analogCircuit.TryGetProperty("routes", out var routes) &&
            routes.ValueKind == JsonValueKind.Array)
        {
            foreach (var routeValue in routes.EnumerateArray())
            {
                var route = new AnalogWireRouteDraft(ReadOptionalString(routeValue, "node"));
                if (routeValue.TryGetProperty("points", out var points) && points.ValueKind == JsonValueKind.Array)
                {
                    foreach (var point in points.EnumerateArray())
                    {
                        route.Points.Add(new AnalogWirePointDraft(
                            point.GetProperty("x").GetDouble(), point.GetProperty("y").GetDouble()));
                    }
                }
                circuit.WireRoutes.Add(route);
            }
        }
    }

    private static PedalSkinDraft ReadSkin(string path)
    {
        using var document = JsonDocument.Parse(File.ReadAllText(path));
        var root = document.RootElement;
        var schemaVersion = root.GetProperty("schemaVersion").GetUInt32();
        if (schemaVersion is not (1 or 2))
        {
            throw new InvalidDataException("只支持 schemaVersion 1 或 2 外观文件");
        }
        var controls = root.GetProperty("controls");
        var (knobId, knobImage) = ReadSkinControl(controls.GetProperty("knob"));
        var (toggleId, toggleImage) = ReadSkinControl(controls.GetProperty("toggle"));
        var (buttonId, buttonImage) = ReadSkinControl(controls.GetProperty("button"));
        var (footswitchId, footswitchImage) = ReadSkinControl(controls.GetProperty("footswitch"));
        var result = new PedalSkinDraft
        {
            Id = root.GetProperty("id").GetString() ?? string.Empty,
            KnobId = knobId,
            ToggleId = toggleId,
            ButtonId = buttonId,
            FootswitchId = footswitchId,
            KnobImage = knobImage,
            ToggleImage = toggleImage,
            ButtonImage = buttonImage,
            FootswitchImage = footswitchImage,
        };
        if (root.TryGetProperty("layout", out var layout))
        {
            if (layout.ValueKind != JsonValueKind.Object)
            {
                throw new InvalidDataException("外观 layout 必须是对象");
            }
            result.Width = layout.TryGetProperty("width", out var width) ? width.GetDouble() : result.Width;
            result.Height = layout.TryGetProperty("height", out var height) ? height.GetDouble() : result.Height;
            result.Background = ReadOptionalString(layout, "background", result.Background);
            result.LabelColor = ReadOptionalString(layout, "labelColor", result.LabelColor);
            if (layout.TryGetProperty("parameters", out var parameters))
            {
                if (parameters.ValueKind != JsonValueKind.Array)
                {
                    throw new InvalidDataException("外观参数坐标必须是数组");
                }
                foreach (var placement in parameters.EnumerateArray())
                {
                    result.ParameterPlacements.Add(new PedalSkinParameterPlacementDraft(
                        placement.GetProperty("id").GetString() ?? string.Empty,
                        placement.GetProperty("x").GetDouble(),
                        placement.GetProperty("y").GetDouble()));
                }
            }
            if (schemaVersion >= 2 && layout.TryGetProperty("elements", out var elements))
            {
                if (elements.ValueKind != JsonValueKind.Array)
                {
                    throw new InvalidDataException("外观元素必须是数组");
                }
                result.Elements.Clear();
                foreach (var element in elements.EnumerateArray())
                {
                    var id = ReadOptionalString(element, "id");
                    var type = ReadOptionalString(element, "type");
                    if (string.IsNullOrWhiteSpace(id) || !PedalControlCatalog.Contains(type))
                    {
                        throw new InvalidDataException("外观元素必须包含已知的唯一 id 和 type");
                    }
                    var descriptor = PedalControlCatalog.Get(type);
                    result.Elements.Add(new PedalSkinElementDraft(
                        id,
                        type,
                        ReadOptionalString(element, "binding"),
                        element.TryGetProperty("x", out var x) ? x.GetDouble() : 0.0,
                        element.TryGetProperty("y", out var y) ? y.GetDouble() : 0.0)
                    {
                        Width = element.TryGetProperty("width", out var elementWidth) ? elementWidth.GetDouble() : descriptor.DefaultWidth,
                        Height = element.TryGetProperty("height", out var elementHeight) ? elementHeight.GetDouble() : descriptor.DefaultHeight,
                        Rotation = element.TryGetProperty("rotation", out var rotation) ? rotation.GetDouble() : 0.0,
                        Detents = element.TryGetProperty("detents", out var detents) ? detents.GetInt32() : descriptor.DefaultDetents,
                        ZIndex = element.TryGetProperty("zIndex", out var zIndex) ? zIndex.GetInt32() : 0,
                        Label = ReadOptionalString(element, "label"),
                        Style = ReadOptionalString(element, "style", "{}"),
                    });
                }
            }
            if (layout.TryGetProperty("footswitch", out var footswitch))
            {
                if (footswitch.ValueKind != JsonValueKind.Object)
                {
                    throw new InvalidDataException("外观踩钉坐标必须是对象");
                }
                result.UseCustomFootswitchPosition = true;
                result.FootswitchX = footswitch.GetProperty("x").GetDouble();
                result.FootswitchY = footswitch.GetProperty("y").GetDouble();
            }
        }
        if (schemaVersion == 1)
        {
            foreach (var placement in result.ParameterPlacements)
            {
                var type = placement.ControlKind switch
                {
                    "toggle" => "switch.toggle",
                    "button" => "button.momentary",
                    "footswitch" => "footswitch",
                    _ => "knob.continuous",
                };
                result.Elements.Add(new PedalSkinElementDraft(
                    $"parameter-{placement.ParameterId}", type, placement.ParameterId, placement.X, placement.Y));
            }
        }
        return result;
    }

    private static (string Id, string Image) ReadSkinControl(JsonElement element)
    {
        if (element.ValueKind == JsonValueKind.String)
        {
            return (element.GetString() ?? string.Empty, string.Empty);
        }
        if (element.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidDataException("外观控件必须是字符串或对象");
        }
        return (
            element.GetProperty("id").GetString() ?? string.Empty,
            ReadOptionalString(element, "image"));
    }

    private void ApplySkin(PedalSkinDraft source)
    {
        Skin.Id = source.Id;
        Skin.KnobId = source.KnobId;
        Skin.ToggleId = source.ToggleId;
        Skin.ButtonId = source.ButtonId;
        Skin.FootswitchId = source.FootswitchId;
        Skin.KnobImage = source.KnobImage;
        Skin.ToggleImage = source.ToggleImage;
        Skin.ButtonImage = source.ButtonImage;
        Skin.FootswitchImage = source.FootswitchImage;
        Skin.Width = source.Width;
        Skin.Height = source.Height;
        Skin.Background = source.Background;
        Skin.LabelColor = source.LabelColor;
        Skin.UseCustomFootswitchPosition = source.UseCustomFootswitchPosition;
        Skin.FootswitchX = source.FootswitchX;
        Skin.FootswitchY = source.FootswitchY;
        Skin.ParameterPlacements.Clear();
        foreach (var placement in source.ParameterPlacements)
        {
            Skin.ParameterPlacements.Add(new PedalSkinParameterPlacementDraft(
                placement.ParameterId, placement.X, placement.Y));
        }
        Skin.Elements.Clear();
        foreach (var element in source.Elements)
        {
            Skin.Elements.Add(new PedalSkinElementDraft(element.Id, element.Type, element.Binding, element.X, element.Y)
            {
                Width = element.Width,
                Height = element.Height,
                Rotation = element.Rotation,
                Detents = element.Detents,
                ZIndex = element.ZIndex,
                Label = element.Label,
                Style = element.Style,
            });
        }
    }

    private void ApplyAnalogCircuit(AnalogCircuitDraft source)
    {
        AnalogCircuit.ReferenceGround = source.ReferenceGround;
        AnalogCircuit.Oversampling = source.Oversampling;
        AnalogCircuit.MaxIterations = source.MaxIterations;
        AnalogCircuit.AbsoluteTolerance = source.AbsoluteTolerance;
        AnalogCircuit.RelativeTolerance = source.RelativeTolerance;
        AnalogCircuit.Nodes.Clear();
        foreach (var node in source.Nodes)
        {
            AnalogCircuit.Nodes.Add(node);
        }
        AnalogCircuit.Ports.Clear();
        foreach (var port in source.Ports)
        {
            AnalogCircuit.Ports.Add(new AnalogCircuitPortDraft(port.Id, port.Direction, port.Node));
        }
        AnalogCircuit.InputNode = source.InputNode;
        AnalogCircuit.OutputNode = source.OutputNode;
        AnalogCircuit.Supplies.Clear();
        foreach (var supply in source.Supplies)
        {
            var cloned = new AnalogSupplyDraft(supply.Id, supply.PositiveNode, supply.NegativeNode, supply.Voltage);
            foreach (var property in supply.Properties)
            {
                cloned.Properties.Add(new AnalogComponentPropertyDraft(property.Id, property.JsonValue));
            }
            AnalogCircuit.Supplies.Add(cloned);
        }
        AnalogCircuit.ParameterBindings.Clear();
        foreach (var binding in source.ParameterBindings)
        {
            AnalogCircuit.ParameterBindings.Add(new AnalogParameterBindingDraft(
                binding.ModelVersion, binding.ParameterId, binding.TargetId, binding.Property,
                binding.SmoothingSeconds));
        }
        AnalogCircuit.Components.Clear();
        foreach (var component in source.Components)
        {
            AnalogCircuit.Components.Add(new AnalogComponentDraft(
                component.Id,
                component.Type,
                component.Pins.Select(pin => new AnalogPinDraft(pin.Id, pin.DisplayName, pin.Node)),
                component.Properties.Select(property => new AnalogComponentPropertyDraft(property.Id, property.JsonValue)),
                component.CanvasX,
                component.CanvasY));
        }
        AnalogCircuit.WireRoutes.Clear();
        foreach (var route in source.WireRoutes)
        {
            var cloned = new AnalogWireRouteDraft(route.Node);
            foreach (var point in route.Points)
            {
                cloned.Points.Add(new AnalogWirePointDraft(point.X, point.Y));
            }
            AnalogCircuit.WireRoutes.Add(cloned);
        }
        ReplaceAnalogProperties(AnalogCircuit.RootProperties, source.RootProperties);
        ReplaceAnalogProperties(AnalogCircuit.SolverProperties, source.SolverProperties);
        ReplaceAnalogProperties(AnalogCircuit.ConvergenceProperties, source.ConvergenceProperties);
    }

    private static void ReplaceAnalogProperties(
        ObservableCollection<AnalogComponentPropertyDraft> target,
        IEnumerable<AnalogComponentPropertyDraft> source)
    {
        target.Clear();
        foreach (var property in source)
        {
            target.Add(new AnalogComponentPropertyDraft(property.Id, property.JsonValue));
        }
    }

    private void DraftCollectionChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        if (args.OldItems is not null)
        {
            foreach (ModuleAuthoringDraft item in args.OldItems)
            {
                item.Changed -= DraftChanged;
            }
        }
        if (args.NewItems is not null)
        {
            foreach (ModuleAuthoringDraft item in args.NewItems)
            {
                item.Changed += DraftChanged;
            }
        }
        DraftChanged();
    }

    private HistoryEntry CaptureHistory()
    {
        var skin = new PedalSkinDraft
        {
            Id = Skin.Id,
            KnobId = Skin.KnobId,
            ToggleId = Skin.ToggleId,
            ButtonId = Skin.ButtonId,
            FootswitchId = Skin.FootswitchId,
            KnobImage = Skin.KnobImage,
            ToggleImage = Skin.ToggleImage,
            ButtonImage = Skin.ButtonImage,
            FootswitchImage = Skin.FootswitchImage,
            Width = Skin.Width,
            Height = Skin.Height,
            Background = Skin.Background,
            LabelColor = Skin.LabelColor,
            UseCustomFootswitchPosition = Skin.UseCustomFootswitchPosition,
            FootswitchX = Skin.FootswitchX,
            FootswitchY = Skin.FootswitchY,
        };
        foreach (var placement in Skin.ParameterPlacements)
        {
            skin.ParameterPlacements.Add(new PedalSkinParameterPlacementDraft(
                placement.ParameterId, placement.X, placement.Y));
        }
        skin.Elements.Clear();
        foreach (var element in Skin.Elements)
        {
            skin.Elements.Add(new PedalSkinElementDraft(element.Id, element.Type, element.Binding, element.X, element.Y)
            {
                Width = element.Width,
                Height = element.Height,
                Rotation = element.Rotation,
                Detents = element.Detents,
                ZIndex = element.ZIndex,
                Label = element.Label,
                Style = element.Style,
            });
        }

        var circuit = new AnalogCircuitDraft
        {
            ReferenceGround = AnalogCircuit.ReferenceGround,
            InputNode = AnalogCircuit.InputNode,
            OutputNode = AnalogCircuit.OutputNode,
            Oversampling = AnalogCircuit.Oversampling,
            MaxIterations = AnalogCircuit.MaxIterations,
            AbsoluteTolerance = AnalogCircuit.AbsoluteTolerance,
            RelativeTolerance = AnalogCircuit.RelativeTolerance,
        };
        circuit.Nodes.Clear();
        foreach (var node in AnalogCircuit.Nodes)
        {
            circuit.Nodes.Add(node);
        }
        circuit.Ports.Clear();
        foreach (var port in AnalogCircuit.Ports)
        {
            circuit.Ports.Add(new AnalogCircuitPortDraft(port.Id, port.Direction, port.Node));
        }
        foreach (var supply in AnalogCircuit.Supplies)
        {
            var cloned = new AnalogSupplyDraft(supply.Id, supply.PositiveNode, supply.NegativeNode, supply.Voltage);
            foreach (var property in supply.Properties)
            {
                cloned.Properties.Add(new AnalogComponentPropertyDraft(property.Id, property.JsonValue));
            }
            circuit.Supplies.Add(cloned);
        }
        foreach (var binding in AnalogCircuit.ParameterBindings)
        {
            circuit.ParameterBindings.Add(new AnalogParameterBindingDraft(
                binding.ModelVersion, binding.ParameterId, binding.TargetId, binding.Property,
                binding.SmoothingSeconds));
        }
        foreach (var component in AnalogCircuit.Components)
        {
            circuit.Components.Add(new AnalogComponentDraft(
                component.Id,
                component.Type,
                component.Pins.Select(pin => new AnalogPinDraft(pin.Id, pin.DisplayName, pin.Node)),
                component.Properties.Select(property => new AnalogComponentPropertyDraft(property.Id, property.JsonValue)),
                component.CanvasX,
                component.CanvasY));
        }
        foreach (var route in AnalogCircuit.WireRoutes)
        {
            var cloned = new AnalogWireRouteDraft(route.Node);
            foreach (var point in route.Points)
            {
                cloned.Points.Add(new AnalogWirePointDraft(point.X, point.Y));
            }
            circuit.WireRoutes.Add(cloned);
        }
        ReplaceAnalogProperties(circuit.RootProperties, AnalogCircuit.RootProperties);
        ReplaceAnalogProperties(circuit.SolverProperties, AnalogCircuit.SolverProperties);
        ReplaceAnalogProperties(circuit.ConvergenceProperties, AnalogCircuit.ConvergenceProperties);

        var state = new LoadedState(kind, ModuleId, DisplayName, PackageVersion, Author, Description,
            DeclaredLatencySamples, LogicFile, SkinFile, LogicSource, SourceAssetPath,
            Ports.Select(port => new ModulePortDraft(port.Id, port.Direction, port.MaxChannels)).ToArray(),
            Parameters.Select(parameter => new ModuleParameterDraft(parameter.Id, parameter.DisplayName,
                parameter.Unit, parameter.Minimum, parameter.Maximum, parameter.DefaultValue,
                parameter.ControlKind)).ToArray(),
            AlgorithmId,
            DigitalOperations.Select(operation => new DigitalOperationDraft(operation.Type,
                operation.ParameterId)).ToArray(),
            CustomSkinEnabled, skin, AnalogStructuredMode, circuit);
        return new HistoryEntry(state, JsonSerializer.Serialize(state, JsonOptions));
    }

    private void CommitHistoryChange()
    {
        var next = CaptureHistory();
        if (currentHistory is null)
        {
            currentHistory = next;
            return;
        }
        if (string.Equals(currentHistory.Fingerprint, next.Fingerprint, StringComparison.Ordinal))
        {
            return;
        }
        PushHistory(undoHistory, currentHistory);
        redoHistory.Clear();
        currentHistory = next;
        OnPropertiesChanged(nameof(CanUndo), nameof(CanRedo));
    }

    private void RestoreHistory(HistoryEntry target, string message)
    {
        suppressChanges = true;
        try
        {
            var state = target.State;
            kind = state.Kind;
            moduleId = state.ModuleId;
            displayName = state.DisplayName;
            packageVersion = state.PackageVersion;
            author = state.Author;
            description = state.Description;
            declaredLatencySamples = state.DeclaredLatencySamples;
            logicFile = state.LogicFile;
            skinFile = state.SkinFile;
            logicSource = state.LogicSource;
            sourceAssetPath = state.SourceAssetPath;
            algorithmId = state.AlgorithmId;
            customSkinEnabled = state.CustomSkinEnabled;
            analogStructuredMode = state.AnalogStructuredMode;
            ApplySkin(state.Skin);
            ApplyAnalogCircuit(state.AnalogCircuit);
            ReplaceDrafts(Ports, state.Ports.Select(port =>
                new ModulePortDraft(port.Id, port.Direction, port.MaxChannels)));
            ReplaceDrafts(Parameters, state.Parameters.Select(parameter =>
                new ModuleParameterDraft(parameter.Id, parameter.DisplayName, parameter.Unit,
                    parameter.Minimum, parameter.Maximum, parameter.DefaultValue, parameter.ControlKind)));
            ReplaceDrafts(DigitalOperations, state.DigitalOperations.Select(operation =>
                new DigitalOperationDraft(operation.Type, operation.ParameterId)));
        }
        finally
        {
            suppressChanges = false;
        }
        currentHistory = CaptureHistory();
        isDirty = !string.Equals(currentHistory.Fingerprint, savedFingerprint, StringComparison.Ordinal);
        nativeValidationMessage = null;
        auditionSummary = "项目已修改，需要重新试听";
        auditionDetails = string.Empty;
        auditionAnalysis = null;
        frequencyResponse = null;
        frequencyResponseSummary = "项目已修改，需要重新测量频响";
        parameterSweep = null;
        parameterSweepSummary = "项目已修改，需要重新运行参数扫动";
        auditionComparison = null;
        auditionComparisonSummary = "项目已修改，需要重新运行 A/B 对比";
        if (SelectedAuditionParameter is null)
        {
            var parameter = Parameters.FirstOrDefault();
            auditionParameterId = parameter?.Id ?? string.Empty;
            auditionParameterValueA = parameter?.Minimum ?? 0.0;
            auditionParameterValueB = parameter?.Maximum ?? 1.0;
            OnPropertiesChanged(nameof(AuditionParameterId), nameof(AuditionParameterValueA),
                nameof(AuditionParameterValueB));
        }
        status = message;
        RefreshValidation();
        OnPropertiesChanged(string.Empty);
    }

    private void ResetHistory(bool saved)
    {
        editTransactionDepth = 0;
        editTransactionChanged = false;
        undoHistory.Clear();
        redoHistory.Clear();
        currentHistory = CaptureHistory();
        if (saved)
        {
            savedFingerprint = currentHistory.Fingerprint;
            isDirty = false;
        }
        OnPropertiesChanged(nameof(CanUndo), nameof(CanRedo), nameof(IsDirty), nameof(FileStatus), nameof(WindowTitle));
    }

    private void MarkSavedHistory()
    {
        currentHistory = CaptureHistory();
        savedFingerprint = currentHistory.Fingerprint;
        isDirty = false;
    }

    private static void PushHistory(List<HistoryEntry> history, HistoryEntry entry)
    {
        history.Add(entry);
        if (history.Count > HistoryLimit)
        {
            history.RemoveAt(0);
        }
    }

    private void DraftChanged()
    {
        if (!suppressChanges)
        {
            if (editTransactionDepth > 0)
            {
                editTransactionChanged = true;
                MarkDirty();
                return;
            }
            CommitHistoryChange();
            MarkDirty();
            RefreshValidation();
        }
    }

    private void AnalogCircuitDraftChanged()
    {
        DraftChanged();
        AnalogCircuitChanged?.Invoke(this, EventArgs.Empty);
    }

    private void MarkDirty()
    {
        nativeValidationMessage = null;
        auditionSummary = "项目已修改，需要重新试听";
        auditionDetails = string.Empty;
        auditionAnalysis = null;
        frequencyResponse = null;
        frequencyResponseSummary = "项目已修改，需要重新测量频响";
        parameterSweep = null;
        parameterSweepSummary = "项目已修改，需要重新运行参数扫动";
        auditionComparison = null;
        auditionComparisonSummary = "项目已修改，需要重新运行 A/B 对比";
        if (SelectedAuditionParameter is null)
        {
            var parameter = Parameters.FirstOrDefault();
            auditionParameterId = parameter?.Id ?? string.Empty;
            auditionParameterValueA = parameter?.Minimum ?? 0.0;
            auditionParameterValueB = parameter?.Maximum ?? 1.0;
            OnPropertiesChanged(nameof(AuditionParameterId), nameof(AuditionParameterValueA),
                nameof(AuditionParameterValueB));
        }
        OnPropertiesChanged(nameof(AuditionSummary), nameof(AuditionDetails), nameof(AuditionAnalysis),
            nameof(FrequencyResponse), nameof(FrequencyResponseSummary),
            nameof(ParameterSweep), nameof(ParameterSweepSummary),
            nameof(AuditionComparison), nameof(AuditionComparisonSummary),
            nameof(AuditionParameterOptions), nameof(HasAuditionParameterOptions),
            nameof(SelectedAuditionParameter),
            nameof(AuditionInputPortOptions), nameof(AuditionOutputPortOptions));
        var dirty = currentHistory is null ||
                    !string.Equals(currentHistory.Fingerprint, savedFingerprint, StringComparison.Ordinal);
        if (isDirty == dirty)
        {
            return;
        }
        isDirty = dirty;
        OnPropertiesChanged(nameof(IsDirty), nameof(FileStatus), nameof(WindowTitle));
    }

    private bool SetDocument<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (!SetField(ref field, value, propertyName))
        {
            return false;
        }
        if (!suppressChanges)
        {
            CommitHistoryChange();
            MarkDirty();
            RefreshValidation();
        }
        return true;
    }

    private bool SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }
        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        return true;
    }

    private bool SetAuditionSetting<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (!SetField(ref field, value, propertyName))
        {
            return false;
        }
        auditionAnalysis = null;
        auditionSummary = "试听设置已修改";
        auditionDetails = string.Empty;
        frequencyResponse = null;
        frequencyResponseSummary = "测量设置已修改";
        parameterSweep = null;
        parameterSweepSummary = "参数设置已修改";
        auditionComparison = null;
        auditionComparisonSummary = "A/B 设置已修改";
        OnPropertiesChanged(nameof(AuditionSummary), nameof(AuditionDetails), nameof(AuditionAnalysis),
            nameof(FrequencyResponse), nameof(FrequencyResponseSummary),
            nameof(ParameterSweep), nameof(ParameterSweepSummary),
            nameof(AuditionComparison), nameof(AuditionComparisonSummary));
        return true;
    }

    private void ResetAuditionParameterSelection()
    {
        var parameter = Parameters.FirstOrDefault();
        auditionParameterId = parameter?.Id ?? string.Empty;
        auditionParameterValueA = parameter?.Minimum ?? 0.0;
        auditionParameterValueB = parameter?.Maximum ?? 1.0;
        parameterSweep = null;
        parameterSweepSummary = "尚未运行参数扫动";
        auditionComparison = null;
        auditionComparisonSummary = "尚未运行 A/B 对比";
    }

    private void SyncSkinPlacementControlKinds()
    {
        foreach (var placement in Skin.ParameterPlacements)
        {
            var parameter = Parameters.FirstOrDefault(candidate =>
                string.Equals(candidate.Id.Trim(), placement.ParameterId.Trim(), StringComparison.Ordinal));
            placement.SetControlKind(parameter?.ControlKind ?? "knob");
        }
    }

    private void OnPropertiesChanged(params string[] names)
    {
        foreach (var name in names)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
        }
    }

    private void ReplaceDrafts<T>(ObservableCollection<T> target, IEnumerable<T> values) where T : ModuleAuthoringDraft
    {
        target.Clear();
        foreach (var value in values)
        {
            target.Add(value);
        }
    }

    private static string UniqueId(string prefix, IEnumerable<string> existing)
    {
        var used = existing.ToHashSet(StringComparer.Ordinal);
        if (!used.Contains(prefix))
        {
            return prefix;
        }
        for (var index = 2; ; index++)
        {
            var candidate = $"{prefix}-{index}";
            if (!used.Contains(candidate))
            {
                return candidate;
            }
        }
    }

    private static string? NormalizeOptional(string? value) => string.IsNullOrWhiteSpace(value) ? null : value.Trim();

    private static string CategoryText(ModuleAuthoringKind projectKind) => projectKind switch
    {
        ModuleAuthoringKind.DigitalAlgorithm => "digital-algorithm",
        ModuleAuthoringKind.AnalogCircuit => "analog-circuit",
        ModuleAuthoringKind.NeuralAmpModel => "neural-amp-model",
        ModuleAuthoringKind.ImpulseResponse => "impulse-response",
        _ => throw new ArgumentOutOfRangeException(nameof(projectKind)),
    };

    private static bool IsSafeRelativePath(string path)
    {
        if (string.IsNullOrWhiteSpace(path) || Path.IsPathRooted(path))
        {
            return false;
        }
        return path.Replace('\\', '/').Split('/').All(part => part is not ("" or "." or ".."));
    }

    private static void AtomicWriteText(string path, string contents)
    {
        var parent = Path.GetDirectoryName(path) ?? throw new InvalidDataException("文件缺少父目录");
        Directory.CreateDirectory(parent);
        var temporary = Path.Combine(parent, $".{Path.GetFileName(path)}.{Guid.NewGuid():N}.tmp");
        try
        {
            File.WriteAllText(temporary, contents);
            File.Move(temporary, path, true);
        }
        finally
        {
            if (File.Exists(temporary))
            {
                File.Delete(temporary);
            }
        }
    }

    private static void AtomicCopyFile(string source, string destination)
    {
        var fullSource = Path.GetFullPath(source);
        var fullDestination = Path.GetFullPath(destination);
        if (string.Equals(fullSource, fullDestination, StringComparison.OrdinalIgnoreCase))
        {
            return;
        }
        var parent = Path.GetDirectoryName(fullDestination) ?? throw new InvalidDataException("文件缺少父目录");
        Directory.CreateDirectory(parent);
        var temporary = Path.Combine(parent, $".{Path.GetFileName(destination)}.{Guid.NewGuid():N}.tmp");
        try
        {
            File.Copy(fullSource, temporary, true);
            File.Move(temporary, fullDestination, true);
        }
        finally
        {
            if (File.Exists(temporary))
            {
                File.Delete(temporary);
            }
        }
    }
}
