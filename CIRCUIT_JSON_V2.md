# `circuit.json` v2 白盒网表契约

`logic/circuit.json` 只描述白盒电路；模块元属性仍由 `module.json`/`manifest.json` 提供，数字算法仍使用 `logic/*.json`，外观仍使用 `ui/pedal.json`。`.srig` 只保存效果链和实例状态，不内嵌电路实现。

## 根对象

必须包含 `version: 2`、`schema: "circuit"`、`referenceGround`、非空 `nodes`、非空 `ports`、非空 `components` 和 `solver`。`supplies`、`parameterBindings`、`measurementBaseline` 以及未来扩展字段可选；未知字段被忽略但不会改变已验证字段的含义。

```json
{
  "version": 2,
  "schema": "circuit",
  "referenceGround": "0",
  "nodes": ["in", "out", "0"],
  "ports": [
    {"id": "input", "direction": "input", "node": "in"},
    {"id": "output", "direction": "output", "node": "out"}
  ],
  "supplies": [
    {"id": "supply", "positive": "vcc", "negative": "0", "voltage": 9.0}
  ],
  "components": [
    {"id": "r1", "type": "resistor", "pins": ["in", "out"], "value": 1000.0,
     "tolerance": 0.05, "initial": 0.0},
    {"id": "c1", "type": "capacitor", "pins": ["out", "0"], "value": 0.000001}
  ],
  "solver": {
    "method": "modified-nodal-analysis",
    "integration": "backward-euler",
    "oversampling": 1,
    "convergence": {
      "maxIterations": 8,
      "absoluteTolerance": 1e-9,
      "relativeTolerance": 1e-6
    }
  }
}
```

节点、端口、供电和元件 ID 在整个网表中必须唯一，所有引脚必须引用已声明节点。下表包含 M18-C2 至 C8a 已交付的 clean-room 主线路径；它不代表完整模型数据库、具体器件、非线性磁芯或白盒等价能力已经交付。

| 类型 | v2 解析 | MNA Compile/Prepare/Solve | 状态/模型 |
| --- | --- | --- | --- |
| `resistor` | 支持 | 支持 | 正值欧姆数，无动态状态 |
| `capacitor` | 支持 | 支持 | backward-Euler 电压历史，`initial` 为伏特 |
| `inductor` | 支持 | 支持 | backward-Euler 分支电流历史，`value` 为亨利、`initial` 为安培 |
| `voltage-source` | 支持 | 支持 | 额外 MNA branch unknown，`value` 为伏特 |
| `diode` | 支持 | 支持 | M18-C1 静态 IV/Jacobian + 有界 Newton；C9a 可选 generic 结电容、静态温度/漏电修正和 BE 结电压历史；inline `shockley`/`zener` 或经 `ModelDatabase` 审计的安全字符串引用 |
| `potentiometer` | 支持 | 支持静态及 D1a 实时位置 | `value` 为总阻值；`potentiometer.position/minimum/maximum` 定义位置，`wiperNode` 或第三引脚定义滑片；Compile 时展开为两个预编译电阻 |
| `vccs` | 支持 | 支持 | 四引脚 `[out+, out-, control+, control-]`；`controlledSource.kind=vccs`、`modelVersion=1`、有限 `gain`，可选电流限幅 |
| `ideal-op-amp` | 支持 | 支持基础理想 VCVS | 四引脚 `[out+, out-, non-inverting, inverting]`；有限正 `gain`，可选输出电压限幅；不是非理想芯片模型 |
| `opamp` | 支持 | 支持 clean-room bounded v1 与 generic-dynamic v2 Newton/BE | 四引脚 `[out+, out-, non-inverting, inverting]`；inline `amplifierModel.kind=bounded-opamp`/`generic-dynamic` 或经 `ModelDatabase` 审计的安全字符串引用 |
| `ota` | 支持 | 支持 clean-room bounded transconductance-amplifier Newton | 四引脚 `[out+, out-, non-inverting, inverting]`；inline `amplifierModel.kind=bounded-ota` 或经 `ModelDatabase` 审计的安全字符串引用 |
| `bjt` | 支持 | 支持 clean-room Ebers-Moll 与 C9b generic-dynamic Newton | 三引脚 `[collector, base, emitter]`；兼容 inline `bjtModel`，或由 `ModelDatabase` 按 BJT 类型/极性绑定的安全字符串引用；`initial` 是基极节点电压初始猜测，并作为 BE/BC 两路共同的极性归一化历史初态 |
| `jfet` | 支持 | 支持 clean-room square-law 与 C9c generic-dynamic Newton | 三引脚 `[drain, gate, source]`；兼容 inline `jfetModel`，或由 `ModelDatabase` 按 JFET 类型/极性绑定的安全字符串引用；`initial` 是栅极节点电压初始猜测，并作为 GS/GD 两路共同历史初态 |
| `mosfet` | 支持 | 支持 clean-room enhancement square-law 与 C9d generic-dynamic Newton | 三引脚 `[drain, gate, source]`；body 永久 tied to source，不暴露第四端；兼容 inline `mosfetModel`，或由 `ModelDatabase` 按 MOSFET 类型/极性绑定的安全字符串引用；`initial` 是两路 GS/GD 历史共同初态 |
| `triode` | 支持 | 支持 clean-room space-charge Newton | 三引脚 `[plate, grid, cathode]`；兼容 inline `triodeModel`，或由 `ModelDatabase` 按 triode 类型绑定的安全字符串引用；`initial` 是栅极节点电压初始猜测 |
| `pentode` | 支持 | 支持 clean-room space-charge/screen-share Newton | 四引脚 `[plate, screen, grid, cathode]`；兼容 inline `pentodeModel`，或由 `ModelDatabase` 按 pentode 类型绑定的安全字符串引用；`initial` 是栅极节点电压初始猜测 |
| `beam-tetrode` | 支持 | 支持 clean-room beam-capture Newton | 四引脚 `[plate, screen, grid, cathode]`；兼容 inline `beamTetrodeModel.kind=beam-capture`，或由 `ModelDatabase` 按 beam-tetrode 类型绑定的安全字符串引用；`initial` 是栅极节点电压初始猜测 |
| `vari-mu` | 支持 | 支持 clean-room remote-cutoff Newton | 三引脚 `[plate, grid, cathode]`；兼容 inline `variMuModel.kind=remote-cutoff`，或由 `ModelDatabase` 按 vari-mu 类型绑定的安全字符串引用；`initial` 是栅极节点电压初始猜测 |
| `coupled-inductor` | 支持 | 支持 clean-room lossy-linear 双支路 MNA | 四引脚 `[primary-dot, primary, secondary-dot, secondary]`；inline `coupledInductorModel.kind=lossy-linear` 或经 `ModelDatabase` 审计的安全字符串引用；`primaryInitialCurrent`/`secondaryInitialCurrent` 单位为安培 |
| `speaker-load` | 支持 | 支持 clean-room electrodynamic branch/Newton/BE | 两引脚 `[positive, negative]`；inline `speakerModel.kind=electrodynamic` 或经 `ModelDatabase` 审计的安全字符串引用；保存 coil current、cone velocity 和 displacement 三路历史 |
| `relay` | 支持 | D1b1 generic dynamic 单线圈/单常开触点 | 四引脚 `[coil+, coil-, contact+, contact-]`；inline `relayModel.kind=generic-dynamic`，线圈 branch BE 与有限触点状态在每个收敛子步更新 |
| `switch` | 支持 | 支持 D1a 两端有限电阻状态 | 动态绑定要求 `states=["open","closed"]`；`openResistance/closedResistance` 为正有限欧姆数，`initial` 为 0/1 状态索引 |

电感采用 branch-current MNA 形式：每个 backward-Euler 步求解 `v+ - v- - (L/dt)i = -(L/dt)i_previous`，并在 `Reset`/重复 `Prepare` 时恢复声明的初始电流。电容使用 companion conductance 与电压历史；`voltage-source`、`supplies` 和理想运放使用额外 MNA branch unknown。电位器在 Compile 阶段按归一化位置展开为端点到 wiper、wiper 到端点的两个电阻，端点的零段以极小正阻值保持矩阵可解；无 binding 的旧静态结果保持兼容。VCCS 直接印章四端线性跨导矩阵；理想运放印章有限增益 VCVS，带限幅时在 Prepare 后的预分配 Newton 工作区内选择线性/饱和分支。矩阵奇异、非有限输入/结果、Newton 不收敛会返回稳定错误；未知器件、未知控制状态、缺失模型不会静默退化。

## M18-D1a 实时参数绑定

根级 `parameterBindings` 每项必须提供 `modelVersion: 1`、module descriptor 中已声明的 `parameter`、网表 `target`、`property` 和 `smoothingSeconds`。D1a 只接受 `potentiometer.position`、`switch.state`、`supply.voltage`；重复 parameter、重复 target/property、未知 target、类型错配、未声明参数和超出 target 安全范围的 descriptor 范围均拒绝。

`MnaCircuitRuntime` 使用固定 64 槽原子目标邮箱。`SetParameter` 仅执行字符串查找、有限值/范围检查和 atomic publish；音频 `Process` 在样本边界读取目标，并在每个 oversampling 子步以 `alpha=(dt/tau)/(1+dt/tau)` 推进当前值，`tau=0` 在下一子步立即应用。该路径不锁、不分配、不解析 JSON、不访问文件或重新编译。

电位器位置按 `(position-minimum)/(maximum-minimum)` 更新两个预编译正电阻。switch 的 `state=0/1` 分别为 `openResistance/closedResistance`，平滑期间只在两个正有限端点间插值，不通过删除 stamp 或零电阻改变拓扑。`supply.voltage` 只更新 nominal source；C10a Sag/ripple 历史继续独立演化。Reset 恢复网表声明的控制初值，runtime 在下一样本重新应用邮箱目标。D1a 不包含 relay、bounce/hysteresis、多刀路由、subcircuit 跨网络或行为模块。

M18-D1b1 `relay` 的 `relayModel` v1 要求 `coilResistance` (ohm)、`coilInductance` (H)、`openResistance/closedResistance` (ohm，且 `Ropen>Rclosed>0`)、`pullInCurrent/dropOutCurrent` (A，且 `0<=dropOut<pullIn`)、`operateTime/releaseTime` (s)、`bounceDuration` (s)、`bounceFrequency` (Hz)、`bounceAmplitude` (0..1)、`maximumCoilCurrent` (A) 和 `initialState` (`open/closed`)。线圈端口满足 `v=Rcoil*i+Lcoil*(i-iprev)/dt`；Schmitt 目标只在外层 Newton 收敛后更新，机械位置按对应时间常数做 BE 推进。触点下一子步电阻为 `Ropen+x*(Rclosed-Ropen)`，bounce 是状态翻转后有界衰减正弦位置扰动。Reset 恢复声明触点状态、零线圈电流和无 bounce。该模型不含多刀/转换触点、接点电弧/电容、商业 relay 或 subcircuit 路由。

M18-D1b3 `relayModel.kind=generic-multi-contact, modelVersion=2` 扩展为 1..4 个共享线圈的 SPDT pole。pins 固定为 `[coil+, coil-, common1, NC1, NO1, ...]`，数量严格为 `2+3*poleCount`；每个 pole 的三节点必须互异。新增 `poleCount` (1..4) 和 `contactGap` (归一化行程比例，0..0.9)，`initialState` 为 `released/operated`。令 `lower=0.5-contactGap/2`、`upper=0.5+contactGap/2`，position 在 gap 内两侧均使用 `openResistance`，gap 外分别把 common-NC 或 common-NO 从 `openResistance` 线性过渡到 `closedResistance`，形成 break-before-make。所有 pole 共享同一 BE 线圈/机械/bounce 历史，Reset/oversampling 与 v1 一致；该 v2 不实现任意开关矩阵、锁存、互锁、接点电弧/电容、商业 relay 或 SPICE/1:1。

## M18-D1b2 inline passive subcircuit

白盒 topology compiler 接受受限的 inline passive subcircuit。实例必须提供唯一 `id`、至少两个唯一局部 `ports`、完整且一一映射的 `portMap`，以及 `inline.nodes/components`。局部端口必须同时出现在 `inline.nodes`，map 的父节点必须由顶层 `nodes` 声明；不存在隐式 ground，接地只能把某个局部端口显式映射到顶层 `referenceGround`。

```json
{
  "id": "tone-stage",
  "ports": ["input", "output", "ground"],
  "portMap": {"input": "in", "output": "out", "ground": "0"},
  "inline": {
    "nodes": ["input", "mid", "output", "ground"],
    "components": [
      {"id": "r", "kind": "resistor", "pins": ["input", "mid"], "value": 470.0},
      {"id": "c", "kind": "capacitor", "pins": ["mid", "ground"],
       "value": 0.000001, "initial": 0.0},
      {"id": "l", "kind": "inductor", "pins": ["mid", "output"],
       "value": 0.01, "initial": 0.0}
    ]
  }
}
```

编译器用 `instance-id::local-id` 确定性前缀重命名内部节点和器件，再把它们作为普通 R/C/L 写入父 graph；已展开实例不会进入运行时 boundary。R/C/L 的单位、backward-Euler 历史、过采样和 Reset 与顶层器件完全相同。单实例最多 256 个局部节点和 256 个局部器件，展开后总器件数最多 256。

legacy `ports`（父节点数组）加 `modelReference` 只保留审计元数据，仍以 `CrossNetworkBoundary` 拒绝执行。inline 与外部引用混用、递归/nested subcircuit、缺失或额外映射、重复父节点映射、未知 pin、主动/非线性/多端内容、非正或非有限值、ID/展开容量冲突均在 Compile 阶段拒绝。D1b2 不读取外部路径，不代表任意 macro-model、SPICE subcircuit 或商业电路支持。

`supplies` 缺省为原有 `kind=ideal, modelVersion=1`，其端口电压仍严格等于 `voltage`。M18-C10a 可选 `kind=rectified-reservoir, modelVersion=1`，并要求 `sourceResistance`/`sagResistance` (ohm)、`recoveryTime` (s)、`rippleAmplitude`/`maximumSag`/`initialSag` (V)、`rippleFrequency` (Hz) 和 `maximumLoadCurrent` (A)。电源 branch current 从 positive 到 negative 为正；向负载供电时电流为负。每个 oversampling 子步以 backward-Euler 更新有界 Sag，按相位生成单一余弦纹波，解析电流导数进入同一 Newton branch stamp；`Reset` 恢复 `initialSag` 和零相位。该等效模型描述整流后的储能节点，不包含逐二极管桥式换相、mains transformer、浪涌、调压环、商业电源参数或 SPICE/1:1。

编译和 `Prepare` 阶段完成节点/分支/历史索引、矩阵和所有工作缓冲的分配；音频 `Process` 只复用这些缓冲，不读文件、不解析 JSON、不动态分配。外部 `module.json` 引用 v2 `logic/*.json` 时，加载器在非实时线程完成解析和编译，`MnaCircuitRuntime::Prepare` 为每个声道复制并准备状态，`Process` 实际调用统一 MNA 求解器。

BJT 使用统一三端 evaluator：`kind=ebers-moll, modelVersion=1` 保持旧六参数结果；`kind=generic-dynamic, modelVersion=2` 增加 Early 有限输出因子、BE/BC depletion companion、静态温度修正和完整显式 3x3 Jacobian。动态字段单位为 `earlyVoltage(V)`、两路 `*JunctionCapacitance(F)`、两路 `*JunctionPotential(V)`、两路 `*GradingCoefficient(1)`、`temperatureCelsius(degC)` 和 `leakageTemperatureCoefficient(1/K)`，范围与项目契约一致。BE/BC 历史在每个 oversampling 子步收敛后更新，Reset 恢复共同的 `initial` 极性归一化初态；超过 `maxIterations` 或矩阵奇异时返回确定的 `NonConvergent`/`SingularMatrix`。审计字符串引用只在 `CompileMnaCircuit(circuit, ModelDatabase)` 或 `CreateModuleRuntime(package, ModelDatabase)` 的控制线程/Prepare 边界绑定为 POD，普通编译稳定返回 `MissingModelAuthorization`。该 clean-room 模型不包含 charge-storage/reverse-recovery、击穿、self-heating、噪声、SOA、商业器件参数或 SPICE/1:1 等价。

JFET 的 `kind=square-law, modelVersion=1` 保持旧四参数结果；`kind=generic-dynamic, modelVersion=2` 增加有限 channel-length modulation、温度修正、GS/GD PN gate leakage 与 depletion companion。动态字段单位为 `channelLengthModulation(1/V)`、`temperatureCelsius(degC)`、`gateSaturationCurrent(A)`、`gateThermalVoltage(V)`、`gateJunctionCapacitance(F)`、`gateJunctionPotential(V)`、`gateGradingCoefficient(1)` 和 `gateLeakageTemperatureCoefficient(1/K)`，范围与 C9c 契约一致。两路历史在每个 oversampling 子步收敛后更新，Reset 恢复共同 `initial` 极性归一化初态；审计引用在同一 Prepare 边界绑定为 POD。该 clean-room 模型不包含 avalanche、charge-storage/reverse-recovery、self-heating、噪声、商业参数或 SPICE/1:1。

MOSFET v1 `enhancement-square-law` 保持旧四参数结果。C9d `generic-dynamic` v2 在三端 POD 上增加有限 channel-length modulation、连续有界 subthreshold、温度修正、source-tied body-to-drain PN 支路和 GS/GD depletion companion；沟道（含 subthreshold/CLM）保持 drain/source 对称，body diode 按 source-body 到 drain 的固定方向产生预期非对称。动态字段分别为 `channelLengthModulation` (1/V)、`subthresholdCurrent` (A)、`subthresholdSlopeVoltage` (V)、`temperatureCelsius` (degC)、`bodyDiodeSaturationCurrent` (A)、`bodyDiodeThermalVoltage` (V)、`gateJunctionCapacitance` (F)、`gateJunctionPotential` (V) 和 `gateGradingCoefficient` (1)，范围见项目契约。所有支路进入同一解析 3x3 Jacobian/Newton 印章；审计引用在 Prepare 绑定并在实时路径只使用 POD。该 clean-room v2 不包含 bulk 第四端、body-effect、Miller/charge partition、avalanche、self-heating、SOA、噪声、商业参数或 SPICE/1:1。

M18-C5 的 `opamp`/`ota` 共用 `EvaluateNonlinearAmplifier` 有界传输 evaluator。运放把差分控制电压乘以 `gain` 后限制到 `minimumOutput`/`maximumOutput`，并以 branch residual 约束输出端电压；OTA 把差分控制电压映射为同样有界的输出电流并注入输出端 KCL。线性分支对正/负控制端的 Jacobian 分别为 `+gain`/`-gain`，硬饱和分支导数为零；乘法前按输入阈值判定饱和，极端有限输入不会先发生增益乘法溢出。`maxStep` 对差分控制电压的 Newton 步长阻尼。审计引用只在 Compile/Prepare 边界绑定为 POD；旧 `ideal-op-amp`/`vccs` 仍是 M9 兼容原语，不作为审计模型证据。

M18-C9e 为 `opamp` 增加 `amplifierModel.kind=generic-dynamic`、`modelVersion=2`。除既有 `gain`、`minimumOutput`、`maximumOutput`、`maxStep` 外，必须提供 `gainBandwidth` (Hz)、`slewRate` (V/s)、`inputResistance` (ohm)、`inputBiasCurrent` (A)、`inputOffset` (V)、`outputResistance` (ohm)、`outputCurrentLimit` (A)、`supplyMinimum`/`supplyMaximum` (V)。四端端电流使用有限输入阻抗和平衡偏置支路；输出采用内部 BE 主极点/slew 状态驱动有限 `Rout` Norton 支路并限流。`initial` 表示输出差分状态初值。供电字段仅是内部输出状态边界，不是显式供电脚，也不表示 PSRR/CMRR 或商业运放宏模型。

M18-C6 的 `triode` 使用统一 `EvaluateNonlinearTriode` 三端 space-charge evaluator。控制量为 `Vgk + Vpk / amplificationFactor`；只有 `Vpk > 0` 且控制量为正时，plate current 才是 `perveance * control^(3/2)`，grid current 为零，cathode current 与 plate current 相反。完整 3x3 Jacobian、KCL 和独立中心有限差分一致；`maxCurrent` 在幂运算前钳位且钳位分支导数为零，`maxStep` 对 Vgk/Vpk Newton 步长阻尼。审计引用只在 Compile/Prepare 边界绑定为 POD。该 generic v1 不包含 heater/filament、grid conduction/leakage、极间电容/电荷、独立高阶 plate resistance、secondary emission、温度/自热、老化、microphonics、噪声或商业管参数。

M18-C7a 的 `pentode` 使用统一 `EvaluateNonlinearPentode` 四端 space-charge/screen-share evaluator。控制量为 `Vgk + Vsk / screenAmplificationFactor`，总空间电流为 `perveance * control^(3/2)`，plate share 为 `1 - exp(-Vpk / kneeVoltage)`；只有 `Vpk > 0`、`Vsk > 0` 且控制量为正时导通。plate current 是扣除 `screenCurrentFraction` 后的总电流乘以 plate share，screen current 是总电流的固定分量，grid current 为零，cathode current 保持四端 KCL。完整 4x4 Jacobian 与独立中心有限差分一致；总空间电流在幂运算前受 `maxCurrent` 限制，钳位后 screen/control 导数为零，但未饱和的 plate-knee 导数继续与返回 plate current 一致；`maxStep` 对 Vpk/Vsk/Vgk Newton 步长阻尼。审计引用只在 Compile/Prepare 边界绑定为 POD。该 generic v1 不包含商业管型、beam tetrode/vari-mu、真实 suppressor-grid/beam-forming、secondary emission、反向导通、grid conduction/leakage、heater/filament、极间电容/电荷、温度/自热、老化、microphonics 或噪声。

M18-C7b 的 `beam-tetrode` 使用独立 `EvaluateNonlinearBeamTetrode` 四端 beam-capture evaluator。控制量为 `Vgk + Vsk / screenAmplificationFactor`，可用空间电流为 `perveance * control^(3/2)`；令 `x = Vpk / kneeVoltage`，plate capture 为 `x^2 / (1 + x^2)`，plate current 为总空间电流乘以 capture，screen current 为 `screenCurrentFraction * spaceCurrent * (1 - capture)`，grid current 为零，cathode current 保持四端 KCL。只有 `Vpk > 0`、`Vsk > 0` 且控制量为正时导通。完整 4x4 Jacobian 与独立中心有限差分一致；`maxCurrent` 在幂运算前限制空间电流，钳位后控制量导数为零而 capture 导数继续与返回电流一致；极端有限 `Vpk/kneeVoltage` 使用归一化分支避免平方溢出，`maxStep` 对 Vpk/Vsk/Vgk Newton 步长阻尼。审计引用只在 Compile/Prepare 边界绑定为 POD。该平方有理 capture 曲线只是 SoloRig 自有 generic beam-knee 数学契约，不包含商业管型、真实 beam-forming/suppressor-grid、secondary-emission kink、反向导通、grid conduction/leakage、heater/filament、极间电容/电荷、温度/自热、老化、microphonics 或噪声。

M18-C7c 的 `vari-mu` 使用独立 `EvaluateNonlinearVariMu` 三端 remote-cutoff evaluator。定义 `control = Vgk + Vpk / amplificationFactor`、`bias = max(-Vgk, 0)` 和 `remoteScale = 1 / sqrt(1 + (remoteCutoffCoefficient * bias)^2)`；只有 `Vpk > 0` 且 control 为正时导通，plate current 为 `perveance * remoteScale * control^(3/2)`，grid current 为零，cathode current 与 plate current 相反。完整 3x3 Jacobian、KCL 和独立中心有限差分一致；归一化电压差、remote-scale 双分支及 log-domain 电流判定避免极端有限输入在限流前溢出，`maxCurrent` 硬钳位后 Jacobian 为零，`maxStep` 对 Vpk/Vgk Newton 步长阻尼。审计引用只在 Compile/Prepare 边界绑定为 POD。该 generic v1 不包含商业管型、真实双节匹配、screen/suppressor/beam 物理、grid conduction/leakage、heater/filament、极间电容/电荷、热/老化/噪声、sidechain/envelope detector、attack/release 或压缩器时间常数。

M18-C8a 的 `coupled-inductor` 是四端有损线性双绕组。定义 `n=Np/Ns`、`L1=Lm+Llp`、`L2=Lm/n^2+Lls`、`M=Lm/n`；dot 端固定正互感，需要反相时交换任一绕组引脚。每个 backward-Euler 子步预分配两个 branch unknown，分别印章绕组电阻、`L1/dt`、`L2/dt` 和对称 `M/dt` 交叉项，并在子步结束更新两路历史电流；`Reset`/重复 `Prepare` 恢复两个声明初态。模型必须提供正 `turnsRatio`/`magnetizingInductance`、非负两路 leakage/resistance，且 `[[L1,M],[M,L2]]` 严格正定，因此两路漏感不能同时为零。该 v1 只是固定参数 lossy-linear coupled-inductor，不包含饱和、磁滞、剩磁、涡流/频变损耗、寄生电容、温度、自热、商业磁芯或商业变压器参数。

M18-C8b 的 `coupled-inductor` 还接受 `kind=saturating-hysteretic`：沿用同一四端语义和 C8a 绕组参数，新增 `saturationFlux`、`hysteresisWidth`、`hysteresisRate`、`remanentFlux`、`maxFlux`、`maxCurrent`、`maxStep`。其核心曲线为 `flux = saturationFlux * tanh(magnetizingInductance * magnetizingCurrent / saturationFlux) + hysteresis`，所以零点解析 `dflux/dcurrent` 为 `magnetizingInductance`；该必填参数同时进入 Newton 动态印章，不得静默失效。每个 backward-Euler 子步更新共享磁通/磁滞状态，`Reset` 恢复剩磁；它仍不代表真实材料回线、商业变压器、SPICE 等价或 1:1 设备。

M18-C10b `speaker-load` 的端口电流从 positive 流向 negative 为正。`speakerModel` v1 必须提供 `voiceCoilResistance` (ohm)、`voiceCoilInductance` (H)、`forceFactor` (N/A)、`movingMass` (kg)、`mechanicalResistance` (N*s/m)、`compliance` (m/N)、`nonlinearStiffness` (N/m)、`nonlinearDisplacementScale` (m)、三项 maximum 和三项对应 `initial*`。每个子步联合求解 `x=xprev+dt*u`、机械质量/阻尼/顺性/有界 tanh suspension 和电端口 `v=Re*i+Le*(i-iprev)/dt+Bl*u`，解析 `dv/di` 进入 branch Newton stamp，收敛后提交三路状态，Reset 恢复独立初态。该 generic 两端模型可与现有反馈输出级在同一 MNA 中求解，但不包含 cone breakup、多模态辐射、箱体/端口声学、热压缩、商业扬声器或完整电源-功放能量闭环。

模块加载器对旧的 `schemaVersion: 1` + `kind: "analog-circuit"` 保持原受限 RC/二极管适配器；v2 读取根对象的 `version: 2`。未知版本、缺失字段、重复 ID、非法节点/引脚、范围值、缺失模型和危险路径均拒绝加载。v1 不会被静默转换为 v2，v2 也不会回退到旧硬编码拓扑。

M18-C1 的 Zener 使用 self-authored 双指数静态数学契约，参数包括正向饱和电流/热电压、反向击穿电压/电流/斜率和最大 Newton 步长；这不是商业器件参数。安全字符串引用只有通过 `CompileMnaCircuit(circuit, ModelDatabase)` 才能在 Prepare 阶段绑定，普通编译稳定返回 `MissingModelAuthorization`；inline 模型对象不经过数据库，必须按本规范范围校验且只代表 generic clean-room 数学模型。

M18-C9a 的 inline diode/Zener 模型可增加 `junctionCapacitance` (F)、`junctionPotential` (V)、`gradingCoefficient` (1)、`temperatureCelsius` (degC)、`leakageCurrent` (A) 和 `leakageTemperatureCoefficient` (1/K)。温度限于 -100 至 200 degC；25 degC 为参考。结电容为 `Cj0 * max(1-v/Vj, 0.05)^(-m)`，BE companion 保存正引脚到负引脚的前一子步结电压，`initial`/`Reset` 使用同一方向。每个过采样子步收敛后更新历史；动态电流和解析 Jacobian 进入同一 Newton 印章。默认新增字段保持 C1 静态兼容。该模型不包含 reverse-recovery、电荷存储、噪声、自热、商业器件或 SPICE 等价。

本版本仍不宣称通用器件覆盖、真实设备 1:1 校准、实时参数控制电位器、开关/受控源控制、商业运放芯片、商业真空管、真实 beam-forming、reverse-recovery、BJT/JFET/MOSFET 更高阶寄生/热效应、逐器件整流电源或测量基线自动校准；这些属于后续独立范围。M18-C2 至 C10a 只完成相应 self-authored generic 主线化，不构成 PedalKernel、SPICE、完整输出变压器或完整白盒等价。
