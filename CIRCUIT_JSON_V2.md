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

节点、端口、供电和元件 ID 在整个网表中必须唯一，所有引脚必须引用已声明节点。M7 的能力矩阵如下：

| 类型 | v2 解析 | MNA Compile/Prepare/Solve | 状态/模型 |
| --- | --- | --- | --- |
| `resistor` | 支持 | 支持 | 正值欧姆数，无动态状态 |
| `capacitor` | 支持 | 支持 | backward-Euler 电压历史，`initial` 为伏特 |
| `inductor` | 支持 | 支持 | backward-Euler 分支电流历史，`value` 为亨利、`initial` 为安培 |
| `voltage-source` | 支持 | 支持 | 额外 MNA branch unknown，`value` 为伏特 |
| `diode` | 支持 | 支持 | 有界 Shockley Newton；字符串 `silicon`/`shockley` 或 inline 模型对象 |
| `potentiometer` | 支持契约解析 | `UnsupportedComponent` | 位置/范围会校验，运行时尚未表达为可变受控元件 |
| `switch` | 支持契约解析 | `UnsupportedComponent` | 状态列表/初态会校验，运行时尚未提供控制邮箱 |

电感采用 branch-current MNA 形式：每个 backward-Euler 步求解 `v+ - v- - (L/dt)i = -(L/dt)i_previous`，并在 `Reset`/重复 `Prepare` 时恢复声明的初始电流。电容使用 companion conductance 与电压历史；`voltage-source` 和 `supplies` 使用额外 MNA branch unknown。矩阵奇异、非有限输入/结果、Newton 不收敛会返回稳定错误；未知器件、未知控制状态、缺失模型不会静默退化。

编译和 `Prepare` 阶段完成节点/分支/历史索引、矩阵和所有工作缓冲的分配；音频 `Process` 只复用这些缓冲，不读文件、不解析 JSON、不动态分配。外部 `module.json` 引用 v2 `logic/*.json` 时，加载器在非实时线程完成解析和编译，`MnaCircuitRuntime::Prepare` 为每个声道复制并准备状态，`Process` 实际调用统一 MNA 求解器。

模块加载器对旧的 `schemaVersion: 1` + `kind: "analog-circuit"` 保持原受限 RC/二极管适配器；v2 读取根对象的 `version: 2`。未知版本、缺失字段、重复 ID、非法节点/引脚、范围值、缺失模型和危险路径均拒绝加载。v1 不会被静默转换为 v2，v2 也不会回退到旧硬编码拓扑。

本版本仍不宣称通用器件覆盖、真实设备 1:1 校准、电位器/开关控制、运放/晶体管/真空管、供电下垂或测量基线自动校准；这些属于后续独立范围。M7 仅交付可审计线性电感 backward-Euler 与已有受限 diode Newton，NAM/原生 DSP 路径不受影响。
