namespace SoloRig.Ui;

public sealed record AnalogPinDescriptor(string Id, string DisplayName);

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
    IReadOnlyList<AnalogPropertyDescriptor> Properties);

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
        Component("bjt", "BJT", "半导体", ["collector", "base", "emitter"],
            ModelProperties("bjtModel", "BJT 模型", BjtModel,
                "solorig.cleanroom.bjt.npn.v1", "solorig.cleanroom.bjt.pnp.v1",
                "solorig.cleanroom.bjt.dynamic.npn.v2", "solorig.cleanroom.bjt.dynamic.pnp.v2")),
        Component("jfet", "JFET", "半导体", ["drain", "gate", "source"],
            ModelProperties("jfetModel", "JFET 模型", JfetModel,
                "solorig.cleanroom.jfet.n-channel.v1", "solorig.cleanroom.jfet.p-channel.v1",
                "solorig.cleanroom.jfet.dynamic.n.v2", "solorig.cleanroom.jfet.dynamic.p.v2")),
        Component("mosfet", "MOSFET", "半导体", ["drain", "gate", "source"],
            ModelProperties("mosfetModel", "MOSFET 模型", MosfetModel,
                "solorig.cleanroom.mosfet.n-channel.v1", "solorig.cleanroom.mosfet.p-channel.v1",
                "solorig.cleanroom.mosfet.n-channel.dynamic.v2",
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

    private static readonly IReadOnlyDictionary<string, AnalogComponentDescriptor> ByType =
        Components.ToDictionary(component => component.Type, StringComparer.Ordinal);

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
        IReadOnlyList<AnalogPropertyDescriptor> properties) =>
        new(type, displayName, category,
            pins.Select(pin => new AnalogPinDescriptor(pin, PinDisplayName(pin))).ToArray(), properties);

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
    private const string JfetModel =
        "{\"kind\":\"square-law\",\"modelVersion\":1,\"polarity\":\"n-channel\"," +
        "\"pinchOffVoltage\":1.0,\"transconductanceParameter\":0.001," +
        "\"maxCurrent\":0.01,\"maxStep\":0.5}";
    private const string MosfetModel =
        "{\"kind\":\"enhancement-square-law\",\"modelVersion\":1,\"polarity\":\"n-channel\"," +
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
