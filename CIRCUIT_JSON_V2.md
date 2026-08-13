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

节点、端口、供电和元件 ID 在整个网表中必须唯一，所有引脚必须引用已声明节点。元件目前可被解析为 `resistor`、`capacitor`、`voltage-source`、`diode`、`potentiometer` 和 `switch`；线性 MNA 编译器本里程碑只执行前三种，二极管/电位器/开关会以明确的 `UnsupportedComponent` 拒绝，不会静默替换成别的算法。二极管必须声明非空且安全的 `model` 引用；绝对路径和 `..` 路径被拒绝。

`capacitor` 使用 backward-Euler companion model，`voltage-source` 使用额外 MNA branch unknown；`supplies` 在编译时作为独立电压源加入。矩阵奇异、非有限输入或结果会返回稳定错误。编译和 `Prepare` 阶段完成节点索引、分支索引、矩阵和历史状态分配；音频 `Process` 只复用这些缓冲，不读文件、不解析 JSON、不分配。

模块加载器对旧的 `schemaVersion: 1` + `kind: "analog-circuit"` 保持原受限 RC/二极管适配器；v2 读取根对象的 `version: 2`。未知版本、缺失字段、重复 ID、非法节点/引脚、范围值、缺失模型和危险路径均拒绝加载。v1 不会被静默转换为 v2，v2 也不会回退到旧硬编码拓扑。

本版本不宣称通用器件覆盖、非线性 Newton 求解、真实设备 1:1 校准、电感/运放/晶体管/真空管、供电下垂或测量基线自动验收；这些属于后续独立范围。
