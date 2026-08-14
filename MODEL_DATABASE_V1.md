# M14 模型数据库与 `model_lookup` 契约

M14 只实现 SoloRig 自有的审计元数据、注册/查找边界和阻塞语义。没有读取、复制或改写未知 PedalKernel 源码、模型、参数或验证数据，也不宣称 PedalKernel 等价或具体商业器件覆盖。

## 审计记录格式

模型记录使用 JSON `version: 1` 与 `schema: "solorig-model-audit"`。必需字段如下：

| 字段 | 含义 |
| --- | --- |
| `modelId` | 稳定模型标识；数据库内唯一 |
| `deviceFamily` / `partNumber` | 器件族和型号标识；没有授权的具体型号只能作为 blocked/planned 元数据 |
| `modelKind` | `diode`、`zener`、`bjt`、`jfet`、`mosfet`、`opamp`、`ota`、`triode`、`pentode`、`vari-mu`、`transformer-core` 或 `test-mathematical` |
| `polarity` | BJT 必需且只能为 `npn`/`pnp`；JFET 必需且只能为 `n-channel`/`p-channel` |
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
4. M18-C3 允许 `diode`、`zener`、`bjt`、`jfet` 和 `test-mathematical` 进入数据库。受保护器件的 Register 强制要求 self-authored clean-room 来源、无商业型号语义且 permission evidence 为 `self-authored-clean-room`；来源不明、商业型号声明或授权不完整稳定返回 `MissingModelAuthorization`。BJT 要求 `partNumber` 精确为 `self-authored-{npn|pnp}-no-commercial-part` 和六参数安全域；JFET 要求 `partNumber` 精确为 `self-authored-{n-channel|p-channel}-no-commercial-part`，并提供四个且仅四个参数：`pinch-off-voltage`、`transconductance-parameter`、`max-current`、`max-step`，单位分别为 V/A/V^2/A/V。所有默认值和声明范围都必须位于 runtime 安全域。MOSFET、运放/OTA、三极管/五极管、vari-mu、变压器/磁芯仍保持 blocked。
5. `CompileMnaCircuit(circuit, database)` 与 `CreateModuleRuntime(package, database)` 是 Prepare-time 引用绑定入口：安全字符串 `model` 必须由数据库按 `modelId`、器件类型以及 BJT/JFET 极性绑定，参数只取已审计默认值并编译为 POD 运行时模型；无数据库、未知、类型不符、极性不符或未授权引用返回稳定错误，不进入音频路径。

模型数据库只建立控制线程/Prepare 阶段的审计和绑定对象；`Solve`/模块 `Process` 不持有数据库、不读取文件、不解析 JSON、不进行动态分配或锁操作。inline Shockley/Zener/BJT/JFET 对象仍是 circuit v2 的 clean-room 数学模型兼容入口，但不能被解释为具体商业型号或数据库授权证据。BJT v1 仅覆盖 generic Ebers-Moll；JFET v1 仅覆盖对称 N/P channel square-law 静态沟道，不包含 gate junction、电容/电荷、沟道长度调制、击穿、温度、自热或噪声。
