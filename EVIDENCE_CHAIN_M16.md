# M16 白盒验证证据链

M16 建立的是 SoloRig 自有的证据包 schema、阻塞语义和最小比较工具，不读取、复制或改写未知 PedalKernel 的 SPICE、golden、模型或测试资产，也不宣称等价验证完成。

## Evidence package v1

证据包固定使用 `evidenceSchemaVersion: 1`，至少包含：

- `netlist`：`schemaVersion: 2`、网表来源引用和输入哈希；
- `modelAuditRefs`：M14 审计模型 ID 引用；
- `dcOperatingPoint`、`frequencyResponse`、`staticCurve`、`dynamicResponse`；
- `spiceComparison`、`goldenComparison`；
- `convergence`：每个正常、极端偏置、奇异、非有限、不收敛、Reset/恢复用例及迭代次数；
- `errorThresholds`：DC、频响和动态误差阈值；
- `run`：工具版本、采样率、过采样和随机种子。

每个 section 明确为 `schema-only`、`blocked` 或 `passed`。`passed` section 必须提供样本数、有限的最大绝对误差、正阈值，比较 section 还必须提供 reference 引用。包级 `passed` 要求 SPICE、golden 和 convergence 都为 `passed`；缺少授权外部数据时只能是 `blocked` 或 `schema-only`。

## 最小证据工具

`CompareEvidenceSamples` 接受调用方拥有的两个 `std::span<const double>`，在非实时验证阶段计算最大绝对误差并与阈值比较。它不分配、不读文件、不解析 JSON、不加锁，也不进入音频 Process。解析器只审计元数据和结果声明，不会把模板数据变成实际测量证据。

当前唯一 `passed` fixture 是无商业归属的自有数学模型回归；它不代表真实器件、实机、SPICE 或 PedalKernel 等价。商业/真实模块仍需授权网表、模型、SPICE/golden 和测量基线后才能进入后续验收。
