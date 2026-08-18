namespace SoloRig.Ui;

public sealed record AnalogPinDescriptor(
    string Id,
    string DisplayName,
    double AnchorX,
    double AnchorY,
    string Side,
    string ElectricalRole);

public sealed record AnalogPropertyDescriptor(
    string Id,
    string DisplayName,
    string Kind,
    string Unit = "",
    string? DefaultJson = null,
    double? Minimum = null,
    double? Maximum = null,
    double? Increment = null,
    IReadOnlyList<string>? Options = null);

public sealed record AnalogComponentDescriptor(
    string Type,
    string DisplayName,
    string Category,
    IReadOnlyList<AnalogPinDescriptor> Pins,
    IReadOnlyList<AnalogPropertyDescriptor> Properties,
    string RuntimeType,
    string SymbolKind,
    string SymbolPath,
    double SymbolWidth,
    double SymbolHeight);

public static class AnalogComponentCatalog
{
    public static IReadOnlyList<AnalogComponentDescriptor> Components { get; } =
    [
        Component("resistor", "电阻", "无源器件", ["positive", "negative"],
            PassiveProperties("ohm", "1000.0", "1.0")),
        Component("capacitor", "电容", "无源器件", ["positive", "negative"],
            PassiveProperties("F", "0.000001", "0.0000001")),
        Component("inductor", "电感", "无源器件", ["positive", "negative"],
            PassiveProperties("H", "0.01", "0.001")),
        Component("voltage-source", "电压源", "电源与控制", ["positive", "negative"],
            [new("value", "电压", "number", "V", "9.0", -1000000.0, 1000000.0, 0.1)]),
        Component("diode", "二极管", "半导体", ["anode", "cathode"],
            [new("model", "模型 ID", "model", Options:
                ["solorig.cleanroom.diode.v1", "solorig.cleanroom.zener.v1",
                 "solorig.cleanroom.diode.dynamic-junction.v1", "solorig.cleanroom.zener.dynamic-junction.v1"]),
             new("diodeModel", "内联模型", "json", DefaultJson: DiodeModel),
             new("initial", "初始结电压", "number", "V", "0.0", Increment: 0.01)]),
        Component("potentiometer", "电位器", "电源与控制", ["end-a", "wiper", "end-b"],
            [new("value", "总阻值", "number", "ohm", "100000.0", 0.000001, 1000000000000.0, 100.0),
             new("potentiometer", "电位器设置", "json", DefaultJson: "{\"position\":0.5,\"minimum\":0.0,\"maximum\":1.0}")]),
        Component("switch", "开关", "电源与控制", ["terminal-a", "terminal-b"],
            [new("switch", "开关设置", "json", DefaultJson: "{\"states\":[\"open\",\"closed\"],\"initial\":0,\"openResistance\":1000000000.0,\"closedResistance\":0.001}")]),
        Component("vccs", "压控电流源", "放大器", ["out+", "out-", "control+", "control-"],
            [new("controlledSource", "受控源模型", "json", DefaultJson: VccsModel)]),
        Component("ideal-op-amp", "理想运放", "放大器", ["out+", "out-", "non-inverting", "inverting"],
            [new("controlledSource", "受控源模型", "json", DefaultJson: IdealOpAmpModel)]),
        Component("opamp", "运算放大器", "放大器", ["out+", "out-", "non-inverting", "inverting"],
            ModelProperties("amplifierModel", "放大器模型", BoundedOpAmpModel,
                "solorig.cleanroom.opamp.bounded.v1", "solorig.cleanroom.opamp.dynamic.v1")),
        Component("ota", "跨导放大器", "放大器", ["out+", "out-", "non-inverting", "inverting"],
            ModelProperties("amplifierModel", "放大器模型", BoundedOtaModel,
                "solorig.cleanroom.ota.bounded.v1")),
        Component("bjt-npn", "NPN 三极管", "半导体", ["collector", "base", "emitter"],
            ModelProperties("bjtModel", "BJT 模型", BjtModel,
                "solorig.cleanroom.bjt.npn.v1", "solorig.cleanroom.bjt.dynamic.npn.v2")),
        Component("bjt-pnp", "PNP 三极管", "半导体", ["collector", "base", "emitter"],
            ModelProperties("bjtModel", "BJT 模型", PnpBjtModel,
                "solorig.cleanroom.bjt.pnp.v1", "solorig.cleanroom.bjt.dynamic.pnp.v2")),
        Component("jfet-n", "N 沟道 JFET", "半导体", ["drain", "gate", "source"],
            ModelProperties("jfetModel", "JFET 模型", JfetModel,
                "solorig.cleanroom.jfet.n-channel.v1", "solorig.cleanroom.jfet.dynamic.n.v2")),
        Component("jfet-p", "P 沟道 JFET", "半导体", ["drain", "gate", "source"],
            ModelProperties("jfetModel", "JFET 模型", PChannelJfetModel,
                "solorig.cleanroom.jfet.p-channel.v1", "solorig.cleanroom.jfet.dynamic.p.v2")),
        Component("mosfet-n", "N 沟道 MOSFET", "半导体", ["drain", "gate", "source"],
            ModelProperties("mosfetModel", "MOSFET 模型", MosfetModel,
                "solorig.cleanroom.mosfet.n-channel.v1",
                "solorig.cleanroom.mosfet.n-channel.dynamic.v2")),
        Component("mosfet-p", "P 沟道 MOSFET", "半导体", ["drain", "gate", "source"],
            ModelProperties("mosfetModel", "MOSFET 模型", PChannelMosfetModel,
                "solorig.cleanroom.mosfet.p-channel.v1",
                "solorig.cleanroom.mosfet.p-channel.dynamic.v2")),
        Component("triode", "三极电子管", "电子管", ["plate", "grid", "cathode"],
            ModelProperties("triodeModel", "三极管模型", TriodeModel,
                "solorig.cleanroom.triode.space-charge.v1")),
        Component("pentode", "五极电子管", "电子管", ["plate", "screen", "grid", "cathode"],
            ModelProperties("pentodeModel", "五极管模型", PentodeModel,
                "solorig.cleanroom.pentode.space-charge.v1")),
        Component("beam-tetrode", "束射四极管", "电子管", ["plate", "screen", "grid", "cathode"],
            ModelProperties("beamTetrodeModel", "束射四极管模型", BeamTetrodeModel,
                "solorig.cleanroom.beam-tetrode.capture.v1")),
        Component("vari-mu", "变 μ 电子管", "电子管", ["plate", "grid", "cathode"],
            ModelProperties("variMuModel", "变 μ 模型", VariMuModel,
                "solorig.cleanroom.vari-mu.remote-cutoff.v1")),
        Component("coupled-inductor", "耦合电感 / 变压器", "磁性器件",
            ["primary-dot", "primary", "secondary-dot", "secondary"],
            [new("model", "模型 ID", "model", Options:
                ["solorig.cleanroom.transformer-core.lossy-linear.v1",
                 "solorig.cleanroom.transformer-core.saturating-hysteretic.v1"]),
             new("coupledInductorModel", "耦合电感模型", "json", DefaultJson: CoupledInductorModel),
             new("primaryInitialCurrent", "初级初始电流", "number", "A", "0.0", Increment: 0.001),
             new("secondaryInitialCurrent", "次级初始电流", "number", "A", "0.0", Increment: 0.001)]),
        Component("speaker-load", "扬声器负载", "负载与机电", ["positive", "negative"],
            [new("model", "模型 ID", "model", Options:
                ["solorig.cleanroom.speaker-load.electrodynamic.v1"]),
             new("speakerModel", "扬声器模型", "json", DefaultJson: SpeakerModel)]),
        Component("relay", "继电器", "负载与机电", ["coil+", "coil-", "contact+", "contact-"],
            [new("relayModel", "继电器模型", "json", DefaultJson: RelayModel)]),
        Component("optocoupler", "光耦", "负载与机电", ["emitter+", "emitter-", "resistor+", "resistor-"],
            [new("optocouplerModel", "光耦模型", "json", DefaultJson: OptocouplerModel)]),
    ];

    private static IReadOnlyList<AnalogComponentDescriptor> LegacyComponents { get; } =
    [
        Component("bjt", "双极型三极管（BJT）", "半导体", ["collector", "base", "emitter"],
            ModelProperties("bjtModel", "BJT 模型", BjtModel,
                "solorig.cleanroom.bjt.npn.v1", "solorig.cleanroom.bjt.pnp.v1",
                "solorig.cleanroom.bjt.dynamic.npn.v2", "solorig.cleanroom.bjt.dynamic.pnp.v2")),
        Component("jfet", "结型场效应管（JFET）", "半导体", ["drain", "gate", "source"],
            ModelProperties("jfetModel", "JFET 模型", JfetModel,
                "solorig.cleanroom.jfet.n-channel.v1", "solorig.cleanroom.jfet.p-channel.v1",
                "solorig.cleanroom.jfet.dynamic.n.v2", "solorig.cleanroom.jfet.dynamic.p.v2")),
        Component("mosfet", "金属氧化物场效应管（MOSFET）", "半导体", ["drain", "gate", "source"],
            ModelProperties("mosfetModel", "MOSFET 模型", MosfetModel,
                "solorig.cleanroom.mosfet.n-channel.v1", "solorig.cleanroom.mosfet.p-channel.v1",
                "solorig.cleanroom.mosfet.n-channel.dynamic.v2",
                "solorig.cleanroom.mosfet.p-channel.dynamic.v2")),
    ];

    private static readonly IReadOnlyDictionary<string, AnalogComponentDescriptor> ByType =
        Components.Concat(LegacyComponents).ToDictionary(component => component.Type, StringComparer.Ordinal);

    public static bool TryGet(string type, out AnalogComponentDescriptor descriptor) =>
        ByType.TryGetValue(type, out descriptor!);

    public static AnalogComponentDescriptor Get(string type) =>
        TryGet(type, out var descriptor) ? descriptor : Components[0];

    public static string DefaultJson(AnalogPropertyDescriptor property) =>
        property.DefaultJson ?? property.Kind switch
        {
            "number" => "0.0",
            "boolean" => "false",
            "json" => "{}",
            _ => "\"\"",
        };

    private static AnalogComponentDescriptor Component(
        string type,
        string displayName,
        string category,
        IReadOnlyList<string> pins,
        IReadOnlyList<AnalogPropertyDescriptor> properties)
    {
        var runtimeType = RuntimeType(type);
        var symbolKind = SymbolKind(type);
        var (width, height) = SymbolSize(symbolKind);
        return new AnalogComponentDescriptor(type, displayName, category,
            pins.Select((pin, index) => PinDescriptor(symbolKind, pin, index, pins.Count, width, height)).ToArray(),
            properties, runtimeType, symbolKind, SymbolPath(symbolKind), width, height);
    }

    private static string RuntimeType(string type) => type switch
    {
        "bjt-npn" or "bjt-pnp" => "bjt",
        "jfet-n" or "jfet-p" => "jfet",
        "mosfet-n" or "mosfet-p" => "mosfet",
        _ => type,
    };

    private static string SymbolKind(string type) => type switch
    {
        "bjt" or "bjt-npn" => "bjt-npn",
        "bjt-pnp" => "bjt-pnp",
        "jfet" or "jfet-n" => "jfet-n",
        "jfet-p" => "jfet-p",
        "mosfet" or "mosfet-n" => "mosfet-n",
        "mosfet-p" => "mosfet-p",
        "ideal-op-amp" or "opamp" or "ota" or "vccs" => "amplifier",
        "coupled-inductor" => "transformer",
        "speaker-load" => "speaker",
        _ => type,
    };

    private static (double Width, double Height) SymbolSize(string symbolKind) => symbolKind switch
    {
        "amplifier" or "transformer" or "relay" or "optocoupler" or "pentode" or "beam-tetrode" => (160.0, 112.0),
        "potentiometer" or "bjt-npn" or "bjt-pnp" or "jfet-n" or "jfet-p" or "mosfet-n" or "mosfet-p" or
            "triode" or "vari-mu" => (144.0, 112.0),
        _ => (144.0, 96.0),
    };

    private static AnalogPinDescriptor PinDescriptor(
        string symbolKind, string id, int index, int count, double width, double height)
    {
        var (x, y, side) = PinAnchor(symbolKind, id, index, count, width, height);
        return new AnalogPinDescriptor(id, PinDisplayName(id), x, y, side, PinRole(id));
    }

    private static (double X, double Y, string Side) PinAnchor(
        string symbolKind, string id, int index, int count, double width, double height)
    {
        if (id is "base" or "gate" or "grid" or "non-inverting" or "inverting" or "control+" or "control-")
        {
            var y = id is "inverting" or "control-" ? height * 0.68 : height * 0.42;
            return (0.0, y, "left");
        }
        if (id is "collector" or "drain" or "plate" or "out+")
        {
            return (width, height * 0.36, "right");
        }
        if (id is "emitter" or "source" or "cathode" or "out-")
        {
            return (width, height * 0.72, "right");
        }
        if (id == "wiper")
        {
            return (width * 0.5, height, "bottom");
        }
        if (count == 4)
        {
            var left = index < 2;
            return (left ? 0.0 : width, index % 2 == 0 ? height * 0.36 : height * 0.72, left ? "left" : "right");
        }
        return index == 0 ? (0.0, height * 0.56, "left") : (width, height * 0.56, "right");
    }

    private static string SymbolPath(string symbolKind) => symbolKind switch
    {
        "resistor" => "M 0 54 L 26 54 L 34 42 L 46 66 L 58 42 L 70 66 L 82 42 L 94 66 L 106 42 L 114 54 L 144 54",
        "capacitor" => "M 0 54 L 60 54 M 60 34 L 60 74 M 84 34 L 84 74 M 84 54 L 144 54",
        "inductor" => "M 0 54 L 30 54 C 30 34 50 34 50 54 C 50 34 70 34 70 54 C 70 34 90 34 90 54 C 90 34 110 34 110 54 L 144 54",
        "diode" => "M 0 54 L 48 54 L 48 34 L 84 54 L 48 74 Z M 88 32 L 88 76 M 88 54 L 144 54",
        "voltage-source" => "M 0 54 L 42 54 M 42 54 A 30 30 0 1 0 102 54 A 30 30 0 1 0 42 54 M 62 44 L 82 44 M 72 34 L 72 54 M 62 66 L 82 66 M 102 54 L 144 54",
        "potentiometer" => "M 0 62 L 26 62 L 34 50 L 46 74 L 58 50 L 70 74 L 82 50 L 94 74 L 106 50 L 114 62 L 144 62 M 72 112 L 72 82 M 64 90 L 72 82 L 80 90",
        "switch" => "M 0 54 L 52 54 M 52 54 L 94 34 M 94 54 L 144 54 M 48 54 A 4 4 0 1 0 56 54 A 4 4 0 1 0 48 54 M 90 54 A 4 4 0 1 0 98 54 A 4 4 0 1 0 90 54",
        "bjt-npn" => "M 0 58 L 48 58 M 48 32 L 48 84 M 48 46 L 96 40 L 144 40 M 48 70 L 96 78 L 144 82 M 92 68 L 104 80 M 92 88 L 104 80",
        "bjt-pnp" => "M 0 58 L 48 58 M 48 32 L 48 84 M 48 46 L 96 40 L 144 40 M 48 70 L 96 78 L 144 82 M 62 66 L 50 72 M 62 78 L 50 72",
        "jfet-n" => "M 0 58 L 46 58 M 54 32 L 54 84 M 54 44 L 98 40 L 144 40 M 54 72 L 98 80 L 144 80 M 24 58 L 40 58 M 32 50 L 40 58 L 32 66",
        "jfet-p" => "M 0 58 L 46 58 M 54 32 L 54 84 M 54 44 L 98 40 L 144 40 M 54 72 L 98 80 L 144 80 M 40 50 L 32 58 L 40 66",
        "mosfet-n" => "M 0 58 L 38 58 M 46 30 L 46 86 M 58 38 L 58 52 M 58 64 L 58 78 M 58 44 L 102 40 L 144 40 M 58 72 L 102 80 L 144 80 M 70 58 L 86 58 M 78 50 L 86 58 L 78 66",
        "mosfet-p" => "M 0 58 L 38 58 M 46 30 L 46 86 M 58 38 L 58 52 M 58 64 L 58 78 M 58 44 L 102 40 L 144 40 M 58 72 L 102 80 L 144 80 M 86 50 L 78 58 L 86 66",
        "amplifier" => "M 0 46 L 34 46 M 0 76 L 34 76 M 34 24 L 34 94 L 126 59 Z M 126 46 L 160 40 M 126 72 L 160 80 M 44 46 L 56 46 M 50 40 L 50 52 M 44 76 L 56 76",
        "transformer" => "M 0 40 L 28 40 M 0 80 L 28 80 M 28 40 C 50 40 50 54 28 54 C 50 54 50 68 28 68 C 50 68 50 80 28 80 M 76 28 L 76 92 M 84 28 L 84 92 M 132 40 C 110 40 110 54 132 54 C 110 54 110 68 132 68 C 110 68 110 80 132 80 M 132 40 L 160 40 M 132 80 L 160 80",
        "speaker" => "M 0 54 L 48 54 M 48 36 L 78 36 L 108 18 L 108 90 L 78 72 L 48 72 Z M 108 54 L 144 54",
        "triode" or "vari-mu" => "M 0 58 L 44 58 M 44 48 L 88 48 M 44 58 L 88 58 M 72 28 L 72 42 M 52 76 C 72 92 92 76 92 76 M 92 76 L 144 82 M 72 28 L 144 40",
        "pentode" or "beam-tetrode" => "M 0 58 L 36 58 M 36 44 L 92 44 M 36 54 L 92 54 M 36 64 L 92 64 M 72 24 L 72 38 M 54 80 C 72 92 90 80 90 80 M 90 80 L 160 82 M 72 24 L 160 40",
        "relay" => "M 0 40 L 28 40 C 28 26 48 26 48 40 C 48 26 68 26 68 40 L 80 40 M 0 80 L 80 80 M 96 80 L 126 54 M 126 80 L 160 80 M 126 54 L 160 40",
        "optocoupler" => "M 0 40 L 34 40 L 58 52 L 34 64 Z M 62 34 L 62 70 M 62 52 L 80 52 M 82 40 L 104 50 M 82 60 L 104 50 M 104 32 L 126 32 L 126 80 L 104 80 M 126 32 L 160 40 M 126 80 L 160 80",
        _ => "M 0 54 L 42 54 L 42 30 L 102 30 L 102 78 L 42 78 L 42 54 M 102 54 L 144 54",
    };

    private static string PinRole(string id) => id switch
    {
        "positive" or "anode" or "collector" or "drain" or "plate" or "out+" or "coil+" or "contact+" or
            "emitter+" or "resistor+" or "primary-dot" or "secondary-dot" => "positive",
        "negative" or "cathode" or "emitter" or "source" or "out-" or "coil-" or "contact-" or
            "emitter-" or "resistor-" => "negative",
        "base" or "gate" or "grid" or "non-inverting" or "inverting" or "control+" or "control-" or "wiper" => "control",
        _ => "passive",
    };

    private static IReadOnlyList<AnalogPropertyDescriptor> ModelProperties(
        string inlineId, string inlineName, string inlineDefault, params string[] modelIds) =>
    [
        new("model", "模型 ID", "model", Options: modelIds),
        new(inlineId, inlineName, "json", DefaultJson: inlineDefault),
        new("initial", "初始状态", "number", DefaultJson: "0.0"),
    ];

    private const string DiodeModel =
        "{\"kind\":\"shockley\",\"modelVersion\":1,\"saturationCurrent\":1e-12," +
        "\"thermalVoltage\":0.02585,\"maxStep\":0.5}";
    private const string VccsModel =
        "{\"kind\":\"vccs\",\"modelVersion\":1,\"gain\":0.001,\"minimum\":-1.0,\"maximum\":1.0}";
    private const string IdealOpAmpModel =
        "{\"kind\":\"ideal-op-amp\",\"modelVersion\":1,\"gain\":100000.0," +
        "\"minimum\":-12.0,\"maximum\":12.0}";
    private const string BoundedOpAmpModel =
        "{\"kind\":\"bounded-opamp\",\"modelVersion\":1,\"gain\":100000.0," +
        "\"minimumOutput\":-12.0,\"maximumOutput\":12.0,\"maxStep\":0.5}";
    private const string BoundedOtaModel =
        "{\"kind\":\"bounded-ota\",\"modelVersion\":1,\"gain\":0.001," +
        "\"minimumOutput\":-0.01,\"maximumOutput\":0.01,\"maxStep\":0.5}";
    private const string BjtModel =
        "{\"kind\":\"ebers-moll\",\"modelVersion\":1,\"polarity\":\"npn\"," +
        "\"saturationCurrent\":1e-15,\"forwardBeta\":100.0,\"reverseBeta\":1.0," +
        "\"thermalVoltage\":0.02585,\"maxCurrent\":0.01,\"maxStep\":0.5}";
    private const string PnpBjtModel =
        "{\"kind\":\"ebers-moll\",\"modelVersion\":1,\"polarity\":\"pnp\"," +
        "\"saturationCurrent\":1e-15,\"forwardBeta\":100.0,\"reverseBeta\":1.0," +
        "\"thermalVoltage\":0.02585,\"maxCurrent\":0.01,\"maxStep\":0.5}";
    private const string JfetModel =
        "{\"kind\":\"square-law\",\"modelVersion\":1,\"polarity\":\"n-channel\"," +
        "\"pinchOffVoltage\":1.0,\"transconductanceParameter\":0.001," +
        "\"maxCurrent\":0.01,\"maxStep\":0.5}";
    private const string PChannelJfetModel =
        "{\"kind\":\"square-law\",\"modelVersion\":1,\"polarity\":\"p-channel\"," +
        "\"pinchOffVoltage\":1.0,\"transconductanceParameter\":0.001," +
        "\"maxCurrent\":0.01,\"maxStep\":0.5}";
    private const string MosfetModel =
        "{\"kind\":\"enhancement-square-law\",\"modelVersion\":1,\"polarity\":\"n-channel\"," +
        "\"thresholdVoltage\":1.0,\"transconductanceParameter\":0.001," +
        "\"maxCurrent\":0.01,\"maxStep\":0.5}";
    private const string PChannelMosfetModel =
        "{\"kind\":\"enhancement-square-law\",\"modelVersion\":1,\"polarity\":\"p-channel\"," +
        "\"thresholdVoltage\":1.0,\"transconductanceParameter\":0.001," +
        "\"maxCurrent\":0.01,\"maxStep\":0.5}";
    private const string TriodeModel =
        "{\"kind\":\"space-charge\",\"modelVersion\":1,\"amplificationFactor\":100.0," +
        "\"perveance\":0.001,\"maxCurrent\":0.01,\"maxStep\":0.5}";
    private const string PentodeModel =
        "{\"kind\":\"space-charge\",\"modelVersion\":1,\"screenAmplificationFactor\":20.0," +
        "\"perveance\":0.001,\"kneeVoltage\":2.0,\"screenCurrentFraction\":0.1," +
        "\"maxCurrent\":0.01,\"maxStep\":0.5}";
    private const string BeamTetrodeModel =
        "{\"kind\":\"beam-capture\",\"modelVersion\":1,\"screenAmplificationFactor\":20.0," +
        "\"perveance\":0.001,\"kneeVoltage\":2.0,\"screenCurrentFraction\":0.1," +
        "\"maxCurrent\":0.01,\"maxStep\":0.5}";
    private const string VariMuModel =
        "{\"kind\":\"remote-cutoff\",\"modelVersion\":1,\"amplificationFactor\":20.0," +
        "\"perveance\":0.001,\"remoteCutoffCoefficient\":2.0," +
        "\"maxCurrent\":0.01,\"maxStep\":0.5}";
    private const string CoupledInductorModel =
        "{\"kind\":\"lossy-linear\",\"modelVersion\":1,\"turnsRatio\":2.0," +
        "\"magnetizingInductance\":1.0,\"primaryLeakageInductance\":0.1," +
        "\"secondaryLeakageInductance\":0.025,\"primaryResistance\":10.0," +
        "\"secondaryResistance\":5.0}";
    private const string SpeakerModel =
        "{\"kind\":\"electrodynamic\",\"modelVersion\":1,\"voiceCoilResistance\":6.0," +
        "\"voiceCoilInductance\":0.0005,\"forceFactor\":5.0,\"movingMass\":0.02," +
        "\"mechanicalResistance\":2.0,\"compliance\":0.0005,\"nonlinearStiffness\":1000.0," +
        "\"nonlinearDisplacementScale\":0.002,\"maximumCurrent\":10.0," +
        "\"maximumVelocity\":100.0,\"maximumDisplacement\":0.1," +
        "\"initialCurrent\":0.0,\"initialVelocity\":0.0,\"initialDisplacement\":0.0}";
    private const string RelayModel =
        "{\"kind\":\"generic-dynamic\",\"modelVersion\":1,\"coilResistance\":100.0," +
        "\"coilInductance\":0.001,\"openResistance\":1000000.0,\"closedResistance\":10.0," +
        "\"pullInCurrent\":0.02,\"dropOutCurrent\":0.01,\"operateTime\":0.0001," +
        "\"releaseTime\":0.0002,\"bounceDuration\":0.0,\"bounceFrequency\":0.0," +
        "\"bounceAmplitude\":0.0,\"maximumCoilCurrent\":1.0,\"initialState\":\"open\"}";
    private const string OptocouplerModel =
        "{\"kind\":\"generic-electrical\",\"modelVersion\":1,\"darkResistance\":1000000.0," +
        "\"litResistance\":100.0,\"emitterThreshold\":1.0,\"emitterScale\":4.0," +
        "\"responseExponent\":1.0,\"attackSeconds\":0.001,\"releaseSeconds\":0.01," +
        "\"maximumDrive\":5.0,\"initialIllumination\":0.0,\"emitterResistance\":220.0}";

    private static IReadOnlyList<AnalogPropertyDescriptor> PassiveProperties(
        string unit, string defaultValue, string increment) =>
    [
        new("value", "数值", "number", unit, defaultValue, 0.000000000001, 1000000000000.0,
            double.Parse(increment, System.Globalization.CultureInfo.InvariantCulture)),
        new("tolerance", "容差", "number", "%", "0.0", 0.0, 1.0, 0.01),
        new("initial", "初始状态", "number", unit is "F" ? "V" : unit is "H" ? "A" : "", "0.0"),
    ];

    private static string PinDisplayName(string id) => id switch
    {
        "positive" => "正端",
        "negative" => "负端",
        "anode" => "阳极",
        "cathode" => "阴极",
        "collector" => "集电极",
        "base" => "基极",
        "emitter" => "发射极",
        "drain" => "漏极",
        "gate" => "栅极",
        "source" => "源极",
        "plate" => "屏极",
        "screen" => "帘栅极",
        "grid" => "控制栅",
        _ => id,
    };
}
