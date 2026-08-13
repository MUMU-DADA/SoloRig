# M12 通用 `circuit.json v2` 编译器前端

本文件记录 M12 的 clean-room SoloRig 自有实现。它不读取、复制或改写未知来源的 PedalKernel 源码、模型或数据，也不宣称 PedalKernel 等价已验证。

## 已交付的前端边界

`CompileWhiteboxGraphV2Json` 在非实时阶段解析并验证：

- `version: 2`、`schema: "circuit"`、参考地和节点集合；
- 输入/输出端口及节点映射；
- 供电端点和有限电压；
- 任意二端组件的稳定 ID、kind、引脚和可选 modelReference；
- 子电路边界端口和可选 modelReference；
- 模型引用 ID、相对资源路径和路径穿越拒绝；
- `backend.preferred` 的 `mna`/`wdf` 选择。

结果是只有 const 访问器的 `WhiteboxCompiledGraph`。调用 `Prepare` 只产生一个不可变的 prepared token；它不读文件、不解析 JSON，且没有运行时拓扑写入口。

## 拓扑分类矩阵

| 分类 | M12 前端行为 | 运行时状态 |
| --- | --- | --- |
| `series` | 通用二端图、连通且仅两个端点时生成 IR | `Prepare` 成功；`Process` 明确返回 `UnsupportedRuntime` |
| `parallel` | 所有二端组件共享同一端点对时生成 IR | `Prepare` 成功；`Process` 明确返回 `UnsupportedRuntime` |
| `spqr` | 对其它连通环路/分支二端图分类并保留已验证 IR | 交给 M18-B topology lowering；是否可执行由 S/P/Q/R 分解和器件能力决定 |
| `rigid` | 多引脚组件或子电路边界分类并保留已验证 IR | 编译成功不等于可运行；M18-B 对未知多引脚和未绑定 subcircuit 稳定拒绝 |
| `unknown` | 空图或不连通图 | 返回 `InvalidTopology` |

编译成功只表示结构字段、引用和图输入通过前端校验。M18-B 才负责确定性串并约简、residual R 和运行时装配；未知多引脚/跨 subcircuit 图不会因为前端保留 IR 而被视为已支持。当前仍不宣称 canonical SPQR、完整非线性联合求解或行为模块 lowering。

## 稳定错误与事务性

解析和校验失败返回单一 `WhiteboxCompileError`，且 `graph` 为空：不会产生半成品运行时。当前覆盖版本、schema、ID、节点、引脚、端口、供电、子电路、模型引用、危险路径、backend、拓扑和非有限值错误。未知 backend、未知模型引用和危险路径不会静默回退到固定单块算法；拓扑能力拒绝发生在可保留完整 IR 的 lowering/Prepare 层。

## 实时边界

`WhiteboxPreparedGraph::Process` 只接受预先准备的 token 和标量输入；当前返回 `NonFiniteInput` 或 `UnsupportedRuntime`，没有假装音频求解。其实现不分配、不访问文件、不解析 JSON、不加锁。M13 才能在该边界内接入真正的预分配 WDF/MNA runtime，并补充无分配检测和数值验收。

## M13 开工条件

1. 冻结 IR 的节点/端口/子电路/模型引用 ABI，以及 `series/parallel/SPQR/rigid` 的完整分类结果格式。
2. 解决 PedalKernel 源码、模型、AGPLv3/商业授权和 clean-room 证据问题；没有授权的模型仍不得进入数据库。
3. 为 MNA/WDF adaptor、线性状态、DC 初态、非线性分组和过采样定义失败 fixture 与 golden/SPICE 数据，再实现运行时。
