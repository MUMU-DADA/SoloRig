# M14 模型数据库与 `model_lookup` 契约

M14 只实现 SoloRig 自有的审计元数据、注册/查找边界和阻塞语义。没有读取、复制或改写未知 PedalKernel 源码、模型、参数或验证数据，也不宣称 PedalKernel 等价或具体商业器件覆盖。

## 审计记录格式

模型记录使用 JSON `version: 1` 与 `schema: "solorig-model-audit"`。必需字段如下：

| 字段 | 含义 |
| --- | --- |
| `modelId` | 稳定模型标识；数据库内唯一 |
| `deviceFamily` / `partNumber` | 器件族和型号标识；没有授权的具体型号只能作为 blocked/planned 元数据 |
| `modelKind` | `diode`、`zener`、`bjt`、`jfet`、`mosfet`、`opamp`、`ota`、`triode`、`pentode`、`vari-mu`、`transformer-core` 或 `test-mathematical` |
| `runtimeStatus` | `approved`、`planned` 或 `blocked` |
| `parameters` | 每项包含 `id`、`default`、`min`、`max`、`unit`；默认值必须位于有限范围内 |
| `source` | `origin`、来源 `version`、`integrity`；只记录可审计来源，不在实时路径解析或读取 |
| `license` | `spdx`、`permissionEvidence`、`reviewStatus`；只有 `approved` 才允许注册 |
| `applicability` | 适用量程，每项包含 `quantity`、`min`、`max`、`unit` |
| `evidenceRefs` | 工作点、静态/动态、频响、SPICE/golden 等证据的稳定引用；当前只保存标识，不自动读取文件 |

未知字段按向前兼容策略保留忽略，但未知版本、缺失必需字段、非法范围和未知 `modelKind` 稳定拒绝。

## 注册与绑定规则

`ParseModelAuditJson` 只负责结构/数值审计；`BuildModelDatabase` 才执行运行时准入：

1. `license.reviewStatus != approved` 返回 `MissingModelAuthorization`。
2. `runtimeStatus != approved` 或器件族契约为 blocked/planned 时返回 `Blocked`。
3. 只有数据库中的记录可由 `Lookup`/`Bind(modelId, expectedKind)` 返回；未知标识返回 `UnknownModel`，类型不符返回 `ModelKindMismatch`。
4. M18-C1 允许无商业归属、self-authored clean-room 且许可证审核通过的 `diode`、`zener` 和 `test-mathematical` 记录进入数据库。diode/Zener 的 Register 还强制要求来源包含 `self-authored clean-room`、型号标识包含 `no-commercial-part` 且 permission evidence 为 `self-authored-clean-room`；来源不明或伪装商业型号稳定返回 `MissingModelAuthorization`。BJT、JFET、MOSFET、运放/OTA、三极管/五极管、vari-mu、变压器/磁芯仍保持 blocked，不能静默退化为已有实验器件。
5. `CompileMnaCircuit(circuit, database)` 是 M18-C1 的 Prepare-time 引用绑定入口：安全字符串 `model` 必须由数据库按 `modelId` 和 diode/Zener 类型绑定，参数只取已审计默认值并编译为 POD 运行时模型；无数据库、未知、类型不符或未授权引用返回 `MissingModelAuthorization`，不进入音频路径。

模型数据库只建立控制线程/Prepare 阶段的审计和绑定对象；`Solve`/模块 `Process` 不持有数据库、不读取文件、不解析 JSON、不进行动态分配或锁操作。inline Shockley/Zener 对象仍是 circuit v2 的 clean-room 数学模型兼容入口，但不能被解释为具体商业型号或数据库授权证据。
