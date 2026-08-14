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

节点、端口、供电和元件 ID 在整个网表中必须唯一，所有引脚必须引用已声明节点。下表包含 M18-C2/C3/C4 clean-room BJT、JFET 和 MOSFET 主线路径；它不代表完整模型数据库、具体器件或白盒等价能力已经交付。

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
| `bjt` | 支持 | 支持 clean-room Ebers-Moll Newton | 三引脚 `[collector, base, emitter]`；兼容 inline `bjtModel`，或由 `ModelDatabase` 按 BJT 类型/极性绑定的安全字符串引用；`initial` 是基极节点电压初始猜测 |
| `jfet` | 支持 | 支持 clean-room square-law Newton | 三引脚 `[drain, gate, source]`；兼容 inline `jfetModel`，或由 `ModelDatabase` 按 JFET 类型/极性绑定的安全字符串引用；`initial` 是栅极节点电压初始猜测 |
| `mosfet` | 支持 | 支持 clean-room enhancement square-law Newton | 三引脚 `[drain, gate, source]`；兼容 inline `mosfetModel`，或由 `ModelDatabase` 按 MOSFET 类型/极性绑定的安全字符串引用；`initial` 是栅极节点电压初始猜测 |
| `switch` | 支持契约解析 | `UnsupportedComponent` | 状态列表/初态会校验，运行时尚未提供控制邮箱 |

电感采用 branch-current MNA 形式：每个 backward-Euler 步求解 `v+ - v- - (L/dt)i = -(L/dt)i_previous`，并在 `Reset`/重复 `Prepare` 时恢复声明的初始电流。电容使用 companion conductance 与电压历史；`voltage-source`、`supplies` 和理想运放使用额外 MNA branch unknown。电位器在 Compile 阶段按归一化位置展开为端点到 wiper、wiper 到端点的两个电阻，位置扫动通过重新 Compile/Prepare 完成；端点的零段以极小正阻值保持矩阵可解。VCCS 直接印章四端线性跨导矩阵；理想运放印章有限增益 VCVS，带限幅时在 Prepare 后的预分配 Newton 工作区内选择线性/饱和分支。已发布的 runtime 没有实时旋钮或受控源参数更新入口，`SetParameter` 会明确拒绝该更新。矩阵奇异、非有限输入/结果、Newton 不收敛会返回稳定错误；未知器件、未知控制状态、缺失模型不会静默退化。

编译和 `Prepare` 阶段完成节点/分支/历史索引、矩阵和所有工作缓冲的分配；音频 `Process` 只复用这些缓冲，不读文件、不解析 JSON、不动态分配。外部 `module.json` 引用 v2 `logic/*.json` 时，加载器在非实时线程完成解析和编译，`MnaCircuitRuntime::Prepare` 为每个声道复制并准备状态，`Process` 实际调用统一 MNA 求解器。

BJT 使用统一 `EvaluateNonlinearBjt` 的三端 Ebers-Moll 电流和完整显式 Jacobian：`forwardBeta`/`reverseBeta` 转换为正反向 transport alpha，`polarity` 决定 NPN/PNP 的结电压方向。指数自变量限制在 `[-60, 60]`，结电流限制在 `maxCurrent`，钳位分支的 Jacobian 与返回电流保持一致；Newton 的 BE/BC 结电压步长由 `maxStep` 阻尼。超过 `maxIterations` 或矩阵奇异时返回确定的 `NonConvergent`/`SingularMatrix`。审计字符串引用只在 `CompileMnaCircuit(circuit, ModelDatabase)` 或 `CreateModuleRuntime(package, ModelDatabase)` 的控制线程/Prepare 边界绑定为 POD，普通编译稳定返回 `MissingModelAuthorization`。该 clean-room v1 不包含结电容、Early 效应、热反馈、噪声、击穿或商业器件参数。

JFET 使用统一 `EvaluateNonlinearJfet` 的三端对称 square-law 沟道电流和完整显式 Jacobian：`polarity` 选择 N/P channel 镜像，正向与反向沟道按 drain/source 方向对称处理，cutoff、线性区和饱和区均有独立有限差分证据。`maxCurrent` 在平方乘法前钳位，钳位分支导数为零并与返回电流一致；`maxStep` 对 VGS/VDS Newton 步长阻尼。审计引用在同一 Prepare 边界绑定为 POD。该 clean-room v1 的 gate current 为零，不包含 PN gate junction、电容/电荷、沟道长度调制、击穿、温度、自热、噪声或商业参数。

MOSFET 使用统一 `EvaluateNonlinearMosfet` 的三端对称 enhancement-mode square-law 沟道电流和完整显式 Jacobian：`thresholdVoltage` 定义正的阈值幅度，`polarity` 选择 N/P channel 镜像，正向与反向沟道按 drain/source 方向对称处理，cutoff、triode 和 saturation 区均有独立有限差分证据。`maxCurrent` 在平方乘法前钳位，钳位分支导数为零并与返回电流一致；`maxStep` 对 VGS/VDS Newton 步长阻尼。审计引用在同一 Prepare 边界绑定为 POD。该 clean-room v1 的 gate current 为零，不包含 body/bulk terminal、body diode/effect、亚阈值、沟道长度调制、寄生电容/电荷、击穿、温度、自热、噪声或商业参数。

模块加载器对旧的 `schemaVersion: 1` + `kind: "analog-circuit"` 保持原受限 RC/二极管适配器；v2 读取根对象的 `version: 2`。未知版本、缺失字段、重复 ID、非法节点/引脚、范围值、缺失模型和危险路径均拒绝加载。v1 不会被静默转换为 v2，v2 也不会回退到旧硬编码拓扑。

M18-C1 的 Zener 使用 self-authored 双指数静态数学契约，参数包括正向饱和电流/热电压、反向击穿电压/电流/斜率和最大 Newton 步长；这不是商业器件参数。安全字符串引用只有通过 `CompileMnaCircuit(circuit, ModelDatabase)` 才能在 Prepare 阶段绑定，普通编译稳定返回 `MissingModelAuthorization`；inline 模型对象不经过数据库，必须按本规范范围校验且只代表 generic clean-room 数学模型。

本版本仍不宣称通用器件覆盖、真实设备 1:1 校准、实时参数控制电位器、开关/受控源控制、非理想运放芯片、真空管、结电容、Early/热效应、供电下垂或测量基线自动校准；这些属于后续独立范围。M18-C2/C3/C4 分别完成 generic BJT/JFET/MOSF 主线化，但不构成 PedalKernel 等价或完整白盒完成。
