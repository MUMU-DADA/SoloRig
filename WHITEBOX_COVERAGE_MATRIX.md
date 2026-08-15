# SoloRig 白盒与产品需求覆盖矩阵

> 状态日期：2026-08-15。本文将用户原始需求按编号映射到当前代码、测试和剩余验收门禁。原始需求文件 `项目最初预期的需求.txt` 保持用户提供的原样，始终不纳入 Git 提交。

## 状态定义

| 状态 | 含义 |
| --- | --- |
| 已验证 | 当前基线中有实现、对应测试和可复验结果；范围仅限表中描述。 |
| 部分完成 | 已有可运行的核心或产品路径，但仍缺少端到端、完整拓扑、真实宿主或精度证据。 |
| 受阻 | 代码边界已明确，但完成验收需要当前工作区之外的 SDK、驱动、硬件或授权参考资料。 |
| 未完成 | 当前没有足够实现或证据，不能按需求宣称完成。 |

“白盒完成”不是“所有元件类名都存在”。白盒 Release 必须同时具备：可编译拓扑、Prepare/Process/Reset 闭环、稳定拒绝边界、实时线程契约、独立参考证据，以及真实宿主回归。

## 23 项需求矩阵

| 编号 | 需求摘要 | 当前状态 | 当前证据 | 完成前的主要门禁 |
| --- | --- | --- | --- | --- |
| 1 | VST3 插件与独立程序 | 部分完成 | `src/vst3/`、`src/managed/`、VST3 Validator、Windows UI 和 C# contract 回归 | 独立程序真实音频后端；VST3/Standalone 均需接入同一完整效果器图，而非仅基础演示链 |
| 2 | 独立程序连接 ASIO | 受阻 | `src/standalone/audio_routing.*` 目前只做设备/通道/时钟路由分析；测试覆盖格式和映射拒绝 | ASIO SDK/驱动后端、设备枚举、打开/关闭、回调、错误恢复和真实设备测试 |
| 3 | 内置调音表 | 部分完成 | `src/dsp/tuner.*`、`tests/tuner_tests.cpp` 已验证干净音高和静音/非法输入 | 将分析器接入独立程序/VST3 可用的音频输入与 UI 状态，并补充噪声、低音、失锁和刷新节流测试 |
| 4 | 模拟单块的电路元件仿真 | 部分完成 | Circuit v2/MNA、无源/电源/二极管/BJT/JFET/MOSFET/运放/电子管/变压器/扬声器等族测试；M23-B 已将 generic triode → pentode → supply → transformer → speaker 作为外部 Circuit v2 包接入命名图 | 具体器件/功放组合的独立参考校准、复杂拓扑和商业型号证据 |
| 5 | 数字单块算法模拟 | 部分完成 | `src/content/module_package.*`、`src/content/module_runtime.cpp`、`src/dsp/digital_*` 与九种操作的 parser/runtime 测试 | 任意数字图、sidechain、反馈和更完整参数语义；当前是受限 generic 操作序列 |
| 6 | 模块化、串联/并联/任意拓扑 | 部分完成 | `NamedPortAudioGraph` 已支持固定容量命名端口、fan-in/fan-out、延迟补偿和显式延迟反馈；M23-B 已验证完整 generic 外部 MNA 包的命名图接入 | 将更多复杂白盒 MNA、UI 编辑拓扑和任意图结构统一到同一图编译/运行时，并覆盖更多拒绝边界 |
| 7 | 全程 192 kHz、32-bit | 部分完成 | `AudioFormat`、Standalone 路由验证和 M22 192 kHz/Float32/4x 测试 | 真实宿主后端的采样率转换、设备时钟和 Float32 数据链路；不能只用静态格式检查代替 |
| 8 | NAM 与 NAM A2 | 部分完成 | `third_party/NeuralAmpModelerCore`、NAM/NAM A2 runtime 和外部包测试 | 产品端到端导入、错误恢复、真实模型回归及明确的黑盒精度口径；不计入白盒元件证据 |
| 9 | 拖动效果器改变位置 | 已验证（编辑器层） | C ABI/Windows UI/C# contract 覆盖选择、拖动、网格吸附和脏状态 | 与任意拓扑图编辑、撤销/重做和保存恢复联合验收 |
| 10 | 每个效果器预期延迟与性能占用 | 部分完成 | 静态估算、声明延迟、`RealtimeLoadMeter`、`PerformanceSnapshot` 和 C#/C ABI 面板 | 真实宿主回灌、过载状态和可复验基准；“实测未连接”不得升级为实测 |
| 11 | 保存/读取当前链路配置 | 已验证（核心文件层） | `src/content/preset.*`、schema v1/v2、主题/MIDI 扩展和 Save/Open 测试 | 完整任意拓扑、模块包引用失效、版本迁移和独立程序端到端回归 |
| 12 | 快捷键和界面切换配置 | 部分完成 | `shortcut_registry`、`preset_controller`、托管 UI contract | 真实输入设备/窗口生命周期、快捷键冲突和切换时音频图原子替换 |
| 13 | MIDI 控制 | 部分完成 | WinMM 输入、无锁队列、CC/Program Change 映射与测试 | 独立程序正式设备选择、MIDI Learn/Program Change UI、断连/溢出策略和图参数实时发布 |
| 14 | 外部标准化主题 | 已验证（解析与默认主题） | `src/content/theme.*`、外部主题 parser 和测试 | 独立程序全控件渲染、版本迁移和恶意/超大主题文件限制 |
| 15 | 内置深色/浅色主题 | 已验证 | `DefaultDarkTheme`、`DefaultLightTheme` 和 UI/ABI 状态 | 真实窗口全组件视觉回归 |
| 16 | 外部载入新的模拟单块文件 | 部分完成 | 外部 ModulePackage、manifest/logic/metadata 契约及 Circuit v2/MNA runtime | 皮肤包、资源生命周期、复杂拓扑、版本兼容和失败回滚；商业模型仍需授权证据 |
| 17 | 外部载入 NAM/NAM A2 文件 | 部分完成 | 外部 NAM/NAM A2 asset load、路径校验和 runtime Prepare/Process/Reset 测试 | 独立程序导入 UI、真实模型回归和资源错误恢复 |
| 18 | IR 箱体模拟 | 已验证（runtime 层） | `ImpulseResponseConvolver` 和 IR 处理测试 | 接入完整箱头/功放链与真实宿主，并明确延迟、采样率和通道语义 |
| 19 | 外部导入 IR | 已验证（受限导入层） | WAV/IR 外部包导入、长度/扩展名/采样率适配测试 | 产品导入 UI、取消/错误恢复和大文件资源限制 |
| 20 | 全功能/边界测试 | 部分完成 | 当前核心 `577/577`、CTest `5/5`、VST3 Validator `47/47` 及 UI/ABI/C# contract；M23-B 增加完整外部管线回归，M23-C1/C2a/C2b 增加无源、Shockley 和 RC 离散频响数学 golden 的真实 Circuit v2 MNA 对照 | 真实 ASIO、多设备、授权参考、完整产品流程和覆盖矩阵逐项闭环 |
| 21 | 默认效果器外观与自定义皮肤 | 部分完成 | 模块元数据/主题/编辑器基础资源路径已有实现 | 默认旋钮/拨档/踩钉组件、模块自定义皮肤包、资源安全校验和视觉回归 |
| 22 | 逻辑、元属性、外观三层模块包 | 部分完成 | `ModulePackage` manifest、logic、descriptor/metadata 和外部 Circuit/NAM/IR 入口 | 三层独立版本/校验、默认皮肤回退、模拟逻辑与数字逻辑统一运行契约 |
| 23 | 多 ASIO 设备与任意 I/O 通道 | 受阻 | `AsioDeviceDescriptor`、通道映射和异步时钟分析器 | 真实 ASIO 后端、多设备时钟/重采样、任意输入输出选择和故障恢复 |

## 白盒专用覆盖

当前白盒主线可以拆成四层：

1. **元件层**：无源器件、非线性器件、动态电源、变压器/磁芯、电子管、扬声器负载和控制元件。
2. **电路层**：Circuit v2 → Compile → MNA Prepare/Solve，含固定容量多输入、多输出和动态状态。
3. **拓扑层**：命名端口图、串并联、fan-in/fan-out、延迟补偿、显式延迟反馈和多声道独立电路。
4. **证据/宿主层**：实时边界、性能计量、SPICE/golden/实机参考以及 VST3/Standalone/ASIO。

当前第 1～3 层已经有较强的 generic 运行基础，M22-B5 已锁定命名 MNA 的实时拒绝边界，M23-B 已完成一个完整外部 generic 管线的端到端组合，M23-C1 已把两类自有数学 golden 绑定到真实 Circuit v2 MNA 求解路径；第 4 层仍未闭环。因此当前状态应写作“generic whitebox runtime substantially implemented, whitebox Release not complete”。

## M23 代表性白盒覆盖集

以下 16 类是当前 generic 白盒回归的首批代表集，来源为 `tests/m19c_validation_harness.*`。`RunnableGeneric` 只表示自有 generic 路径能完成编译/准备/处理/Reset 和边界验证，不表示商业型号、SPICE 或 `reference-1to1`。

| 类别 | 当前 generic 证据 | 后续独立参考 |
| --- | --- | --- |
| Passive tone | MNA 运行、Reset、192 kHz/过采样、非有限拒绝 | R/C 网络参考或授权 SPICE |
| Diode overdrive | 二极管非线性 MNA 运行和稳定拒绝 | 二极管型号网表、静态曲线和动态波形 |
| BJT boost/fuzz | BJT 三端联合求解 | 晶体管模型授权和失真/偏置基线 |
| JFET boost/fuzz | JFET 三端联合求解 | JFET 模型授权和旋钮扫动基线 |
| MOSFET boost/fuzz | MOSFET 三端联合求解 | MOSFET 模型授权和动态基线 |
| Dynamic op-amp drive | 动态运放/OTA MNA 路径 | 芯片模型授权或独立测量 |
| Tremolo | typed behavior graph 控制链 | 设备调制深度/速率测量 |
| Optical-like compression | envelope + VCA 控制复合 | 光学压缩器动态曲线 |
| Envelope detector | detector 行为模块 | 攻击/释放测量 |
| Optocoupler resistance | LDR/optocoupler → MNA binding | 元件响应和电路测量 |
| BBD/VCO feedback | clocked BBD、VCO 和有界延迟反馈 | BBD 时钟/噪声/频响参考 |
| Tube amp | triode/pentode/beam-tetrode、供电和负载编译/运行 | 管型、功放、变压器和箱体参考 |
| Wah | LFO → MNA 电位器调制 | 实机扫频/频响基线 |
| Phaser | 多 binding R/C MNA 调制 | 实机级数/频响/调制基线 |
| Chorus/Flanger | clocked delay/BBD generic 路径 | 延迟调制、混合和频响基线 |
| Optical hardware | emitter/LDR、动态状态和 MNA 联合求解 | 元件/实机动态测量 |

这个覆盖集用于决定后续参考数据优先级，不把 16 类直接扩展成“覆盖绝大多数单块”。任意新增类别必须同时增加独立实现边界、测试和证据引用。

## M23-A 出口标准

M23-A 只有在以下证据同时存在时才算完成：

- 每一项需求都有表中唯一状态、代码/测试证据和下一道门禁；
- 表中“已验证”不依赖模拟 ASIO、静态 CPU、schema 解析或 generic fixture 去替代真实外部条件；
- 代表性白盒类别已经选定，且每类明确逻辑路径、拓扑边界、参考数据需求和拒绝条件；
- 受阻项记录了缺失的外部前置条件，但不把受阻状态隐藏为已完成；
- 后续提交只允许依据新的实现、测试或外部证据更新状态。

本矩阵是收口门禁，不是白盒完成声明。M23-B 已完成首个完整 generic 组合样例，M23-C1 仅完成 self-authored mathematical golden 的第一道可复验门禁；白盒完成仍需通过 M23-C 的授权/实测参考证据、M23-D（真实 ASIO/宿主）和最终 Release gate。

## M23-B 首个完整外部 generic 管线

- Fixture `fixtures/whitebox/m23b_generic_tube_amp_power_transformer_load.json` 以真实 Circuit v2 网表表达输入耦合、电压供电、triode 前级、pentode 功放级、级间耦合、saturating-hysteretic transformer 和 electrodynamic speaker load；输出测量点为变压器次级与扬声器正端共享的 `secondary_dot`。
- `tests/named_audio_graph_tests.cpp` 将该网表复制到外部模块包，经过 ModulePackage Parse、Named MNA Compile、命名端口图 Prepare/Process/Reset；验收条件为 192 kHz、Float32、4x、双声道、声明延迟、有限非平凡输出和 Reset 后逐样本一致。
- 该里程碑证明“完整 generic 外部组合可运行”，不证明商业箱头、具体管型、真实磁芯、箱体声学、SOA/保护、SPICE/golden、实机测量或 `reference-1to-1`。下一目标是 M23-C：为代表性类别建立独立参考数据与误差门禁。

## M23-C1 自有数学 golden 与 MNA 参考门禁

- `fixtures/whitebox/m23c_passive_divider_*` 固定无源分压网表、公式和 192 kHz/4x 输入输出样本；测试通过 `PrepareWhiteboxMnaV2Json` 进入真实 Circuit v2 → MNA Compile/Prepare/Solve 路径，并以 `CompareEvidenceSamples` 校验误差。
- `fixtures/whitebox/m23c_rc_step_*` 固定串联电阻/对地电容低通网表；测试独立重算每个 backward-Euler oversampled 子步，再与实际 MNA 输出逐样本比较，避免只验证 fixture JSON 或只验证解析器。
- `fixtures/whitebox/m23c_passive_evidence.json` 将 golden comparison 标为 `passed`，同时保留 SPICE comparison 为 `blocked`；`EvaluateEvidencePackage` 因此仍返回 `blocked`，不会把自有数学回归冒充授权 SPICE、实机或 `reference-1to-1`。
- 本轮独立复验：核心 `573/573`、CTest `5/5`、VST3 Validator `47/47`、Windows UI、backend ABI、C# contract 全通过；build manifest MD5 `02D133DBFE9629C2BA85599A6BDE86F1`。下一步是为代表性 generic 类别接入可审计的授权 SPICE/golden/实机数据，并继续保持缺失资料时的阻塞状态。
- M23-C2a 新增 Shockley 二极管静态曲线网表、独立方程 golden 和证据包；测试按每个输入点用独立 Newton 方程求解，再对照真实 Circuit v2 MNA 的静态输出。SPICE comparison 仍为 `blocked`，不把 self-authored 数学结果升级为器件校准。
- M23-C2b 新增 RC 离散频响 golden 和证据包；测试按独立 backward-Euler 离散传递函数生成幅度/相位参考，再用真实 MNA 运行 512 点预热与 1024 点测量逐频率比较。SPICE comparison 仍为 `blocked`，频响结果只代表自有数学模型。
- 本轮独立复验：核心 `577/577`、CTest `5/5`、VST3 Validator `47/47`、Windows UI、backend ABI、C# contract 全通过；build manifest MD5 `157FC363F9151D49E90D22E514D7AC0A`。下一步是扩展更多代表性类别的 DC/动态证据，并继续保持缺失资料时的阻塞状态。
