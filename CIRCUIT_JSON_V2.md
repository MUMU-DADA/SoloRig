# `circuit.json` v2 白盒网表契约

`logic/circuit.json` 只描述白盒电路；模块元属性仍由 `module.json`/`manifest.json` 提供，数字算法仍使用 `logic/*.json`，外观仍使用 `ui/pedal.json`。`.srig` 只保存效果链和实例状态，不内嵌电路实现。

## 根对象

必须包含 `version: 2`、`schema: "circuit"`、`referenceGround`、非空 `nodes`、非空 `ports`、非空 `components` 和 `solver`。`supplies`、`measurementBaseline` 以及未来扩展字段可选；未知字段被忽略但不会改变已验证字段的含义。

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
| `diode` | 支持 | 支持 | M18-C1 统一 IV/Jacobian + 有界 Newton；inline `shockley`/`zener` clean-room 数学模型，或经 `ModelDatabase` 审计的安全字符串引用 |
| `potentiometer` | 支持 | 支持静态位置 | `value` 为总阻值；`potentiometer.position/minimum/maximum` 定义位置，`wiperNode` 或第三引脚定义滑片；Compile 时展开为两个预编译电阻 |
| `vccs` | 支持 | 支持 | 四引脚 `[out+, out-, control+, control-]`；`controlledSource.kind=vccs`、`modelVersion=1`、有限 `gain`，可选电流限幅 |
| `ideal-op-amp` | 支持 | 支持基础理想 VCVS | 四引脚 `[out+, out-, non-inverting, inverting]`；有限正 `gain`，可选输出电压限幅；不是非理想芯片模型 |
| `opamp` | 支持 | 支持 clean-room bounded voltage-amplifier Newton | 四引脚 `[out+, out-, non-inverting, inverting]`；inline `amplifierModel.kind=bounded-opamp` 或经 `ModelDatabase` 审计的安全字符串引用 |
| `ota` | 支持 | 支持 clean-room bounded transconductance-amplifier Newton | 四引脚 `[out+, out-, non-inverting, inverting]`；inline `amplifierModel.kind=bounded-ota` 或经 `ModelDatabase` 审计的安全字符串引用 |
| `bjt` | 支持 | 支持 clean-room Ebers-Moll Newton | 三引脚 `[collector, base, emitter]`；兼容 inline `bjtModel`，或由 `ModelDatabase` 按 BJT 类型/极性绑定的安全字符串引用；`initial` 是基极节点电压初始猜测 |
| `jfet` | 支持 | 支持 clean-room square-law Newton | 三引脚 `[drain, gate, source]`；兼容 inline `jfetModel`，或由 `ModelDatabase` 按 JFET 类型/极性绑定的安全字符串引用；`initial` 是栅极节点电压初始猜测 |
| `mosfet` | 支持 | 支持 clean-room enhancement square-law Newton | 三引脚 `[drain, gate, source]`；兼容 inline `mosfetModel`，或由 `ModelDatabase` 按 MOSFET 类型/极性绑定的安全字符串引用；`initial` 是栅极节点电压初始猜测 |
| `triode` | 支持 | 支持 clean-room space-charge Newton | 三引脚 `[plate, grid, cathode]`；兼容 inline `triodeModel`，或由 `ModelDatabase` 按 triode 类型绑定的安全字符串引用；`initial` 是栅极节点电压初始猜测 |
| `pentode` | 支持 | 支持 clean-room space-charge/screen-share Newton | 四引脚 `[plate, screen, grid, cathode]`；兼容 inline `pentodeModel`，或由 `ModelDatabase` 按 pentode 类型绑定的安全字符串引用；`initial` 是栅极节点电压初始猜测 |
| `beam-tetrode` | 支持 | 支持 clean-room beam-capture Newton | 四引脚 `[plate, screen, grid, cathode]`；兼容 inline `beamTetrodeModel.kind=beam-capture`，或由 `ModelDatabase` 按 beam-tetrode 类型绑定的安全字符串引用；`initial` 是栅极节点电压初始猜测 |
| `vari-mu` | 支持 | 支持 clean-room remote-cutoff Newton | 三引脚 `[plate, grid, cathode]`；兼容 inline `variMuModel.kind=remote-cutoff`，或由 `ModelDatabase` 按 vari-mu 类型绑定的安全字符串引用；`initial` 是栅极节点电压初始猜测 |
| `coupled-inductor` | 支持 | 支持 clean-room lossy-linear 双支路 MNA | 四引脚 `[primary-dot, primary, secondary-dot, secondary]`；inline `coupledInductorModel.kind=lossy-linear` 或经 `ModelDatabase` 审计的安全字符串引用；`primaryInitialCurrent`/`secondaryInitialCurrent` 单位为安培 |
| `switch` | 支持契约解析 | `UnsupportedComponent` | 状态列表/初态会校验，运行时尚未提供控制邮箱 |

电感采用 branch-current MNA 形式：每个 backward-Euler 步求解 `v+ - v- - (L/dt)i = -(L/dt)i_previous`，并在 `Reset`/重复 `Prepare` 时恢复声明的初始电流。电容使用 companion conductance 与电压历史；`voltage-source`、`supplies` 和理想运放使用额外 MNA branch unknown。电位器在 Compile 阶段按归一化位置展开为端点到 wiper、wiper 到端点的两个电阻，位置扫动通过重新 Compile/Prepare 完成；端点的零段以极小正阻值保持矩阵可解。VCCS 直接印章四端线性跨导矩阵；理想运放印章有限增益 VCVS，带限幅时在 Prepare 后的预分配 Newton 工作区内选择线性/饱和分支。已发布的 runtime 没有实时旋钮或受控源参数更新入口，`SetParameter` 会明确拒绝该更新。矩阵奇异、非有限输入/结果、Newton 不收敛会返回稳定错误；未知器件、未知控制状态、缺失模型不会静默退化。

编译和 `Prepare` 阶段完成节点/分支/历史索引、矩阵和所有工作缓冲的分配；音频 `Process` 只复用这些缓冲，不读文件、不解析 JSON、不动态分配。外部 `module.json` 引用 v2 `logic/*.json` 时，加载器在非实时线程完成解析和编译，`MnaCircuitRuntime::Prepare` 为每个声道复制并准备状态，`Process` 实际调用统一 MNA 求解器。

BJT 使用统一 `EvaluateNonlinearBjt` 的三端 Ebers-Moll 电流和完整显式 Jacobian：`forwardBeta`/`reverseBeta` 转换为正反向 transport alpha，`polarity` 决定 NPN/PNP 的结电压方向。指数自变量限制在 `[-60, 60]`，结电流限制在 `maxCurrent`，钳位分支的 Jacobian 与返回电流保持一致；Newton 的 BE/BC 结电压步长由 `maxStep` 阻尼。超过 `maxIterations` 或矩阵奇异时返回确定的 `NonConvergent`/`SingularMatrix`。审计字符串引用只在 `CompileMnaCircuit(circuit, ModelDatabase)` 或 `CreateModuleRuntime(package, ModelDatabase)` 的控制线程/Prepare 边界绑定为 POD，普通编译稳定返回 `MissingModelAuthorization`。该 clean-room v1 不包含结电容、Early 效应、热反馈、噪声、击穿或商业器件参数。

JFET 使用统一 `EvaluateNonlinearJfet` 的三端对称 square-law 沟道电流和完整显式 Jacobian：`polarity` 选择 N/P channel 镜像，正向与反向沟道按 drain/source 方向对称处理，cutoff、线性区和饱和区均有独立有限差分证据。`maxCurrent` 在平方乘法前钳位，钳位分支导数为零并与返回电流一致；`maxStep` 对 VGS/VDS Newton 步长阻尼。审计引用在同一 Prepare 边界绑定为 POD。该 clean-room v1 的 gate current 为零，不包含 PN gate junction、电容/电荷、沟道长度调制、击穿、温度、自热、噪声或商业参数。

MOSFET 使用统一 `EvaluateNonlinearMosfet` 的三端对称 enhancement-mode square-law 沟道电流和完整显式 Jacobian：`thresholdVoltage` 定义正的阈值幅度，`polarity` 选择 N/P channel 镜像，正向与反向沟道按 drain/source 方向对称处理，cutoff、triode 和 saturation 区均有独立有限差分证据。`maxCurrent` 在平方乘法前钳位，钳位分支导数为零并与返回电流一致；`maxStep` 对 VGS/VDS Newton 步长阻尼。审计引用在同一 Prepare 边界绑定为 POD。该 clean-room v1 的 gate current 为零，不包含 body/bulk terminal、body diode/effect、亚阈值、沟道长度调制、寄生电容/电荷、击穿、温度、自热、噪声或商业参数。

M18-C5 的 `opamp`/`ota` 共用 `EvaluateNonlinearAmplifier` 有界传输 evaluator。运放把差分控制电压乘以 `gain` 后限制到 `minimumOutput`/`maximumOutput`，并以 branch residual 约束输出端电压；OTA 把差分控制电压映射为同样有界的输出电流并注入输出端 KCL。线性分支对正/负控制端的 Jacobian 分别为 `+gain`/`-gain`，硬饱和分支导数为零；乘法前按输入阈值判定饱和，极端有限输入不会先发生增益乘法溢出。`maxStep` 对差分控制电压的 Newton 步长阻尼。审计引用只在 Compile/Prepare 边界绑定为 POD；旧 `ideal-op-amp`/`vccs` 仍是 M9 兼容原语，不作为审计模型证据。

M18-C6 的 `triode` 使用统一 `EvaluateNonlinearTriode` 三端 space-charge evaluator。控制量为 `Vgk + Vpk / amplificationFactor`；只有 `Vpk > 0` 且控制量为正时，plate current 才是 `perveance * control^(3/2)`，grid current 为零，cathode current 与 plate current 相反。完整 3x3 Jacobian、KCL 和独立中心有限差分一致；`maxCurrent` 在幂运算前钳位且钳位分支导数为零，`maxStep` 对 Vgk/Vpk Newton 步长阻尼。审计引用只在 Compile/Prepare 边界绑定为 POD。该 generic v1 不包含 heater/filament、grid conduction/leakage、极间电容/电荷、独立高阶 plate resistance、secondary emission、温度/自热、老化、microphonics、噪声或商业管参数。

M18-C7a 的 `pentode` 使用统一 `EvaluateNonlinearPentode` 四端 space-charge/screen-share evaluator。控制量为 `Vgk + Vsk / screenAmplificationFactor`，总空间电流为 `perveance * control^(3/2)`，plate share 为 `1 - exp(-Vpk / kneeVoltage)`；只有 `Vpk > 0`、`Vsk > 0` 且控制量为正时导通。plate current 是扣除 `screenCurrentFraction` 后的总电流乘以 plate share，screen current 是总电流的固定分量，grid current 为零，cathode current 保持四端 KCL。完整 4x4 Jacobian 与独立中心有限差分一致；总空间电流在幂运算前受 `maxCurrent` 限制，钳位后 screen/control 导数为零，但未饱和的 plate-knee 导数继续与返回 plate current 一致；`maxStep` 对 Vpk/Vsk/Vgk Newton 步长阻尼。审计引用只在 Compile/Prepare 边界绑定为 POD。该 generic v1 不包含商业管型、beam tetrode/vari-mu、真实 suppressor-grid/beam-forming、secondary emission、反向导通、grid conduction/leakage、heater/filament、极间电容/电荷、温度/自热、老化、microphonics 或噪声。

M18-C7b 的 `beam-tetrode` 使用独立 `EvaluateNonlinearBeamTetrode` 四端 beam-capture evaluator。控制量为 `Vgk + Vsk / screenAmplificationFactor`，可用空间电流为 `perveance * control^(3/2)`；令 `x = Vpk / kneeVoltage`，plate capture 为 `x^2 / (1 + x^2)`，plate current 为总空间电流乘以 capture，screen current 为 `screenCurrentFraction * spaceCurrent * (1 - capture)`，grid current 为零，cathode current 保持四端 KCL。只有 `Vpk > 0`、`Vsk > 0` 且控制量为正时导通。完整 4x4 Jacobian 与独立中心有限差分一致；`maxCurrent` 在幂运算前限制空间电流，钳位后控制量导数为零而 capture 导数继续与返回电流一致；极端有限 `Vpk/kneeVoltage` 使用归一化分支避免平方溢出，`maxStep` 对 Vpk/Vsk/Vgk Newton 步长阻尼。审计引用只在 Compile/Prepare 边界绑定为 POD。该平方有理 capture 曲线只是 SoloRig 自有 generic beam-knee 数学契约，不包含商业管型、真实 beam-forming/suppressor-grid、secondary-emission kink、反向导通、grid conduction/leakage、heater/filament、极间电容/电荷、温度/自热、老化、microphonics 或噪声。

M18-C7c 的 `vari-mu` 使用独立 `EvaluateNonlinearVariMu` 三端 remote-cutoff evaluator。定义 `control = Vgk + Vpk / amplificationFactor`、`bias = max(-Vgk, 0)` 和 `remoteScale = 1 / sqrt(1 + (remoteCutoffCoefficient * bias)^2)`；只有 `Vpk > 0` 且 control 为正时导通，plate current 为 `perveance * remoteScale * control^(3/2)`，grid current 为零，cathode current 与 plate current 相反。完整 3x3 Jacobian、KCL 和独立中心有限差分一致；归一化电压差、remote-scale 双分支及 log-domain 电流判定避免极端有限输入在限流前溢出，`maxCurrent` 硬钳位后 Jacobian 为零，`maxStep` 对 Vpk/Vgk Newton 步长阻尼。审计引用只在 Compile/Prepare 边界绑定为 POD。该 generic v1 不包含商业管型、真实双节匹配、screen/suppressor/beam 物理、grid conduction/leakage、heater/filament、极间电容/电荷、热/老化/噪声、sidechain/envelope detector、attack/release 或压缩器时间常数。

M18-C8a 的 `coupled-inductor` 是四端有损线性双绕组。定义 `n=Np/Ns`、`L1=Lm+Llp`、`L2=Lm/n^2+Lls`、`M=Lm/n`；dot 端固定正互感，需要反相时交换任一绕组引脚。每个 backward-Euler 子步预分配两个 branch unknown，分别印章绕组电阻、`L1/dt`、`L2/dt` 和对称 `M/dt` 交叉项，并在子步结束更新两路历史电流；`Reset`/重复 `Prepare` 恢复两个声明初态。模型必须提供正 `turnsRatio`/`magnetizingInductance`、非负两路 leakage/resistance，且 `[[L1,M],[M,L2]]` 严格正定，因此两路漏感不能同时为零。该 v1 只是固定参数 lossy-linear coupled-inductor，不包含饱和、磁滞、剩磁、涡流/频变损耗、寄生电容、温度、自热、商业磁芯或商业变压器参数。

模块加载器对旧的 `schemaVersion: 1` + `kind: "analog-circuit"` 保持原受限 RC/二极管适配器；v2 读取根对象的 `version: 2`。未知版本、缺失字段、重复 ID、非法节点/引脚、范围值、缺失模型和危险路径均拒绝加载。v1 不会被静默转换为 v2，v2 也不会回退到旧硬编码拓扑。

M18-C1 的 Zener 使用 self-authored 双指数静态数学契约，参数包括正向饱和电流/热电压、反向击穿电压/电流/斜率和最大 Newton 步长；这不是商业器件参数。安全字符串引用只有通过 `CompileMnaCircuit(circuit, ModelDatabase)` 才能在 Prepare 阶段绑定，普通编译稳定返回 `MissingModelAuthorization`；inline 模型对象不经过数据库，必须按本规范范围校验且只代表 generic clean-room 数学模型。

本版本仍不宣称通用器件覆盖、真实设备 1:1 校准、实时参数控制电位器、开关/受控源控制、真实非理想/商业运放芯片、商业真空管、真实 beam-forming、非线性 transformer core、结电容、Early/热效应、供电下垂或测量基线自动校准；这些属于后续独立范围。M18-C2 至 C8a 已完成相应 generic 主线化，但不构成 PedalKernel 等价、完整输出变压器或完整白盒完成。
