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
4. M18-C8a 允许 `diode`、`zener`、`bjt`、`jfet`、`mosfet`、`opamp`、`ota`、`triode`、`pentode`、`beam-tetrode`、`vari-mu`、受限 `transformer-core` 和 `test-mathematical` 进入数据库。受保护器件的 Register 强制要求 self-authored clean-room 来源、无商业型号语义且 permission evidence 为 `self-authored-clean-room`；来源不明、商业型号声明或授权不完整稳定返回 `MissingModelAuthorization`。BJT/JFET/MOSFET 继续使用既有精确 part/参数契约；bounded opamp/OTA 的 `partNumber` 分别精确为 `self-authored-bounded-opamp-no-commercial-part` 和 `self-authored-bounded-ota-no-commercial-part`。opamp v1 必须且仅能提供 `open-loop-gain`、`minimum-output`、`maximum-output`、`max-step`；C9e dynamic opamp 的 `partNumber` 精确为 `self-authored-dynamic-opamp-no-commercial-part`，并额外且完整提供 `gain-bandwidth` (`Hz`)、`slew-rate` (`V/s`)、`input-resistance` (`ohm`)、`input-bias-current` (`A`)、`input-offset` (`V`)、`output-resistance` (`ohm`)、`output-current-limit` (`A`) 和 `supply-minimum`/`supply-maximum` (`V`)。OTA v1 参数保持不变。triode、pentode、beam tetrode、vari-mu 和 transformer/core 保持各自现有精确 clean-room part/model/参数契约。所有默认值和声明范围必须位于 runtime 安全域；opamp/OTA 的默认上下输出限值必须严格有序，dynamic opamp 的 output 范围必须位于 supply 边界内。
M18-C8b 在本轮授权后新增第二个明确准入：`solorig.cleanroom.transformer-core.saturating-hysteretic.v1`，part 为 `self-authored-saturating-hysteretic-transformer-core-no-commercial-part`，在 C8a 六项绕组参数外严格要求 `saturation-flux`、`hysteresis-width`、`hysteresis-rate`、`remanent-flux`、`max-flux`、`max-current`、`max-step`。`magnetizing-inductance` 必须进入 `saturationFlux * tanh(magnetizingInductance * current / saturationFlux)`，使零点 `dflux/dcurrent` 等于该电感；审计参数不得只解析而不绑定。它只代表 self-authored generic 有界 tanh/磁滞状态数学模型，不代表商业磁芯、SPICE 等价或 1:1 设备。
M18-C9a 为 diode/Zener 增加可选的完整动态结参数组；六项必须同时出现，不能部分绑定：`junction-capacitance` (`F`)、`junction-potential` (`V`)、`grading-coefficient` (`1`)、`temperature-celsius` (`degC`)、`leakage-current` (`A`) 和 `leakage-temperature-coefficient` (`1/K`)。旧 C1 静态审计记录继续兼容；带动态组的记录由独立 binding helper 在 Prepare 阶段完整复制并再次通过 runtime POD 验证。温度范围为 -100 至 200 degC，结电容最大 1e-3 F，漏电温度系数范围为 -0.1 至 0.1 1/K。该准入只覆盖 self-authored generic 动态结，不批准商业 diode/Zener、reverse-recovery 或 SPICE 模型。
M18-C9b 的 BJT 审计记录继续兼容旧六参数；动态记录在其基础上严格追加 `early-voltage`、`be-junction-capacitance`、`bc-junction-capacitance`、`be-junction-potential`、`bc-junction-potential`、`be-grading-coefficient`、`bc-grading-coefficient`、`temperature-celsius`、`leakage-temperature-coefficient` 九项。单位分别为 `V`、`F`、`F`、`V`、`V`、`1`、`1`、`degC`、`1/K`，范围与 C9b 项目契约一致；独立 BJT binding helper 在 Prepare 阶段选择 `ebers-moll` v1 或 `generic-dynamic` v2，运行时不读取数据库。
M18-C9c 的 JFET 审计记录继续兼容旧四参数；动态记录在其基础上严格追加 `channel-length-modulation`、`temperature-celsius`、`gate-saturation-current`、`gate-thermal-voltage`、`gate-junction-capacitance`、`gate-junction-potential`、`gate-grading-coefficient`、`gate-leakage-temperature-coefficient` 八项。独立 JFET binding helper 在 Prepare 阶段选择 `square-law` v1 或 `generic-dynamic` v2，运行时不读取数据库。

M18-C9d 的 MOSFET 审计记录继续兼容四参数；动态记录严格追加 `channel-length-modulation`、`subthreshold-current`、`subthreshold-slope-voltage`、`temperature-celsius`、`body-diode-saturation-current`、`body-diode-thermal-voltage`、`gate-junction-capacitance`、`gate-junction-potential`、`gate-grading-coefficient` 九项，单位和范围与项目契约一致。独立 MOSFET binding helper 在 Prepare 阶段选择 `enhancement-square-law` v1 或 `generic-dynamic` v2；body 永久 tied to source，不允许 bulk 第四端或把 body diode 宣称为对称沟道。
5. `CompileMnaCircuit(circuit, database)` 与 `CreateModuleRuntime(package, database)` 是 Prepare-time 引用绑定入口：安全字符串 `model` 必须由数据库按 `modelId`、器件类型以及晶体管极性（若适用）绑定，参数只取已审计默认值并编译为 POD 运行时模型；无数据库、未知、类型不符、极性不符或未授权引用返回稳定错误，不进入音频路径。

模型数据库只建立控制线程/Prepare 阶段的审计和绑定对象；`Solve`/模块 `Process` 不持有数据库、不读取文件、不解析 JSON、不进行动态分配或锁操作。inline 模型仍是 clean-room 数学兼容入口，不能被解释为具体商业型号或数据库授权证据。opamp/OTA v1 保持无频率状态的有界差分传输；C9e dynamic opamp 只在现有四端结构内增加单主极点/有限 GBW、slew、有限差分输入/输出阻抗、平衡偏置/失调、输出限流和内部供电轨边界，不含显式供电脚、PSRR/CMRR、共模范围、噪声、复杂多极点、温漂、自热、保护电路或商业芯片参数。其它器件族保持各自已登记的边界。
