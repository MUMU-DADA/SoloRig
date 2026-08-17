using SoloRig.Ui;
using System.IO.Compression;

var authoringDirectory = Path.Combine(Path.GetTempPath(), $"solorig-authoring-contract-{Guid.NewGuid():N}");
var authoringWorkflowDirectory = Path.Combine(Path.GetTempPath(), $"solorig-authoring-workflow-{Guid.NewGuid():N}");
try
{
    var authoring = new ModuleAuthoringDocument();
    Require(authoring.Kind == ModuleAuthoringKind.DigitalAlgorithm && authoring.IsValid && !authoring.IsDirty,
        "DIY authoring starts with a clean, structurally valid digital template");
    var delayTemplate = new ModuleAuthoringDocument();
    delayTemplate.NewProjectFromTemplate("digital-delay");
    Require(ModuleAuthoringDocument.TemplateOptions.Count >= 6 && delayTemplate.IsValid && !delayTemplate.IsDirty &&
            delayTemplate.Parameters.Select(parameter => parameter.Id).SequenceEqual(["time", "feedback", "mix"]) &&
            delayTemplate.DigitalOperations.Select(operation => operation.Type)
                .SequenceEqual(["delay", "feedback", "mix"]),
        "template library provides a ready-to-run digital delay project");
    var diodeTemplate = new ModuleAuthoringDocument();
    diodeTemplate.NewProjectFromTemplate("analog-diode-clipper");
    Require(diodeTemplate.IsValid && !diodeTemplate.IsDirty &&
            diodeTemplate.AnalogCircuit.Components.Any(component => component.Id == "clipper" &&
                component.Type == "diode" && component.Pins[0].Node == "out" && component.Pins[1].Node == "0"),
        "template library provides a valid component-level diode clipper project");
    authoring.ModuleId = "org.mumu-dada.solorig.contract-diy";
    authoring.DisplayName = "Contract DIY";
    authoring.Author = "SoloRig Contract";
    authoring.Description = "Round-trip module authoring contract";
    Require(authoring.IsDirty && authoring.WindowTitle.Contains("*", StringComparison.Ordinal),
        "DIY authoring metadata edits mark the project dirty and visible");
    Require(authoring.CanUndo && authoring.Undo() && string.IsNullOrEmpty(authoring.Description) &&
            authoring.CanRedo && authoring.Redo() &&
            authoring.Description == "Round-trip module authoring contract",
        "DIY authoring undo and redo restore complete document metadata snapshots");
    var duplicatePort = authoring.AddPort("input");
    duplicatePort.Id = "input";
    Require(!authoring.IsValid && authoring.ValidationMessages.Any(message => message.Contains("端口 ID 重复", StringComparison.Ordinal)),
        "DIY authoring rejects duplicate port identifiers before saving");
    Require(authoring.RemovePort(duplicatePort) && authoring.IsValid,
        "removing the duplicate port restores structural validity");
    var extraParameter = authoring.AddParameter();
    extraParameter.Id = "tone";
    extraParameter.DisplayName = "Tone";
    extraParameter.Unit = "%";
    extraParameter.ControlKind = "knob";
    var toneOperation = authoring.AddDigitalOperation();
    toneOperation.Type = "filter";
    toneOperation.ParameterId = "tone";
    authoring.CustomSkinEnabled = true;
    authoring.Skin.Id = "contract.diy-skin";
    authoring.Skin.KnobId = "contract-knob";
    authoring.Skin.FootswitchId = "contract-footswitch";
    authoring.Skin.Width = 248;
    authoring.Skin.Height = 188;
    authoring.Skin.Background = "#173B4A";
    authoring.Skin.LabelColor = "#F4F7FA";
    authoring.Skin.UseCustomFootswitchPosition = true;
    authoring.Skin.FootswitchX = 124;
    authoring.Skin.FootswitchY = 158;
    var tonePlacement = authoring.AddSkinParameterPlacement();
    tonePlacement.ParameterId = "tone";
    tonePlacement.X = 172;
    tonePlacement.Y = 92;
    Require(authoring.Undo() && authoring.Skin.ParameterPlacements.Single().Y != 92 && authoring.Redo() &&
            authoring.Skin.ParameterPlacements.Single().Y == 92,
        "DIY authoring history covers nested pedal layout coordinate edits");
    var liveToneParameter = authoring.Parameters.Single(parameter => parameter.Id == "tone");
    var liveTonePlacement = authoring.Skin.ParameterPlacements.Single(placement => placement.ParameterId == "tone");
    liveToneParameter.ControlKind = "toggle";
    Require(liveTonePlacement.IsToggle && liveTonePlacement.ControlKindDisplay == "拨档",
        "pedal designer projects parameter control kinds into the matching canvas control");
    liveToneParameter.ControlKind = "knob";
    Require(authoring.SaveToDirectory(authoringDirectory) && !authoring.IsDirty &&
            File.Exists(Path.Combine(authoringDirectory, "manifest.json")) &&
            File.Exists(Path.Combine(authoringDirectory, "logic", "algorithm.json")) &&
            File.Exists(Path.Combine(authoringDirectory, "ui", "pedal.json")),
        "DIY authoring saves package-shaped manifest, logic and custom appearance files atomically");
    Directory.CreateDirectory(authoringWorkflowDirectory);
    var importedSkinSource = Path.Combine(authoringWorkflowDirectory, "custom-knob.bmp");
    File.WriteAllBytes(importedSkinSource, OneByOneBmp());
    var importedSkinRelative = ModulePackageWorkflow.ImportProjectResource(
        authoringDirectory, importedSkinSource, "ui/images");
    authoring.Skin.KnobImage = importedSkinRelative[3..];
    var preservedResource = Path.Combine(authoringDirectory, "ui", "images", "manual-resource.bin");
    File.WriteAllBytes(preservedResource, [1, 2, 3, 4]);
    Require(authoring.Save() && File.Exists(preservedResource) &&
            File.Exists(Path.Combine(authoringDirectory,
                importedSkinRelative.Replace('/', Path.DirectorySeparatorChar))),
        "transactional project save preserves imported and unmanaged project resources");
    var projectCopyDirectory = Path.Combine(authoringWorkflowDirectory, "project-copy");
    ModulePackageWorkflow.CopyProject(authoringDirectory, projectCopyDirectory);
    Require(File.Exists(Path.Combine(projectCopyDirectory, "manifest.json")) &&
            File.Exists(Path.Combine(projectCopyDirectory,
                importedSkinRelative.Replace('/', Path.DirectorySeparatorChar))),
        "project copy creates a complete independent package directory");
    var exportPath = Path.Combine(authoringWorkflowDirectory, "contract-diy.srmodule");
    ModulePackageWorkflow.ExportPackage(authoringDirectory, exportPath);
    using (var archive = ZipFile.OpenRead(exportPath))
    {
        Require(archive.Entries.Any(entry => entry.FullName == "manifest.json") &&
                archive.Entries.Any(entry => entry.FullName == importedSkinRelative) &&
                archive.Entries.All(entry => !entry.FullName.StartsWith(".solorig/", StringComparison.OrdinalIgnoreCase)),
            "module export includes runtime resources and excludes private workshop metadata");
    }
    var moduleLibrary = Path.Combine(authoringWorkflowDirectory, "module-library");
    var installedDirectory = ModulePackageWorkflow.Install(authoringDirectory, moduleLibrary);
    Require(File.Exists(Path.Combine(installedDirectory, "manifest.json")) &&
            Throws<ModuleInstallConflictException>(() =>
                ModulePackageWorkflow.Install(authoringDirectory, moduleLibrary)),
        "module install persists a package and reports a version conflict before overwrite");
    authoring.PackageVersion = "1.1.0";
    Require(authoring.Save(), "updated package version saves before overwrite installation");
    installedDirectory = ModulePackageWorkflow.Install(authoringDirectory, moduleLibrary, overwrite: true);
    Require(ModulePackageWorkflow.ReadManifest(installedDirectory).PackageVersion == "1.1.0",
        "confirmed overwrite installation replaces the package transactionally");
    var auditionWavePath = Path.Combine(authoringDirectory, "audition-input.wav");
    WritePcm16Wave(auditionWavePath, 48000U, (ushort)2,
        Enumerable.Range(0, 64).SelectMany(index =>
        {
            var sample = (short)(Math.Sin(index * Math.PI / 8.0) * 12000.0);
            return new[] { sample, (short)(sample / 2) };
        }).ToArray());
    var auditionWave = AuditionWaveFile.Load(auditionWavePath);
    Require(auditionWave is { SampleRateHz: 48000, SourceChannelCount: 2, FrameCount: 64 } &&
            auditionWave.MonoSamples.Count == 64 && auditionWave.MonoSamples.Any(sample => Math.Abs(sample) > 0.1F),
        "file audition decodes bounded stereo PCM16 WAV and explicitly downmixes it to mono");
    authoring.SetAuditionWaveFile(auditionWave);
    Require(!authoring.IsDirty && authoring.HasAuditionFile &&
            authoring.CreateAuditionRequest() is
                { Stimulus: ModuleAuditionStimulus.CallerWaveform, SampleRateHz: 48000, BlockSize: 64,
                  InputWaveform.Count: 64 },
        "file audition selection remains view state and adopts the validated WAV format");
    using (var authoringBackend = new BackendSession())
    {
        authoringBackend.ValidateModulePackage(authoringDirectory);
        var audition = authoringBackend.AuditionModulePackage(authoringDirectory);
        Require(audition is { SampleRateHz: 48000, BlockSize: 256, OutputFinite: true } &&
                double.IsFinite(audition.OutputPeak) && double.IsFinite(audition.OutputRms) &&
                double.IsFinite(audition.ProcessingCpuPercent),
            "DIY authoring runs an offline native audition without ASIO hardware");
        authoring.SetAuditionResult(audition);
        Require(authoring.AuditionSummary.Contains("通过", StringComparison.Ordinal) &&
                authoring.AuditionDetails.Contains("48000 Hz", StringComparison.Ordinal),
            "DIY authoring exposes offline audition format, level, latency and CPU diagnostics");
        var parameterRequest = new ModuleAuditionRequest(
            48000U, 512U, 1U, ModuleAuditionStimulus.Sine, "input", "output",
            0.1, 440.0, 440.0);
        var comparison = authoringBackend.CompareModuleParameter(
            authoringDirectory, parameterRequest, "gain", 0.5, 1.5);
        Require(comparison.OutputRmsDeltaDb > 8.5 && comparison.ResultB.OutputRms > comparison.ResultA.OutputRms,
            "DIY authoring A/B comparison applies two real runtime parameter values");
        var sweep = authoringBackend.MeasureModuleParameterSweep(
            authoringDirectory, parameterRequest, "gain", 0.25, 1.75, 7);
        Require(sweep.Points.Count == 7 && sweep.Points[^1].GainDb > sweep.Points[0].GainDb + 12.0,
            "DIY authoring parameter sweep measures monotonic runtime gain changes");
        authoring.SetParameterSweepResult(sweep);
        authoring.SetAuditionComparisonResult(comparison);
        Require(authoring.ParameterSweepSummary.Contains("7 点", StringComparison.Ordinal) &&
                authoring.AuditionComparisonSummary.Contains("A/B 完成", StringComparison.Ordinal) &&
                !authoring.IsDirty,
            "parameter sweep and A/B results remain workshop analysis state");
        var fileAudition = authoringBackend.AuditionModulePackage(
            authoringDirectory, authoring.CreateAuditionRequest());
        Require(fileAudition.InputWaveform.SequenceEqual(auditionWave.MonoSamples) &&
                fileAudition.OutputRms > 0.0 && fileAudition.OutputFinite,
            "native offline audition consumes the caller-owned WAV waveform instead of regenerating a stimulus");
    }
    var recentStorePath = Path.Combine(authoringDirectory, "recent-projects.json");
    var recentProjects = new ModuleAuthoringRecentProjects(recentStorePath);
    recentProjects.Touch(authoringDirectory);
    var reopenedRecentProjects = new ModuleAuthoringRecentProjects(recentStorePath);
    Require(reopenedRecentProjects.Entries.Count == 1 &&
            string.Equals(reopenedRecentProjects.Entries[0].Directory, authoringDirectory, StringComparison.OrdinalIgnoreCase),
        "DIY authoring persists a bounded recent-project entry after a successful save");
    using (var authoringRig = new RigEditorViewModel())
    {
        Require(authoringRig.ImportResource(authoringDirectory) &&
                authoringRig.AddModule(authoring.ModuleId, "contract-diy-1", 520, 180),
            "DIY authoring installs its exported package into the current module catalog and rig");
        var installedNode = authoringRig.CanvasNodes.Single(node => node.InstanceId == "contract-diy-1");
        Require(installedNode.Skin.Id == "contract.diy-skin" && installedNode.Skin.KnobId == "contract-knob" &&
                installedNode.Skin.FootswitchId == "contract-footswitch" &&
                installedNode.Width == 248 && installedNode.Height == 188 &&
                installedNode.CardBackground == "#173B4A" && installedNode.LabelForeground == "#F4F7FA" &&
                 installedNode.Controls.Single(control => control.Id == "tone") is { X: 172, Y: 92 } &&
                 installedNode.HasCustomFootswitchPosition &&
                 installedNode.FootswitchCanvasLeft == 113 && installedNode.FootswitchCanvasTop == 147,
            $"DIY custom layout reaches the shared renderer with exact size, colors and control coordinates; " +
            $"skin={installedNode.Skin.Id}/{installedNode.Skin.KnobId}/{installedNode.Skin.FootswitchId}, " +
            $"size={installedNode.Width}x{installedNode.Height}, colors={installedNode.CardBackground}/{installedNode.LabelForeground}, " +
            $"tone={installedNode.Controls.Single(control => control.Id == "tone").X}," +
            $"{installedNode.Controls.Single(control => control.Id == "tone").Y}, " +
            $"foot={installedNode.HasCustomFootswitchPosition}/" +
            $"{installedNode.FootswitchCanvasLeft},{installedNode.FootswitchCanvasTop}");
    }

    var reopenedAuthoring = new ModuleAuthoringDocument();
    Require(reopenedAuthoring.OpenFromDirectory(authoringDirectory) && reopenedAuthoring.IsValid &&
            reopenedAuthoring.ModuleId == authoring.ModuleId && reopenedAuthoring.DisplayName == authoring.DisplayName &&
            reopenedAuthoring.Ports.Count == authoring.Ports.Count && reopenedAuthoring.Parameters.Count == authoring.Parameters.Count &&
            reopenedAuthoring.DigitalOperations.Count == 2 &&
            reopenedAuthoring.DigitalOperations.Any(operation => operation.Type == "filter" && operation.ParameterId == "tone") &&
            reopenedAuthoring.CustomSkinEnabled && reopenedAuthoring.Skin.Id == "contract.diy-skin" &&
            reopenedAuthoring.Skin.KnobId == "contract-knob" && reopenedAuthoring.Skin.FootswitchId == "contract-footswitch" &&
            reopenedAuthoring.Skin.Width == 248 && reopenedAuthoring.Skin.Height == 188 &&
            reopenedAuthoring.Skin.Background == "#173B4A" && reopenedAuthoring.Skin.LabelColor == "#F4F7FA" &&
            reopenedAuthoring.Skin.UseCustomFootswitchPosition && reopenedAuthoring.Skin.FootswitchX == 124 &&
            reopenedAuthoring.Skin.FootswitchY == 158 &&
            reopenedAuthoring.Skin.ParameterPlacements.Single() is { ParameterId: "tone", X: 172, Y: 92 } &&
            reopenedAuthoring.LogicSource.Contains("digital-algorithm", StringComparison.Ordinal),
        "DIY authoring reopens metadata, operations and complete pedal layout without data loss");
    reopenedAuthoring.Skin.ParameterPlacements[0].X = 999;
    Require(!reopenedAuthoring.IsValid &&
            reopenedAuthoring.ValidationMessages.Any(message => message.Contains("坐标超出", StringComparison.Ordinal)),
        "DIY authoring rejects parameter controls outside the pedal canvas");
    reopenedAuthoring.Skin.ParameterPlacements[0].X = 172;
    Require(reopenedAuthoring.IsValid, "restoring the skin coordinate restores package validity");
    reopenedAuthoring.Parameters[0].DefaultValue = reopenedAuthoring.Parameters[0].Maximum + 1.0;
    Require(!reopenedAuthoring.IsValid && !reopenedAuthoring.Save(),
        "DIY authoring refuses to overwrite a project with an invalid parameter default");

    var namAuthoring = new ModuleAuthoringDocument();
    namAuthoring.NewProject(ModuleAuthoringKind.NeuralAmpModel);
    Require(!namAuthoring.IsValid && namAuthoring.ValidationMessages.Any(message => message.Contains("NAM", StringComparison.Ordinal)),
        "NAM authoring template requires an explicit model asset before export");

    var analogDirectory = Path.Combine(authoringDirectory, "analog-project");
    var analogAuthoring = new ModuleAuthoringDocument();
    analogAuthoring.NewProject(ModuleAuthoringKind.AnalogCircuit);
    analogAuthoring.ModuleId = "org.mumu-dada.solorig.contract-analog-diy";
    analogAuthoring.DisplayName = "Contract Analog DIY";
    Require(analogAuthoring.UsesStructuredAnalogLogic && analogAuthoring.AnalogCircuit.Components.Count == 2 &&
            analogAuthoring.IsValid,
        "analog DIY template starts in a valid structured circuit-v2 netlist editor");
    var capacitor = analogAuthoring.AddAnalogComponent();
    capacitor.Id = "tone-cap";
    capacitor.Type = "capacitor";
    capacitor.PinA = "out";
    capacitor.PinB = "0";
    capacitor.Value = 0.000001;
    Require(analogAuthoring.SaveToDirectory(analogDirectory),
        "structured analog DIY editor serializes a supported circuit-v2 package");
    analogAuthoring.AuditionStimulusIndex = 2;
    analogAuthoring.AuditionInputPortId = "input";
    analogAuthoring.AuditionOutputPortId = "output";
    analogAuthoring.AuditionAmplitude = 0.15;
    analogAuthoring.AuditionStartFrequencyHz = 120.0;
    analogAuthoring.AuditionEndFrequencyHz = 6000.0;
    analogAuthoring.AuditionBlockSize = 1024;
    Require(!analogAuthoring.IsDirty && analogAuthoring.CreateAuditionRequest() is
            { Stimulus: ModuleAuditionStimulus.LogarithmicSweep, InputPortId: "input",
              OutputPortId: "output", BlockSize: 1024 },
        "offline audition controls remain view state and preserve the saved project dirty flag");
    using (var analogBackend = new BackendSession())
    {
        analogBackend.ValidateModulePackage(analogDirectory);
        Require(analogBackend.AuditionModulePackage(analogDirectory).OutputFinite,
            "structured analog DIY package prepares and processes in the offline audition runtime");
        var measuredAudition = analogBackend.AuditionModulePackage(
            analogDirectory, analogAuthoring.CreateAuditionRequest());
        Require(measuredAudition is { SchemaVersion: 3, BlockSize: 1024, OutputFinite: true } &&
                measuredAudition.InputWaveform.Count == 1024 && measuredAudition.OutputWaveform.Count == 1024 &&
                measuredAudition.InputRms > 0.0 && measuredAudition.OutputRms > 0.0 &&
                double.IsFinite(measuredAudition.InputDc) && double.IsFinite(measuredAudition.OutputDc) &&
                measuredAudition.HasSolverDiagnostics && measuredAudition.SolverProcessedSampleCount == 1024 &&
                measuredAudition.SolverMaximumIterations >= 1 && measuredAudition.SolverFailureCount == 0,
            "configurable native audition returns waveforms, metrics and real MNA convergence diagnostics");
        analogAuthoring.SetAuditionResult(measuredAudition);
        Require(ReferenceEquals(analogAuthoring.AuditionAnalysis, measuredAudition) &&
                analogAuthoring.AuditionSummary.Contains("测量通过", StringComparison.Ordinal) &&
                analogAuthoring.AuditionDetails.Contains("输入 Peak", StringComparison.Ordinal) &&
                analogAuthoring.AuditionDetails.Contains("Newton 平均", StringComparison.Ordinal) &&
                !analogAuthoring.IsDirty,
            "workshop document exposes measured audition results without changing project state");
        var frequencyResponse = analogBackend.MeasureModuleFrequencyResponse(
            analogDirectory, analogAuthoring.CreateAuditionRequest(), 12);
        Require(frequencyResponse.Points.Count == 12 &&
                frequencyResponse.Points.All(point => double.IsFinite(point.MagnitudeDb) &&
                                                      double.IsFinite(point.PhaseDegrees)) &&
                frequencyResponse.Points[0].MagnitudeDb > frequencyResponse.Points[^1].MagnitudeDb + 6.0,
            "workshop frequency response measures the generated RC circuit roll-off through native processing");
        analogAuthoring.SetFrequencyResponseResult(frequencyResponse);
        Require(ReferenceEquals(analogAuthoring.FrequencyResponse, frequencyResponse) &&
                analogAuthoring.FrequencyResponseSummary.Contains("12 点", StringComparison.Ordinal) &&
                !analogAuthoring.IsDirty,
            "frequency response results remain non-document workshop analysis state");
    }
    var reopenedAnalog = new ModuleAuthoringDocument();
    Require(reopenedAnalog.OpenFromDirectory(analogDirectory) && reopenedAnalog.UsesStructuredAnalogLogic &&
            reopenedAnalog.AnalogCircuit.Components.Any(component => component.Id == "tone-cap" && component.Type == "capacitor"),
        "supported circuit-v2 packages reopen in structured mode without losing components");
    Require(reopenedAnalog.SetAnalogStructuredMode(false) && reopenedAnalog.UsesTextLogic &&
            reopenedAnalog.LogicSource.Contains("tone-cap", StringComparison.Ordinal) &&
            reopenedAnalog.SetAnalogStructuredMode(true) && reopenedAnalog.UsesStructuredAnalogLogic,
        "analog DIY editor round-trips between structured netlist and advanced JSON modes");

    Require(AnalogComponentCatalog.Components.Count >= 22 &&
            AnalogComponentCatalog.Get("bjt").Pins.Count == 3 &&
            AnalogComponentCatalog.Get("opamp").Pins.Count == 4,
        "analog DIY component catalog exposes the complete current Circuit v2 runtime families");
    var inlineDefaultTypes = new[]
    {
        "diode", "vccs", "ideal-op-amp", "opamp", "ota", "bjt", "jfet", "mosfet", "triode",
        "pentode", "beam-tetrode", "vari-mu", "coupled-inductor", "speaker-load", "relay", "optocoupler",
    };
    Require(inlineDefaultTypes.All(type => AnalogComponentCatalog.Get(type).Properties
                .Where(property => property.Kind == "json")
                .All(property => AnalogComponentCatalog.DefaultJson(property) != "{}")),
        "advanced analog components expose complete inline defaults instead of empty JSON objects");
    var componentAuthoringDirectory = Path.Combine(authoringDirectory, "component-authoring-project");
    var componentAuthoring = new ModuleAuthoringDocument();
    componentAuthoring.NewProject(ModuleAuthoringKind.AnalogCircuit);
    componentAuthoring.ModuleId = "org.mumu-dada.solorig.contract-component-authoring";
    componentAuthoring.DisplayName = "Contract Component Authoring";
    var bjt = componentAuthoring.AddAnalogComponent("bjt", 320.0, 180.0);
    bjt.Id = "gain-stage";
    bjt.Pins[0].Node = "out";
    bjt.Pins[1].Node = "in";
    bjt.Pins[2].Node = "0";
    bjt.SetJsonProperty("bjtModel",
        "{\"kind\":\"ebers-moll\",\"modelVersion\":1,\"polarity\":\"npn\",\"saturationCurrent\":1e-15," +
        "\"forwardBeta\":100.0,\"reverseBeta\":1.0,\"thermalVoltage\":0.02585," +
        "\"maxCurrent\":0.01,\"maxStep\":1.0}");
    var resistorValueProperty = componentAuthoring.AnalogCircuit.Components[0].Properties.Single(property =>
        property.Id == "value");
    resistorValueProperty.NumberValue = 4700.0;
    var bjtModelIdProperty = bjt.Properties.Single(property => property.Id == "model");
    bjtModelIdProperty.TextValue = "contract-bjt";
    Require(resistorValueProperty is { IsNumber: true, Unit: "ohm", NumberValue: 4700.0 } &&
            componentAuthoring.AnalogCircuit.Components[0].Value == 4700.0 &&
            bjtModelIdProperty.IsModel && bjtModelIdProperty.Options.Contains("solorig.cleanroom.bjt.npn.v1") &&
            bjt.Model == "contract-bjt" &&
            bjt.Properties.Single(property => property.Id == "bjtModel").IsJson,
        "component inspector descriptors expose typed number, model text and nested JSON editors");
    Require(componentAuthoring.IsValid && componentAuthoring.ValidationIssues.Any(issue =>
                issue is { IsWarning: true, Target: ModuleAuthoringIssueTarget.AnalogProperty, FieldId: "model" } &&
                ReferenceEquals(issue.TargetObject, bjt)),
        "audited model references remain saveable but expose a targeted runtime authorization warning");
    var bjtInlineProperty = bjt.Properties.Single(property => property.Id == "bjtModel");
    var forwardBetaField = bjtInlineProperty.JsonFields.Single(field => field.Name == "forwardBeta");
    forwardBetaField.NumberValue = 150.0;
    Require(bjtInlineProperty.HasJsonFields && forwardBetaField.IsNumber &&
            bjtInlineProperty.JsonValue.Contains("150", StringComparison.Ordinal),
        "inline component model objects expose editable typed fields that update their parent JSON");
    bjtModelIdProperty.TextValue = string.Empty;
    Require(componentAuthoring.SaveToDirectory(componentAuthoringDirectory),
        "component-level analog authoring saves multi-pin components and nested model properties");
    var reopenedComponentAuthoring = new ModuleAuthoringDocument();
    Require(reopenedComponentAuthoring.OpenFromDirectory(componentAuthoringDirectory) &&
            reopenedComponentAuthoring.UsesStructuredAnalogLogic,
        "component-level analog project reopens in structured mode");
    var reopenedBjt = reopenedComponentAuthoring.AnalogCircuit.Components.Single(component =>
        component.Id == "gain-stage");
    Require(reopenedBjt.Type == "bjt" && reopenedBjt.Pins.Count == 3 &&
            reopenedBjt.Pins[0].Node == "out" && reopenedBjt.Pins[1].Node == "in" &&
            reopenedBjt.Pins[2].Node == "0" && reopenedBjt.CanvasX == 320.0 && reopenedBjt.CanvasY == 180.0 &&
            reopenedBjt.TryGetProperty("bjtModel", out var reopenedBjtModel) &&
            reopenedBjtModel.Contains("ebers-moll", StringComparison.Ordinal) &&
            reopenedBjt.Properties.Single(property => property.Id == "bjtModel").JsonFields
                .Single(field => field.Name == "forwardBeta").NumberValue == 150.0,
        "component-level analog authoring preserves pins, nested model JSON and editor layout metadata");

    var defaultInlineDirectory = Path.Combine(authoringDirectory, "default-inline-model-project");
    var defaultInlineAuthoring = new ModuleAuthoringDocument();
    defaultInlineAuthoring.NewProject(ModuleAuthoringKind.AnalogCircuit);
    defaultInlineAuthoring.ModuleId = "org.mumu-dada.solorig.contract-default-inline";
    defaultInlineAuthoring.DisplayName = "Contract Default Inline Models";
    var defaultBjt = defaultInlineAuthoring.AddAnalogComponent("bjt", 280.0, 160.0);
    defaultBjt.Id = "default-bjt";
    defaultBjt.Pins[0].Node = "out";
    defaultBjt.Pins[1].Node = "in";
    defaultBjt.Pins[2].Node = "0";
    var defaultDiode = defaultInlineAuthoring.AddAnalogComponent("diode", 500.0, 160.0);
    defaultDiode.Id = "default-diode";
    defaultDiode.Pins[0].Node = "out";
    defaultDiode.Pins[1].Node = "0";
    Require(defaultInlineAuthoring.SaveToDirectory(defaultInlineDirectory),
        "new advanced components save with their inline defaults without hand-written JSON");
    var defaultInlineSource = File.ReadAllText(Path.Combine(defaultInlineDirectory, "logic", "circuit.json"));
    Require(defaultInlineSource.Contains("\"bjtModel\"", StringComparison.Ordinal) &&
            defaultInlineSource.Contains("\"kind\": \"ebers-moll\"", StringComparison.Ordinal) &&
            defaultInlineSource.Contains("\"model\": {", StringComparison.Ordinal) &&
            !defaultInlineSource.Contains("\"diodeModel\"", StringComparison.Ordinal) &&
            !defaultInlineSource.Contains("\"model\": \"\"", StringComparison.Ordinal),
        "generated Circuit v2 selects inline model mode and maps the diode editor model to the runtime field");
    using (var defaultInlineBackend = new BackendSession())
    {
        defaultInlineBackend.ValidateModulePackage(defaultInlineDirectory);
    }
    var reopenedDefaultInline = new ModuleAuthoringDocument();
    Require(reopenedDefaultInline.OpenFromDirectory(defaultInlineDirectory) &&
            reopenedDefaultInline.AnalogCircuit.Components.Single(component => component.Id == "default-diode")
                .Properties.Single(property => property.Id == "diodeModel").HasJsonFields &&
            reopenedDefaultInline.AnalogCircuit.Components.Single(component => component.Id == "default-bjt")
                .Properties.Single(property => property.Id == "model").TextValue == string.Empty,
        "inline runtime models reopen into typed fields while retaining the audited-model selector");

    var rootAuthoringDirectory = Path.Combine(authoringDirectory, "root-authoring-project");
    var rootAuthoring = new ModuleAuthoringDocument();
    rootAuthoring.NewProject(ModuleAuthoringKind.AnalogCircuit);
    rootAuthoring.ModuleId = "org.mumu-dada.solorig.contract-root-authoring";
    rootAuthoring.DisplayName = "Contract Root Authoring";
    var voltageParameter = rootAuthoring.AddParameter();
    voltageParameter.Id = "voltage";
    voltageParameter.DisplayName = "Voltage";
    voltageParameter.Unit = "V";
    voltageParameter.Minimum = 0.0;
    voltageParameter.Maximum = 18.0;
    voltageParameter.DefaultValue = 9.0;
    var sidechainModulePort = rootAuthoring.AddPort("input");
    sidechainModulePort.Id = "sidechain";
    sidechainModulePort.MaxChannels = 1;
    Require(rootAuthoring.SetAnalogStructuredMode(false),
        "root-level analog contract can enter advanced JSON mode");
    rootAuthoring.LogicSource = """
        {
          "version": 2,
          "schema": "circuit",
          "referenceGround": "0",
          "nodes": ["in", "side", "out", "vcc", "0", "reserved"],
          "ports": [
            {"id":"input","direction":"input","node":"in"},
            {"id":"sidechain","direction":"input","node":"side"},
            {"id":"output","direction":"output","node":"out"}
          ],
          "supplies": [
            {"id":"rail","positive":"vcc","negative":"0","voltage":9.0,"kind":"ideal","modelVersion":1}
          ],
          "components": [
            {"id":"series","type":"resistor","pins":["in","out"],"value":1000.0},
            {"id":"load","type":"resistor","pins":["out","0"],"value":1000.0}
          ],
          "parameterBindings": [
            {"modelVersion":1,"parameter":"voltage","target":"rail","property":"supply.voltage","smoothingSeconds":0.02}
          ],
          "measurementBaseline": {"reference":"contract","frequencyHz":1000.0,"tolerance":0.01},
          "editorNote": "preserve this extension",
          "solver": {
            "method":"modified-nodal-analysis",
            "integration":"backward-euler",
            "oversampling":4,
            "diagnosticLabel":"root-contract",
            "convergence": {
              "maxIterations":32,
              "absoluteTolerance":1e-10,
              "relativeTolerance":1e-7,
              "diagnosticMode":true
            }
          }
        }
        """;
    Require(rootAuthoring.SetAnalogStructuredMode(true) && rootAuthoring.IsValid &&
            rootAuthoring.AnalogCircuit.Ports.Count == 3 &&
            rootAuthoring.AnalogCircuit.Supplies.Count == 1 &&
            rootAuthoring.AnalogCircuit.ParameterBindings.Count == 1 &&
            rootAuthoring.AnalogCircuit.MaxIterations == 32 &&
            rootAuthoring.AnalogCircuit.RootProperties.Any(property => property.Id == "measurementBaseline") &&
            rootAuthoring.AnalogCircuit.RootProperties.Any(property => property.Id == "editorNote") &&
            rootAuthoring.AnalogCircuit.SolverProperties.Any(property => property.Id == "diagnosticLabel") &&
            rootAuthoring.AnalogCircuit.ConvergenceProperties.Any(property => property.Id == "diagnosticMode"),
        "structured analog editor accepts ports, supplies, bindings, solver settings and extension fields");
    Require(rootAuthoring.SaveToDirectory(rootAuthoringDirectory),
        "root-level structured analog project saves without falling back to advanced JSON");
    var rootRuntimeRejected = false;
    using (var rootAuthoringBackend = new BackendSession())
    {
        try
        {
            rootAuthoringBackend.ValidateModulePackage(rootAuthoringDirectory);
        }
        catch (BackendException exception) when (exception.Message.Contains("singular", StringComparison.OrdinalIgnoreCase))
        {
            rootRuntimeRejected = true;
        }
    }
    Require(rootRuntimeRejected && rootAuthoring.ValidationIssues.Any(issue =>
                issue is { IsWarning: true, Target: ModuleAuthoringIssueTarget.AnalogPort, TargetId: "sidechain" }),
        "runtime validation rejects an unconnected multi-port circuit as singular while local diagnostics locate the port");
    var reopenedRootAuthoring = new ModuleAuthoringDocument();
    Require(reopenedRootAuthoring.OpenFromDirectory(rootAuthoringDirectory) &&
            reopenedRootAuthoring.UsesStructuredAnalogLogic &&
            reopenedRootAuthoring.AnalogCircuit.Ports.Any(port => port.Id == "sidechain" && port.Node == "side") &&
            reopenedRootAuthoring.AnalogCircuit.Supplies.Single() is { Id: "rail", Voltage: 9.0 } &&
            reopenedRootAuthoring.AnalogCircuit.ParameterBindings.Single() is
                { ParameterId: "voltage", TargetId: "rail", Property: "supply.voltage" },
        "root-level analog authoring reopens typed ports, supplies and parameter bindings");
    var runtimeRootDirectory = Path.Combine(authoringDirectory, "root-authoring-runtime-project");
    Require(reopenedRootAuthoring.RemovePort(reopenedRootAuthoring.Ports.Single(port => port.Id == "sidechain")) &&
            reopenedRootAuthoring.RemoveAnalogCircuitPort(
                reopenedRootAuthoring.AnalogCircuit.Ports.Single(port => port.Id == "sidechain")) &&
            reopenedRootAuthoring.AnalogCircuit.Nodes.Remove("side") &&
            reopenedRootAuthoring.AnalogCircuit.Nodes.Remove("reserved") &&
            reopenedRootAuthoring.SaveToDirectory(runtimeRootDirectory),
        "root-level analog authoring can remove unconnected declared nodes and derive a runnable two-port package");
    using (var runtimeRootBackend = new BackendSession())
    {
        runtimeRootBackend.ValidateModulePackage(runtimeRootDirectory);
        Require(runtimeRootBackend.AuditionModulePackage(runtimeRootDirectory).OutputFinite,
            "two-port structured analog package with supply binding prepares and processes natively");
    }
    using (var savedRootCircuit = System.Text.Json.JsonDocument.Parse(
               File.ReadAllText(Path.Combine(rootAuthoringDirectory, "logic", "circuit.json"))))
    {
        var savedRoot = savedRootCircuit.RootElement;
        Require(savedRoot.GetProperty("nodes").EnumerateArray().Any(node => node.GetString() == "reserved") &&
                savedRoot.GetProperty("measurementBaseline").GetProperty("reference").GetString() == "contract" &&
                savedRoot.GetProperty("editorNote").GetString() == "preserve this extension" &&
                savedRoot.GetProperty("solver").GetProperty("diagnosticLabel").GetString() == "root-contract" &&
                savedRoot.GetProperty("solver").GetProperty("convergence").GetProperty("diagnosticMode").GetBoolean(),
            "structured analog serialization preserves declared nodes and unknown root/solver extensions");
    }

    var topologyAuthoring = new ModuleAuthoringDocument();
    topologyAuthoring.NewProject(ModuleAuthoringKind.AnalogCircuit);
    var seriesComponent = topologyAuthoring.AnalogCircuit.Components.Single(component => component.Id == "series");
    var loadComponent = topologyAuthoring.AnalogCircuit.Components.Single(component => component.Id == "load");
    Require(topologyAuthoring.DisconnectAnalogPin(loadComponent.Pins[0]) &&
            loadComponent.Pins[0].Node != seriesComponent.Pins[1].Node,
        "schematic authoring can detach one component pin into a new net");
    Require(topologyAuthoring.IsValid && topologyAuthoring.ValidationIssues.Any(issue =>
                issue is { IsWarning: true, Target: ModuleAuthoringIssueTarget.AnalogPin } &&
                ReferenceEquals(issue.TargetObject, loadComponent)),
        "single-pin schematic nets produce a targeted dangling-node warning without blocking save");
    Require(topologyAuthoring.ConnectAnalogPins(seriesComponent.Pins[1], loadComponent.Pins[0]) &&
            loadComponent.Pins[0].Node == seriesComponent.Pins[1].Node,
        "schematic authoring connects two pins by merging their circuit nets");
    Require(topologyAuthoring.Undo() &&
            topologyAuthoring.AnalogCircuit.Components.Single(component => component.Id == "load").Pins[0].Node != "out" &&
            topologyAuthoring.Undo() &&
            topologyAuthoring.AnalogCircuit.Components.Single(component => component.Id == "load").Pins[0].Node == "out",
        "pin connect and disconnect operations each occupy one document undo entry");
    var duplicatedComponent = topologyAuthoring.DuplicateAnalogComponent(
        topologyAuthoring.AnalogCircuit.Components.Single(component => component.Id == "series"));
    Require(topologyAuthoring.AnalogCircuit.Components.Count == 3 && duplicatedComponent.Id != "series" &&
            duplicatedComponent.CanvasX != seriesComponent.CanvasX && topologyAuthoring.Undo() &&
            topologyAuthoring.AnalogCircuit.Components.Count == 2,
        "schematic component duplication preserves content, offsets layout and remains undoable");
    var groupedComponents = topologyAuthoring.AnalogCircuit.Components.ToArray();
    var groupedOriginalPositions = groupedComponents.Select(component => (component.CanvasX, component.CanvasY)).ToArray();
    topologyAuthoring.BeginEditTransaction();
    for (var index = 0; index < groupedComponents.Length; ++index)
    {
        groupedComponents[index].CanvasX += 32.0;
        groupedComponents[index].CanvasY += 16.0;
    }
    topologyAuthoring.EndEditTransaction();
    Require(topologyAuthoring.Undo() && topologyAuthoring.AnalogCircuit.Components.Select((component, index) =>
                component.CanvasX == groupedOriginalPositions[index].CanvasX &&
                component.CanvasY == groupedOriginalPositions[index].CanvasY).All(restored => restored),
        "schematic multi-component movement is coalesced into one undo transaction");
}
finally
{
    if (Directory.Exists(authoringDirectory))
    {
        Directory.Delete(authoringDirectory, true);
    }
    if (Directory.Exists(authoringWorkflowDirectory))
    {
        Directory.Delete(authoringWorkflowDirectory, true);
    }
}

using var viewModel = new RigEditorViewModel();
Require(viewModel.Nodes.Count >= 2, "default snapshot exposes Drive and Delay nodes");
Require(viewModel.CanvasNodes.All(node => node.Controls.Count > 0 &&
    node.Controls.All(control => control.IsKnob && control.Maximum > control.Minimum)),
    "node cards expose parameter-backed knob controls with valid ranges");
Require(viewModel.CanvasNodes.All(node => node.Skin.Id == "solorig.default-pedal" &&
    node.Skin.KnobId == "standard-knob" && node.Skin.FootswitchId == "standard-footswitch"),
    "node snapshots expose the default shared pedal skin metadata");
Require(viewModel.Nodes.All(node => !node.Bypassed), "startup node snapshots expose enabled processing state");
var driveRange = viewModel.Nodes.Single(node => node.InstanceId == "drive-1").Parameters.Single(parameter => parameter.Id == "drive");
Require(driveRange.Minimum == 0.0F && driveRange.Maximum == 16.0F,
    "parameter snapshot exposes the validated Drive control range");
Require(viewModel.Nodes.All(node => node.EstimatedCpuPercent > 0), "per-node estimated performance is visible");
Require(viewModel.Nodes.All(node => !node.MeasurementAvailable), "startup keeps measured performance explicitly unavailable");
Require(viewModel.PerformanceStatus.Contains("实测未连接", StringComparison.Ordinal), "performance status distinguishes estimate from measurement");
Require(viewModel.Snapshot.Connections.Count == 1, "default snapshot exposes the Drive-to-Delay connection");
Require(viewModel.EffectChainNodes.Count == 2 &&
        viewModel.EffectChainNodes[0].InstanceId == "drive-1" &&
        viewModel.EffectChainNodes[1].InstanceId == "delay-1",
    "default snapshot exposes an ordered visible effect-chain projection");
Require(viewModel.SelectNode("delay-1") && viewModel.SelectedNode?.InstanceId == "delay-1",
    "effect-chain projection can select a node for inspector editing");
Require(viewModel.SelectedPorts.Count == 2 &&
        viewModel.SelectedPorts.Any(port => !port.IsOutput && port.Id == "input" && port.MaxChannels == 2U) &&
        viewModel.SelectedPorts.Any(port => port.IsOutput && port.Id == "output" && port.MaxChannels == 2U),
    "selected built-in node exposes descriptor-backed stereo input/output ports");
Require(viewModel.SelectedResource is { Kind: "Built-in module", Status: "Ready" } &&
        viewModel.SelectedResource.KindText == "内置模块",
    "selected built-in node exposes localized resource origin and readiness");
Require(viewModel.ClearSelection() && viewModel.SelectedNode is null,
    "effect-chain selection can return to the empty selection state");
Require(!viewModel.CanUndo && !viewModel.CanRedo, "startup document history is empty");
Require(viewModel.NodeNavigatorEntries.Count == viewModel.CanvasNodes.Count &&
        viewModel.FilteredNodeNavigatorEntries.Count == viewModel.CanvasNodes.Count &&
        viewModel.BatchSelectedNodeCount == 0 && !viewModel.CanBatchNodes,
    "project node navigator starts fully projected with no batch selection");
viewModel.NodeNavigatorSearchText = "drive-1";
Require(viewModel.FilteredNodeNavigatorEntries.Count == 1 &&
        viewModel.FilteredNodeNavigatorEntries[0].InstanceId == "drive-1" &&
        viewModel.NodeNavigatorSummaryText.Contains("1/2", StringComparison.Ordinal),
    "project node navigator filters by instance ID and reports the result count");
viewModel.SelectAllFilteredNodes();
Require(viewModel.BatchSelectedNodeCount == 1 && viewModel.CanBatchBypassNodes &&
        !viewModel.CanBatchEnableNodes,
    "select-all-results checks the visible enabled node and gates batch actions");
viewModel.NodeNavigatorSearchText = string.Empty;
viewModel.SelectAllFilteredNodes();
Require(viewModel.BatchSelectedNodeCount == 2 && viewModel.CanBatchNodes,
    "clearing search can add every project node to the batch selection");
Require(viewModel.SetBatchNodesBypassed(true) &&
        viewModel.Nodes.All(node => node.Bypassed) &&
        viewModel.NodeNavigatorEntries.All(entry => entry.Node.Bypassed && entry.IsBatchSelected) &&
        !viewModel.CanBatchBypassNodes && viewModel.CanBatchEnableNodes,
    "one shared transaction bypasses every selected project node and refreshes navigator state");
Require(viewModel.Undo() && viewModel.Nodes.All(node => !node.Bypassed),
    "one undo restores every node changed by the batch bypass transaction");
Require(viewModel.SetBatchNodesBypassed(true) && viewModel.SetBatchNodesBypassed(false) &&
        viewModel.Nodes.All(node => !node.Bypassed),
    "batch enable restores processing for all selected nodes");
Require(viewModel.Undo() && viewModel.Nodes.All(node => node.Bypassed) &&
        viewModel.Undo() && viewModel.Nodes.All(node => !node.Bypassed),
    "batch enable and bypass each occupy one undo history entry");
viewModel.ClearBatchNodeSelection();
Require(viewModel.BatchSelectedNodeCount == 0 && !viewModel.CanBatchNodes &&
        viewModel.ClearHistory() && !viewModel.IsDirty,
    "batch workflow restores a clean document and empty selection for later tests");
Require(viewModel.ProbeDpiScaling(), "shared geometry and hit testing remain aligned at 1x/1.25x/2x DPI");
Require(viewModel.MidiStatus.Contains("closed", StringComparison.OrdinalIgnoreCase), "startup MIDI remains closed");
Require(viewModel.AsioDrivers.Count == 0 && viewModel.MidiDevices.Count == 0,
    "startup keeps ASIO and MIDI enumeration explicit and side-effect free");
Require(!viewModel.AudioOpen && !viewModel.AudioRunning && !viewModel.HasAsioDrivers && !viewModel.HasSelectedAsio,
    "startup exposes an explicit closed ASIO lifecycle without a selected device");
Require(!viewModel.CanOpenAsio && !viewModel.CanStartAsio && !viewModel.CanStopAsio &&
        !viewModel.CanCloseAsio && !viewModel.CanRecoverAsio,
    "startup disables ASIO actions that have no valid device or lifecycle state");
Require(viewModel.CanEditAsioRoutes && viewModel.CanRefreshAsio,
    "closed ASIO lifecycle keeps route editing and device refresh available");
Require(!viewModel.MidiOpen && !viewModel.MidiLearnArmed && viewModel.MidiState == 0U,
    "startup exposes a closed MIDI lifecycle state");
Require(!viewModel.CanOpenMidi && !viewModel.CanCloseMidi && !viewModel.CanDrainMidi &&
        !viewModel.CanArmMidiLearn && !viewModel.CanCancelMidiLearn,
    "startup disables MIDI actions without an enumerated or open input");
Require(!viewModel.CanSelectMidiDevice && viewModel.CanRefreshMidi,
    "closed MIDI lifecycle keeps refresh available without enabling an empty selector");
var routedDriverFixtures = new[]
{
    new AsioDriverSnapshot("fixture-input", "Fixture Input", 4U, 2U, 44100.0),
    new AsioDriverSnapshot("fixture-output", "Fixture Output", 2U, 6U, 48000.0),
};
foreach (var route in viewModel.AsioInputRoutes.Concat(viewModel.AsioOutputRoutes))
{
    route.SetDrivers(routedDriverFixtures);
}
viewModel.AsioInputRoutes[0].SelectedDriverIndex = 0;
viewModel.AsioInputRoutes[0].SelectedDeviceChannel = 3U;
viewModel.AsioInputRoutes[1].SelectedDriverIndex = 1;
viewModel.AsioInputRoutes[1].SelectedDeviceChannel = 1U;
viewModel.AsioOutputRoutes[0].SelectedDriverIndex = 1;
viewModel.AsioOutputRoutes[0].SelectedDeviceChannel = 4U;
viewModel.AsioOutputRoutes[1].SelectedDriverIndex = 0;
viewModel.AsioOutputRoutes[1].SelectedDeviceChannel = 1U;
Require(viewModel.AsioInputRoutes.All(route => route.IsConfigured) &&
        viewModel.AsioOutputRoutes.All(route => route.IsConfigured),
    "ASIO route editor accepts independent physical channels for each input/output row");
Require(viewModel.AsioInputRoutes[0].ToSnapshot() == new AsioChannelRouteSnapshot(0U, 3U, 0U) &&
        viewModel.AsioInputRoutes[1].ToSnapshot() == new AsioChannelRouteSnapshot(1U, 1U, 1U) &&
        viewModel.AsioOutputRoutes[0].ToSnapshot() == new AsioChannelRouteSnapshot(1U, 4U, 0U) &&
        viewModel.AsioOutputRoutes[1].ToSnapshot() == new AsioChannelRouteSnapshot(0U, 1U, 1U),
    "ASIO route editor preserves cross-device driver indexes and processing-channel mappings");
var lockedRoute = new AsioRouteSelection("输入测试", 0U, static () => false);
lockedRoute.SetDrivers(routedDriverFixtures);
var lockedDriver = lockedRoute.SelectedDriverIndex;
var lockedChannel = lockedRoute.SelectedDeviceChannel;
lockedRoute.SelectedDriverIndex = lockedDriver == 0 ? 1 : 0;
lockedRoute.SelectedDeviceChannel = lockedChannel + 1U;
Require(!lockedRoute.IsEditable && !lockedRoute.CanEditDeviceChannel &&
        lockedRoute.SelectedDriverIndex == lockedDriver && lockedRoute.SelectedDeviceChannel == lockedChannel,
    "ASIO route selection rejects edits while its lifecycle is locked");
Require(viewModel.AsioRouteText.Contains("Fixture Input", StringComparison.Ordinal) &&
        viewModel.AsioRouteText.Contains("Fixture Output", StringComparison.Ordinal),
    "ASIO route summary exposes both independently selected devices");
viewModel.AsioOutputRoutes[1].SelectedDriverIndex = 1;
viewModel.AsioOutputRoutes[1].SelectedDeviceChannel = 4U;
Require(!viewModel.AsioRouteMapValid && viewModel.AsioRouteValidationText.Contains("重复", StringComparison.Ordinal),
    "ASIO route editor rejects duplicate physical output routes before opening hardware");
viewModel.AsioOutputRoutes[1].SelectedDriverIndex = 0;
viewModel.AsioOutputRoutes[1].SelectedDeviceChannel = 1U;
Require(viewModel.AsioRouteMapValid, "ASIO route editor restores a valid cross-device route map");
foreach (var route in viewModel.AsioInputRoutes.Concat(viewModel.AsioOutputRoutes))
{
    route.SetDrivers([]);
}
Require(!viewModel.TunerEnabled, "startup keeps the tuner disabled");
Require(!viewModel.TunerSignalPresent && viewModel.TunerMidiNote == -1 &&
        !viewModel.CanToggleTuner && viewModel.TunerReadout.Contains("ASIO", StringComparison.Ordinal),
    "startup exposes a structured tuner state and gates the tuner before audio is running");
Require(!viewModel.SetTunerEnabled(true) && viewModel.Status.Contains("ASIO", StringComparison.OrdinalIgnoreCase),
    "tuner enable is rejected before the standalone audio lifecycle is running");
Require(viewModel.MidiLearnTargets.Count >= 2, "MIDI learn exposes current parameter targets without opening hardware");
Require(!viewModel.ArmSelectedMidiLearn(), "MIDI learn rejects an unopened MIDI input");
Require(!viewModel.StartAsio(), "ASIO start is rejected before an explicit device open");
Require(!viewModel.RecoverAsio() && viewModel.Status.Contains("no ASIO", StringComparison.OrdinalIgnoreCase),
    "ASIO recovery without a pending fault reports an actionable diagnostic");
Require(viewModel.ModuleCatalog.Count >= 2, "module catalog exposes built-in modules");
Require(viewModel.ModuleCatalog.Any(module => module.ModuleId == "org.mumu-dada.solorig.drive"), "catalog exposes Drive metadata");
var builtInCatalogCount = viewModel.ModuleCatalog.Count;
Require(viewModel.ModuleCatalogFilter == "全部" && viewModel.FilteredModuleCatalog.Count == builtInCatalogCount,
    "module catalog starts unfiltered");
Require(viewModel.SetModuleCatalogFilter("模拟"), "analog catalog filter is accepted");
Require(viewModel.FilteredModuleCatalog.Count > 0 && viewModel.FilteredModuleCatalog.All(module => module.Category == "Analog"),
    "analog catalog filter only exposes analog modules");
Require(viewModel.SetModuleCatalogFilter("数字"), "digital catalog filter is accepted");
Require(viewModel.FilteredModuleCatalog.Count > 0 && viewModel.FilteredModuleCatalog.All(module => module.Category == "Digital"),
    "digital catalog filter only exposes digital modules");
Require(viewModel.SetModuleCatalogFilter("外部") && viewModel.FilteredModuleCatalog.Count == 0,
    "external catalog filter is empty before external import");
Require(!viewModel.SetModuleCatalogFilter("invalid"), "unknown catalog filter is rejected");
Require(viewModel.SetModuleCatalogFilter("全部"), "catalog can return to the all filter");
Require(viewModel.CanAddModule && !viewModel.CanConnectNodes && viewModel.CanDisconnectNodes,
    "graph actions require an explicit valid source/target pair while keeping disconnect available");
Require(!viewModel.CanDuplicateSelected && !viewModel.CanDeleteSelected,
    "node copy/delete gates stay disabled without a selected node");
viewModel.SetViewport(620, 430, 1.0);
viewModel.SelectedCatalogModule = viewModel.ModuleCatalog.First(module =>
    module.ModuleId == "org.mumu-dada.solorig.drive");
var buttonAddedIds = viewModel.CanvasNodes.Select(node => node.InstanceId).ToHashSet(StringComparer.Ordinal);
viewModel.AddModuleCommand.Execute(null);
var buttonAddedNode = viewModel.CanvasNodes.FirstOrDefault(node => !buttonAddedIds.Contains(node.InstanceId));
Require(buttonAddedNode is not null && buttonAddedNode.CanvasX >= 0.0 && buttonAddedNode.CanvasY >= 0.0 &&
        buttonAddedNode.CanvasX + buttonAddedNode.Width <= viewModel.Snapshot.ViewportWidth + 0.001 &&
        buttonAddedNode.CanvasY + buttonAddedNode.Height <= viewModel.Snapshot.ViewportHeight + 0.001,
    "button-added module is projected fully inside the visible canvas bounds");
viewModel.SetViewport(1280, 720, 1.0);
Require(!viewModel.CanDeleteMidiBinding && !viewModel.CanClearMidiBindings,
    "MIDI binding removal gates stay disabled without bindings");
Require(viewModel.SelectInspectorTab("端口") && viewModel.IsPortInspectorVisible && !viewModel.IsParameterInspectorVisible,
    "inspector port tab is selectable");
Require(viewModel.SelectInspectorTab("资源") && viewModel.IsResourceInspectorVisible,
    "inspector resource tab is selectable");
Require(!viewModel.SelectInspectorTab("invalid"), "unknown inspector tab is rejected");
Require(viewModel.SelectInspectorTab("参数") && viewModel.IsParameterInspectorVisible,
    "inspector returns to parameter tab");
viewModel.ConnectionSourceNodeId = "drive-1";
viewModel.ConnectionDestinationNodeId = "delay-1";
Require(!viewModel.CanConnectNodes && viewModel.ConnectionCandidateStatusText.Contains("已存在", StringComparison.Ordinal),
    "topology editor rejects a duplicate directed edge before calling the backend");
viewModel.ConnectionSourceNodeId = "delay-1";
viewModel.ConnectionDestinationNodeId = "drive-1";
Require(!viewModel.CanConnectNodes && viewModel.ConnectionCandidateStatusText.Contains("环路", StringComparison.Ordinal),
    "topology editor rejects a cycle before calling the backend");
viewModel.ConnectionDestinationNodeId = "delay-1";
Require(!viewModel.CanConnectNodes && viewModel.ConnectionCandidateStatusText.Contains("自身", StringComparison.Ordinal),
    "topology editor rejects self-connections");
viewModel.ConnectionSourceNodeId = null;
viewModel.ConnectionDestinationNodeId = null;
Require(viewModel.RealtimeContract.IsRealtimeSafe, "C ABI reports a C++-only, preallocated realtime boundary");
Require(viewModel.RealtimeContract.MaximumOversampling is >= 1 and <= 16, "realtime contract bounds oversampling");
Require(viewModel.RealtimeStatus.Contains("C++ safe", StringComparison.Ordinal), "realtime status is visible to the shared ViewModel");
Require(viewModel.WhiteboxStatus.Diagnostic.Contains("PedalKernel", StringComparison.Ordinal), "whitebox status keeps the clean-room boundary visible");

using (var lifecycleProbe = new BackendSession())
{
    Require(lifecycleProbe.ReadRealtimeContract().IsRealtimeSafe, "direct C ABI contract read succeeds");
    lifecycleProbe.Dispose();
    var disposedRejected = false;
    try
    {
        _ = lifecycleProbe.ReadWhiteboxStatus();
    }
    catch (ObjectDisposedException)
    {
        disposedRejected = true;
    }
    Require(disposedRejected, "disposed backend rejects further status reads");
}
using (var topologyProbe = new RigEditorViewModel())
{
    Require(topologyProbe.DisconnectNodes("drive-1", "delay-1") && topologyProbe.CanConnectNodes,
        "disconnecting an edge keeps its endpoints in the topology composer for fast reconnection");
    Require(topologyProbe.ConnectConnectionCandidate() &&
            topologyProbe.SelectedConnection is { SourceInstanceId: "drive-1", DestinationInstanceId: "delay-1" } &&
            !topologyProbe.CanConnectNodes,
        "topology composer creates and selects an explicit directed edge, then disables the duplicate action");
}
using (var hostProbe = new BackendSession())
{
    var invalidAsioRejected = false;
    try
    {
        hostProbe.OpenAsio(0, IntPtr.Zero, 0, 1, 0, 1);
    }
    catch (BackendException exception) when (exception.Status == NativeStatus.NotFound)
    {
        invalidAsioRejected = true;
    }
    Require(invalidAsioRejected, "ASIO open rejects an invalid driver index");

    var invalidMidiRejected = false;
    try
    {
        hostProbe.OpenMidi(0);
    }
    catch (BackendException)
    {
        invalidMidiRejected = true;
    }
    Require(invalidMidiRejected, "MIDI open rejects a device before enumeration");
}
Require(viewModel.AddModule("org.mumu-dada.solorig.drive", "drive-extra", 640, 320), "module add uses the C ABI transaction");
Require(viewModel.Snapshot.Nodes.Any(node => node.InstanceId == "drive-extra" && node.Parameters.Any(parameter => parameter.Id == "drive" && parameter.Value == 4.0F)),
    "added module receives validated defaults");
Require(viewModel.Snapshot.Connections.Any(connection => connection.DestinationInstanceId == "drive-extra"),
    "added module is appended to the visible effect chain");
Require(viewModel.EffectChainNodes.Any(node => node.InstanceId == "drive-extra") &&
        viewModel.EffectChainNodes[^1].InstanceId == "drive-extra",
    "button-added module is present in the ordered effect-chain projection");
Require(viewModel.AddModuleAtCanvasPoint("org.mumu-dada.solorig.digital-delay", 420, 420),
    "canvas drop creates a module at the requested editor point");
var droppedNode = viewModel.CanvasNodes.FirstOrDefault(node => node.InstanceId.StartsWith("digital-delay", StringComparison.Ordinal));
Require(droppedNode is not null && Math.Abs(droppedNode.CanvasX - 322.0) < 0.001 && Math.Abs(droppedNode.CanvasY - 343.0) < 0.001,
    "canvas drop exposes the new module at the requested canvas point");
Require(viewModel.DuplicateNode("drive-extra", "drive-copy"), "node duplication uses the C ABI transaction");
Require(viewModel.ConnectNodes("drive-extra", "drive-copy"), "serial connection edit succeeds");
Require(viewModel.Snapshot.Connections.Any(connection => connection.SourceInstanceId == "drive-extra"), "connection feedback is visible");
viewModel.NodeNavigatorSearchText = "drive-copy";
viewModel.SelectAllFilteredNodes();
Require(viewModel.BatchSelectedNodeCount == 1 &&
        viewModel.NodeNavigatorEntries.Any(entry => entry.InstanceId == "drive-copy" && entry.IsBatchSelected),
    "project node navigator can batch-select a newly duplicated node");
Require(viewModel.DeleteNode("drive-copy"), "node deletion removes the copied module");
Require(viewModel.BatchSelectedNodeCount == 0 && !viewModel.CanBatchNodes &&
        viewModel.FilteredNodeNavigatorEntries.Count == 0,
    "deleting a selected project node prunes its stale batch selection and search result");
viewModel.NodeNavigatorSearchText = string.Empty;
Require(viewModel.Snapshot.TotalLatencySamples > 0 && viewModel.Snapshot.EstimatedCpuPercent > 0, "performance snapshot is visible");
Require(viewModel.PresetNames.Contains("SoloRig Default"), "preset list exposes the in-memory default");
viewModel.NewPresetCommand.Execute(null);
Require(string.IsNullOrEmpty(viewModel.Snapshot.SourcePath) && !viewModel.IsDirty, "quick default preset switching stays in memory and clean");
Require(viewModel.FileStatus.Contains("unsaved default", StringComparison.Ordinal) &&
        !viewModel.FileStatus.Contains("*", StringComparison.Ordinal),
    "document status identifies the clean in-memory default");
Require(viewModel.SelectNode("drive-1"), "parameter editor can select the Drive node");
var driveEditor = viewModel.SelectedParameters.Single(parameter => parameter.Target == "drive-1.drive");
Require(driveEditor.DisplayName == "增益" && driveEditor.CoarseStep == 0.2F && driveEditor.FineStep == 0.02F &&
        driveEditor.NormalizedText == "25%",
    "parameter editor exposes localized labels, normalized values and stable nice steps");
Require(viewModel.SetParameterText("drive-1.drive", "50%") &&
        viewModel.SelectedParameters.Single(parameter => parameter.Target == "drive-1.drive").Value == 8.0F,
    "parameter text accepts a normalized percentage and maps it through the descriptor range");
Require(viewModel.NudgeParameter("drive-1.drive", 1, fine: true) &&
        Math.Abs(viewModel.SelectedParameters.Single(parameter => parameter.Target == "drive-1.drive").Value - 8.02F) < 0.0001F,
    "parameter fine nudge uses one tenth of the descriptor-derived coarse step");
Require(!viewModel.SetParameterText("drive-1.drive", "not-a-number") &&
        Math.Abs(viewModel.SelectedParameters.Single(parameter => parameter.Target == "drive-1.drive").Value - 8.02F) < 0.0001F,
    "invalid parameter text is rejected without changing the backend value");
Require(viewModel.SetParameter("drive-1.drive", 4.0F, markDirty: false) &&
        viewModel.RestoreDocumentState(false) && viewModel.ClearHistory(),
    "parameter workflow probe restores the clean default state and history");
Require(viewModel.SetParameter("drive-1.drive", 6.0F), "C ABI parameter edit succeeds");
Require(viewModel.CanUndo && !viewModel.CanRedo, "parameter edit enables undo and clears redo");
Require(viewModel.Undo(), "C ABI undo restores the previous parameter state");
Require(viewModel.Nodes.SelectMany(node => node.Parameters).Any(parameter => parameter.Id == "drive" && parameter.Value == 4.0F) &&
        !viewModel.IsDirty && viewModel.CanRedo,
    "undo restores the parameter value and clean document state");
Require(viewModel.Redo(), "C ABI redo reapplies the parameter edit");
Require(viewModel.Nodes.SelectMany(node => node.Parameters).Any(parameter => parameter.Id == "drive" && parameter.Value == 6.0F) &&
        viewModel.IsDirty && viewModel.CanUndo && !viewModel.CanRedo,
    "redo reapplies the value and dirty state");
Require(viewModel.Undo(), "second undo restores the clean parameter state");
Require(viewModel.ClearHistory() && !viewModel.CanUndo && !viewModel.CanRedo,
    "history can be cleared after a completed undo/redo round trip");
Require(viewModel.SetParameter("drive-1.drive", 6.0F), "parameter edit remains available after history reset");
Require(viewModel.Nodes.SelectMany(node => node.Parameters).Any(parameter => parameter.Id == "drive" && parameter.Value == 6.0F),
    "edited parameter is visible in the next snapshot");
Require(viewModel.IsDirty && viewModel.FileStatus.Contains("*", StringComparison.Ordinal) &&
        viewModel.WindowTitle.Contains("*", StringComparison.Ordinal),
    "document status exposes the dirty marker after an edit");
Require(!viewModel.SetParameter("drive-1.drive", 20.0F), "shared UI rejects a parameter value outside the descriptor range");
Require(viewModel.SelectPreset("SoloRig Default"), "preset selector activates the quick default action");
Require(viewModel.SelectedPresetName == "SoloRig Default" && !viewModel.IsDirty,
    "preset selector restores the clean default state");
Require(!viewModel.SelectPreset("missing-preset"), "preset selector rejects an unavailable preset");
Require(viewModel.SetParameter("drive-1.drive", 6.0F), "parameter edit after preset selection succeeds");
Require(viewModel.SelectAt(74.0, 158.0), "C ABI selects a node using editor coordinates");
Require(viewModel.CanDuplicateSelected && viewModel.CanDeleteSelected,
    "node copy/delete gates enable after selecting a node");
Require(viewModel.ClearSelection() && viewModel.SelectedNode is null &&
        !viewModel.CanDuplicateSelected && !viewModel.CanDeleteSelected,
    "clear selection resets node action gates");
Require(viewModel.SelectAt(74.0, 158.0), "node selection can be restored after clearing");
Require(viewModel.BeginDrag(74.0, 158.0), "C ABI begins a node drag");
Require(viewModel.DragTo(210.0, 232.0, true), "C ABI drags and snaps a node");
viewModel.EndDrag();
Require(viewModel.Nodes.Single(node => node.InstanceId == "drive-1").Selected, "dragged node remains selected");
Require(viewModel.ToggleTheme(), "C ABI theme command succeeds");
Require(!viewModel.Snapshot.DarkTheme, "theme change is visible in the snapshot");

var path = Path.Combine(Path.GetTempPath(), "solorig-m2-contract.srig");
var corruptPath = Path.Combine(Path.GetTempPath(), "solorig-m2-corrupt.srig");
var themePath = Path.Combine(Path.GetTempPath(), "solorig-m25-contract-theme.json");
var resourcePath = Path.Combine(Path.GetTempPath(), "solorig-m5-resource.nam");
var midiBankDirectory = Path.Combine(Path.GetTempPath(), "solorig-m2-midi-bank");
var presetLibraryDirectory = Path.Combine(Path.GetTempPath(), "solorig-m25-preset-library");
var skinPackageDirectory = Path.Combine(Path.GetTempPath(), "solorig-m25-skin-package");
try
{
    File.Delete(path);
    File.Delete(themePath);
    File.Delete(resourcePath);
    if (Directory.Exists(midiBankDirectory))
    {
        Directory.Delete(midiBankDirectory, true);
    }
    if (Directory.Exists(presetLibraryDirectory))
    {
        Directory.Delete(presetLibraryDirectory, true);
    }
    if (Directory.Exists(skinPackageDirectory))
    {
        Directory.Delete(skinPackageDirectory, true);
    }
    File.WriteAllText(corruptPath, "{not-json");
    File.WriteAllText(themePath, """
{
  "schemaVersion": 1,
  "id": "contract.external",
  "displayName": "Contract External",
  "colors": {
    "background": "#202A38",
    "surface": "#2D3A4A",
    "text": "#F8FAFC",
    "accent": "#F59E0B",
    "window": "#18212D",
    "canvas": "#1D2938"
  },
  "metrics": {
    "gridSize": 16,
    "cornerRadius": 8
  }
}
""");
    Require(viewModel.LoadTheme(themePath), "external theme loads through the shared C# ViewModel");
    Require(viewModel.Theme.Id == "contract.external" && viewModel.ThemeDisplayName == "Contract External",
        "external theme metadata reaches the shared UI");
    Require(viewModel.Theme.GetColor("accent") == "#F59E0B" && viewModel.Theme.GetColor("canvas") == "#1D2938",
        "external theme colors reach the shared UI palette");
    Require(viewModel.Snapshot.Dirty, "loading an external theme marks the preset dirty");
    var modelCandidates = new[]
    {
        Path.GetFullPath(Path.Combine(Directory.GetCurrentDirectory(), "third_party", "NeuralAmpModelerCore", "example_models", "wavenet.nam")),
        Path.GetFullPath(Path.Combine(Directory.GetCurrentDirectory(), "..", "third_party", "NeuralAmpModelerCore", "example_models", "wavenet.nam")),
        Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "third_party", "NeuralAmpModelerCore", "example_models", "wavenet.nam")),
        Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "third_party", "NeuralAmpModelerCore", "example_models", "wavenet.nam"))
    };
    var sourceModel = modelCandidates.FirstOrDefault(File.Exists);
    Require(!string.IsNullOrWhiteSpace(sourceModel), "NAM contract fixture is available");
    File.Copy(sourceModel!, resourcePath, true);
    Require(viewModel.ImportResource(resourcePath), "explicit resource import is recognized");
    Require(viewModel.Snapshot.ResourceStatus.Contains("NAM", StringComparison.Ordinal), "resource status identifies NAM");
    Require(viewModel.ModuleCatalog.Count > builtInCatalogCount, "resource import refreshes the module catalog");
    var namModule = viewModel.ModuleCatalog.Single(module => module.Category == "NAM");
    Require(viewModel.AddModule(namModule.ModuleId, "contract-nam-1", 760, 160),
        "imported NAM module can be added for inspector verification");
    Require(viewModel.SelectNode("contract-nam-1") && viewModel.SelectedResource is { Kind: "NAM", Status: "Ready" } &&
            string.Equals(viewModel.SelectedResource.Path, resourcePath, StringComparison.OrdinalIgnoreCase) &&
            viewModel.SelectedPorts.Count == 2,
        "NAM resource path, readiness and descriptor ports reach the selected-node inspector");
    Require(viewModel.SetModuleCatalogFilter("外部") && viewModel.FilteredModuleCatalog.Count > 0,
        "external catalog filter exposes the imported resource");
    Require(viewModel.SetModuleCatalogFilter("全部"), "catalog filter resets after external import");
    Directory.CreateDirectory(Path.Combine(skinPackageDirectory, "logic"));
    Directory.CreateDirectory(Path.Combine(skinPackageDirectory, "ui", "images"));
    File.WriteAllText(Path.Combine(skinPackageDirectory, "manifest.json"), """
{
  "schemaVersion": 1,
  "moduleId": "org.mumu-dada.solorig.contract-skin",
  "displayName": "Contract Skin",
  "category": "digital-algorithm",
  "logicFile": "logic/main.json",
  "skinFile": "ui/pedal.json",
  "ports": [
    {"id":"input","direction":"input","maxChannels":1},
    {"id":"output","direction":"output","maxChannels":1}
  ],
  "parameters": [{"id":"gain","minimum":0,"maximum":2,"default":0.5}]
}
""");
    File.WriteAllText(Path.Combine(skinPackageDirectory, "logic", "main.json"),
        "{\"schemaVersion\":1,\"kind\":\"digital-algorithm\",\"algorithm\":\"linear-gain\",\"operations\":[{\"type\":\"gain\",\"parameter\":\"gain\"}]}" );
    File.WriteAllText(Path.Combine(skinPackageDirectory, "ui", "pedal.json"), """
{
  "schemaVersion": 1,
  "id": "contract.image-skin",
  "controls": {
    "knob": {"id":"contract-knob","image":"images/knob.bmp"},
    "toggle": {"id":"contract-toggle","image":"images/toggle.bmp"},
    "button": "contract-button",
    "footswitch": {"id":"contract-footswitch","image":"images/footswitch.bmp"}
  }
}
""");
    File.WriteAllBytes(Path.Combine(skinPackageDirectory, "ui", "images", "knob.bmp"), OneByOneBmp());
    File.WriteAllBytes(Path.Combine(skinPackageDirectory, "ui", "images", "toggle.bmp"), OneByOneBmp());
    File.WriteAllBytes(Path.Combine(skinPackageDirectory, "ui", "images", "footswitch.bmp"), OneByOneBmp());
    const string skinPackageModuleId = "org.mumu-dada.solorig.contract-skin";
    Require(viewModel.ImportResource(skinPackageDirectory), "external package with a custom skin imports");
    Require(viewModel.AddModule(skinPackageModuleId, "contract-skin-1", 900, 160),
        "custom-skin module can be added to the shared graph");
    var skinNode = viewModel.CanvasNodes.Single(node => node.InstanceId == "contract-skin-1");
    Require(skinNode.Skin.Id == "contract.image-skin" && skinNode.Skin.KnobId == "contract-knob" &&
            skinNode.Controls.All(control => control.HasImage) && skinNode.HasFootswitchImage,
        "custom skin metadata and image paths reach shared node cards");
    Require(viewModel.SelectNode("contract-skin-1") &&
            viewModel.SelectedPorts.Count == 2 && viewModel.SelectedPorts.All(port => port.MaxChannels == 1U) &&
            viewModel.SelectedResource is { Kind: "External package", Status: "Ready" } &&
            viewModel.SelectedResource.Path.Replace('\\', '/').EndsWith("logic/main.json", StringComparison.OrdinalIgnoreCase),
        $"external package ports and logic resource details reach the shared inspector; " +
        $"selected={viewModel.SelectedNode?.InstanceId ?? "none"}, " +
        $"ports={string.Join(",", viewModel.SelectedPorts.Select(port => port.DisplayText))}, " +
        $"resource={viewModel.SelectedResource?.Kind}|{viewModel.SelectedResource?.Path}|{viewModel.SelectedResource?.Status}");
    Require(viewModel.ToggleNodeBypass("drive-1"), "footswitch command toggles node bypass through the C ABI");
    Require(viewModel.Nodes.Single(node => node.InstanceId == "drive-1").Bypassed &&
            viewModel.CanvasNodes.Single(node => node.InstanceId == "drive-1").FootswitchText == "旁路",
        "bypass state reaches the shared node card projection");
    Require(viewModel.SaveToPath(path), "explicit Save writes a validated preset");
    Require(!viewModel.IsDirty, "successful Save clears dirty state");
    Directory.CreateDirectory(presetLibraryDirectory);
    File.Copy(path, Path.Combine(presetLibraryDirectory, "01-default.srig"), true);
    File.Copy(path, Path.Combine(presetLibraryDirectory, "02-default-copy.srig"), true);
    File.WriteAllText(Path.Combine(presetLibraryDirectory, "99-invalid.srig"), "{not-json");
    Require(viewModel.LoadPresetLibrary(presetLibraryDirectory), "explicit preset library directory loads");
    Require(viewModel.PresetLibraryEntries.Count == 3 && viewModel.PresetLibraryEntries.Count(entry => entry.IsValid) == 2,
        "preset library scans valid and invalid .srig files with deterministic ordering");
    var invalidLibraryEntry = viewModel.PresetLibraryEntries.Single(entry => !entry.IsValid);
    Require(!viewModel.OpenPresetLibraryEntry(invalidLibraryEntry), "invalid preset library entry is rejected before load");
    var validLibraryEntry = viewModel.PresetLibraryEntries.First(entry => entry.IsValid);
    Require(viewModel.OpenPresetLibraryEntry(validLibraryEntry), "selected preset library entry opens through the shared backend");
    Require(viewModel.Snapshot.SourcePath == validLibraryEntry.SourcePath && !viewModel.IsDirty,
        "preset library load updates the current source path and keeps a clean document");
    Directory.CreateDirectory(midiBankDirectory);
    File.Copy(path, Path.Combine(midiBankDirectory, "01-default.srig"), true);
    Require(viewModel.ImportMidiBank(midiBankDirectory), "MIDI Bank directory import uses the C ABI");
    Require(viewModel.Snapshot.Bindings.Any(binding => binding.MessageType == 1U && binding.TargetKind == 1U),
        "MIDI Bank import creates a Program Change preset binding");
    Require(viewModel.DeleteSelectedMidiBinding(), "selected MIDI binding can be deleted from C# UI state");
    Require(viewModel.ClearMidiBindings(), "MIDI bindings can be cleared from C# UI state");
    Require(viewModel.Snapshot.Bindings.Count == 0, "clearing MIDI bindings updates the shared snapshot");
    var savedLayout = viewModel.Nodes.Single(node => node.InstanceId == "drive-1").LayoutX;
    Require(viewModel.SetParameter("drive-1.drive", 5.0F), "post-save edit succeeds");
    var beforeFailure = viewModel.Snapshot;
    Require(!viewModel.OpenFromPath(corruptPath), "corrupt Open is rejected");
    Require(viewModel.Snapshot.SourcePath == beforeFailure.SourcePath, "failed Open keeps source path");
    Require(viewModel.IsDirty == beforeFailure.Dirty, "failed Open keeps dirty state");
    Require(viewModel.Snapshot.DarkTheme == beforeFailure.DarkTheme, "failed Open keeps theme state");
    Require(viewModel.Nodes.Single(node => node.InstanceId == "drive-1").LayoutX == savedLayout,
        "failed Open keeps the edited layout");
    Require(viewModel.OpenFromPath(path), "valid Open restores the saved preset");
    Require(viewModel.Nodes.Single(node => node.InstanceId == "drive-1").LayoutX == savedLayout,
        "Open restores the saved layout");
    Require(viewModel.Nodes.SelectMany(node => node.Parameters).Any(parameter => parameter.Id == "drive" && parameter.Value == 6.0F),
        "Open restores the saved parameter");
    Require(viewModel.Nodes.Single(node => node.InstanceId == "drive-1").Bypassed,
        "Open restores the saved node bypass state");
    var beforeCancel = viewModel.Snapshot;
    viewModel.ReportFileDialogCancelled("Open");
    Require(viewModel.Snapshot.SourcePath == beforeCancel.SourcePath && viewModel.IsDirty == beforeCancel.Dirty,
        "cancelled dialog keeps source and dirty state");
}
finally
{
    File.Delete(path);
    File.Delete(corruptPath);
    File.Delete(themePath);
    File.Delete(resourcePath);
    if (Directory.Exists(midiBankDirectory))
    {
        Directory.Delete(midiBankDirectory, true);
    }
    if (Directory.Exists(presetLibraryDirectory))
    {
        Directory.Delete(presetLibraryDirectory, true);
    }
    if (Directory.Exists(skinPackageDirectory))
    {
        Directory.Delete(skinPackageDirectory, true);
    }
}

Console.WriteLine("[PASS] Shared C# ViewModel performs catalog, graph editing, performance, resource and preset workflow");

static void Require(bool condition, string description)
{
    if (!condition)
    {
        throw new InvalidOperationException(description);
    }
}

static bool Throws<TException>(Action action) where TException : Exception
{
    try
    {
        action();
        return false;
    }
    catch (TException)
    {
        return true;
    }
}

static void WritePcm16Wave(string path, uint sampleRate, ushort channels, IReadOnlyList<short> samples)
{
    if (samples.Count % channels != 0) throw new ArgumentException("interleaved sample count is invalid", nameof(samples));
    using var stream = File.Create(path);
    using var writer = new BinaryWriter(stream);
    var dataSize = checked((uint)samples.Count * 2U);
    writer.Write("RIFF"u8.ToArray());
    writer.Write(36U + dataSize);
    writer.Write("WAVE"u8.ToArray());
    writer.Write("fmt "u8.ToArray());
    writer.Write(16U);
    writer.Write((ushort)1);
    writer.Write(channels);
    writer.Write(sampleRate);
    writer.Write(sampleRate * channels * 2U);
    writer.Write((ushort)(channels * 2U));
    writer.Write((ushort)16);
    writer.Write("data"u8.ToArray());
    writer.Write(dataSize);
    foreach (var sample in samples) writer.Write(sample);
}

static byte[] OneByOneBmp() =>
[
    0x42, 0x4D, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00,
    0x28, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA5, 0xD6, 0x4D, 0x00
];
