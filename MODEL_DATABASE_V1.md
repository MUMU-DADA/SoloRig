# M14 模型数据库与 `model_lookup` 契约

M14 只实现 SoloRig 自有的审计元数据、注册/查找边界和阻塞语义。没有读取、复制或改写未知 PedalKernel 源码、模型、参数或验证数据，也不宣称 PedalKernel 等价或具体商业器件覆盖。

## 审计记录格式

模型记录使用 JSON `version: 1` 与 `schema: "solorig-model-audit"`。必需字段如下：

| 字段 | 含义 |
| --- | --- |
| `modelId` | 稳定模型标识；数据库内唯一 |
| `deviceFamily` / `partNumber` | 器件族和型号标识；没有授权的具体型号只能作为 blocked/planned 元数据 |
| `modelKind` | `diode`、`zener`、`bjt`、`jfet`、`mosfet`、`opamp`、`ota`、`triode`、`pentode`、`beam-tetrode`、`vari-mu`、`transformer-core` 或 `test-mathematical` |
| `polarity` | BJT 必需且只能为 `npn`/`pnp`；JFET/MOSFET 必需且只能为 `n-channel`/`p-channel` |
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
4. M18-C7c 允许 `diode`、`zener`、`bjt`、`jfet`、`mosfet`、`opamp`、`ota`、`triode`、`pentode`、`beam-tetrode`、`vari-mu` 和 `test-mathematical` 进入数据库。受保护器件的 Register 强制要求 self-authored clean-room 来源、无商业型号语义且 permission evidence 为 `self-authored-clean-room`；来源不明、商业型号声明或授权不完整稳定返回 `MissingModelAuthorization`。BJT/JFET/MOSFET 继续使用既有精确 part/参数契约；opamp/OTA 的 `partNumber` 分别精确为 `self-authored-bounded-opamp-no-commercial-part` 和 `self-authored-bounded-ota-no-commercial-part`。opamp 必须且仅能提供 `open-loop-gain`、`minimum-output`、`maximum-output`、`max-step`，单位分别为 `1`、`V`、`V`、`V`；OTA 必须且仅能提供 `transconductance`、`minimum-output`、`maximum-output`、`max-step`，单位分别为 `A/V`、`A`、`A`、`V`。triode 的 `partNumber` 精确为 `self-authored-space-charge-triode-no-commercial-part`，必须且仅能提供 `amplification-factor`、`perveance`、`max-current`、`max-step`，单位分别为 `1`、`A/V^1.5`、`A`、`V`。pentode 的 `partNumber` 精确为 `self-authored-space-charge-pentode-no-commercial-part`，必须且仅能提供 `screen-amplification-factor`、`perveance`、`knee-voltage`、`screen-current-fraction`、`max-current`、`max-step`，单位分别为 `1`、`A/V^1.5`、`V`、`1`、`A`、`V`。beam tetrode 的 `partNumber` 精确为 `self-authored-beam-tetrode-capture-no-commercial-part`，必须且仅能提供同名六参数及单位，`modelId` 为 `solorig.cleanroom.beam-tetrode.capture.v1`。vari-mu 的 `partNumber` 精确为 `self-authored-vari-mu-remote-cutoff-no-commercial-part`，`modelId` 为 `solorig.cleanroom.vari-mu.remote-cutoff.v1`，必须且仅能提供 `amplification-factor`、`perveance`、`remote-cutoff-coefficient`、`max-current`、`max-step`，单位分别为 `1`、`A/V^1.5`、`1/V`、`A`、`V`。所有默认值和声明范围必须位于 runtime 安全域；opamp/OTA 的默认上下输出限值必须严格有序，pentode 与 beam tetrode 的 screen current fraction 必须位于 `[0, 0.5]`。transformer-core 仍保持 blocked。
5. `CompileMnaCircuit(circuit, database)` 与 `CreateModuleRuntime(package, database)` 是 Prepare-time 引用绑定入口：安全字符串 `model` 必须由数据库按 `modelId`、器件类型以及晶体管极性（若适用）绑定，参数只取已审计默认值并编译为 POD 运行时模型；无数据库、未知、类型不符、极性不符或未授权引用返回稳定错误，不进入音频路径。

模型数据库只建立控制线程/Prepare 阶段的审计和绑定对象；`Solve`/模块 `Process` 不持有数据库、不读取文件、不解析 JSON、不进行动态分配或锁操作。inline Shockley/Zener/BJT/JFET/MOSFET/amplifier/triode/pentode/beam-tetrode/vari-mu 对象仍是 circuit v2 的 clean-room 数学模型兼容入口，但不能被解释为具体商业型号或数据库授权证据。BJT/JFET/MOSFET 的既有物理边界保持不变；opamp/OTA v1 只覆盖无频率状态的有界线性差分传输，不包含电源脚、输入/输出阻抗、偏置/失调、共模/电源抑制、slew rate、GBW/极点、噪声、温漂或商业芯片参数。triode v1 只覆盖无动态状态的 generic space-charge plate current，不包含 heater/filament、grid conduction/leakage、极间电容/电荷、secondary emission、温度/自热、老化、microphonics、噪声或商业管参数。pentode v1 只覆盖无动态状态的 generic space-charge 和固定 screen-current 分流，不包含商业管型、beam tetrode/vari-mu、真实 suppressor-grid/beam-forming、secondary emission、反向导通、grid conduction/leakage、heater/filament、极间电容/电荷、温度/自热、老化、microphonics 或噪声。beam tetrode v1 只覆盖平方有理 plate-capture 与剩余 screen diversion，不包含商业管型、真实 beam-forming/suppressor-grid、secondary-emission kink、反向或 grid 导通、动态电容、热行为、老化、microphonics 或噪声。vari-mu v1 只覆盖三端 remote-cutoff 静态数学契约，不包含商业管型、真实双节匹配、screen/suppressor/beam 物理、grid conduction/leakage、动态电容、热行为、老化、microphonics、噪声或任何 sidechain/envelope/attack/release 行为。
