# SoloRig 白盒与产品需求覆盖矩阵

> 状态日期：2026-08-16。本文将用户原始需求按编号映射到当前代码、测试和剩余验收门禁。原始需求文件 `项目最初预期的需求.txt` 保持用户提供的原样，始终不纳入 Git 提交。

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
| 1 | VST3 插件与独立程序 | 部分完成 | `src/vst3/`、`src/managed/`、VST3 Validator、Windows UI 和 C# contract 回归；VST3 与 Standalone/编辑器共用 `DefaultRigPreset` 的 Drive→Delay 默认图；新增 SDK factory→IComponent/IAudioProcessor→setup/process/state restore 集成回归（VST3 专项 `9/9`），Standalone 通过 `StandaloneAudioEngine` 接入 ASIO，并提供 preset 打开/保存和外部资源 Import 入口 | 真实 VST3 宿主回灌、Standalone 完整产品流程和真实宿主停止/重配置回归 |
| 2 | 独立程序连接 ASIO | 部分完成 | `src/standalone/asio_host.*` 已实现 Windows 注册驱动发现、COM 打开/探测、Float32/Int16/Int24/Int32 LSB 边界转换、双缓冲回调和安全停止；`AsioDeviceCoordinator` 已支持最多四个活动设备、固定容量跨设备 ring、受限时钟伺服、恢复 API 和任意 I/O 路由；本机 `Midiplus USB Audio` 经 Drive+Delay 图实测 192 kHz、8 入/8 出、3000 个 64-sample 回调，reset/dropout 均为 0；Standalone ReloadPreset 在新图校验/准备或重新启动失败时会尝试恢复上一张可用图；另有 48 kHz/44.1 kHz 离线时钟契约测试 | 两个及以上可用 ASIO 设备的真实异步回归、真实 reset/断连恢复和更多硬件采样率回归 |
| 3 | 内置调音表 | 部分完成 | `TunerAnalyzer` 已接入 Standalone 内部 192 kHz 输入边界；Win32 Tuner 开关和 250ms 控制线程状态刷新可显示音名/cents；新增 440 Hz 集成捕获自测、低音 E、噪声和静音/非法输入测试 | 真实硬件输入下的失锁/刷新压力回归，以及是否向 VST3 宿主暴露调音状态的产品决策 |
| 4 | 模拟单块的电路元件仿真 | 部分完成 | Circuit v2/MNA、无源/电源/二极管/BJT/JFET/MOSFET/运放/电子管/变压器/扬声器等族测试；M23-B 已将 generic triode → pentode → supply → transformer → speaker 作为外部 Circuit v2 包接入命名图 | 具体器件/功放组合的独立参考校准、复杂拓扑和商业型号证据 |
| 5 | 数字单块算法模拟 | 部分完成 | `src/content/module_package.*`、`src/content/module_runtime.cpp`、`src/dsp/digital_*` 与九种操作的 parser/runtime 测试 | 任意数字图、sidechain、反馈和更完整参数语义；当前是受限 generic 操作序列 |
| 6 | 模块化、串联/并联/任意拓扑 | 部分完成 | `NamedPortAudioGraph` 已支持固定容量命名端口、fan-in/fan-out、延迟补偿和显式延迟反馈；M23-B 已验证完整 generic 外部 MNA 包的命名图接入 | 将更多复杂白盒 MNA、UI 编辑拓扑和任意图结构统一到同一图编译/运行时，并覆盖更多拒绝边界 |
| 7 | 全程 192 kHz、32-bit | 部分完成 | `AudioFormat`、Standalone 路由验证、M22 192 kHz/Float32/4x 测试；真实 ASIO 设备经整数样本→Float32 边界进入 Drive+Delay `RigRuntime` 图，设备原生率到内部 192 kHz 的同步重采样已具备，异步时钟有离线契约 | 非 192 kHz 硬件实测、两个设备的时钟策略和更多通道图验证；不能只用静态格式检查代替 |
| 8 | NAM 与 NAM A2 | 部分完成 | `third_party/NeuralAmpModelerCore`、NAM/NAM A2 runtime 和外部包测试 | 产品端到端导入、错误恢复、真实模型回归及明确的黑盒精度口径；不计入白盒元件证据 |
| 9 | 拖动效果器改变位置 | 已验证（编辑器层） | C ABI/Windows UI/C# contract 覆盖选择、拖动、网格吸附和脏状态 | 与任意拓扑图编辑、撤销/重做和保存恢复联合验收 |
| 10 | 每个效果器预期延迟与性能占用 | 部分完成 | 静态估算、声明延迟、`RealtimeLoadMeter`、`PerformanceSnapshot` 和 C#/C ABI 面板；Win32 ASIO 状态持续显示回调计数、最近 block 大小和 reset/dropout 恢复提示 | 真实宿主回灌、过载状态和可复验基准；“实测未连接”不得升级为实测 |
| 11 | 保存/读取当前链路配置 | 已验证（核心文件层） | `src/content/preset.*`、schema v1/v2、主题/MIDI 扩展和 Save/Open 测试 | 完整任意拓扑、模块包引用失效、版本迁移和独立程序端到端回归 |
| 12 | 快捷键和界面切换配置 | 部分完成 | `shortcut_registry`、`preset_controller`、托管 UI contract | 真实输入设备/窗口生命周期、快捷键冲突和切换时音频图原子替换 |
| 13 | MIDI 控制 | 部分完成 | WinMM 输入、无锁队列、CC/Program Change 映射与测试 | 独立程序正式设备选择、MIDI Learn/Program Change UI、断连/溢出策略和图参数实时发布 |
| 14 | 外部标准化主题 | 已验证（解析与默认主题） | `src/content/theme.*`、外部主题 parser 和测试 | 独立程序全控件渲染、版本迁移和恶意/超大主题文件限制 |
| 15 | 内置深色/浅色主题 | 已验证 | `DefaultDarkTheme`、`DefaultLightTheme` 和 UI/ABI 状态 | 真实窗口全组件视觉回归 |
| 16 | 外部载入新的模拟单块文件 | 部分完成 | 外部 ModulePackage、manifest/logic/metadata 契约及 Circuit v2/MNA runtime；编辑器与 Standalone 原生窗口均可显式导入目录、`manifest.json` 或独立 `.json`，执行完整校验并加入动态目录/preset；独立 Circuit v2 以 `sourceKind=circuit` 持久化；VST3 state v3 可持久化并在 setupProcessing 停止边界重新校验/注册同一外部包；C ABI 端到端覆盖导入、添加节点、保存/重开 | 真实 VST3 宿主回灌、复杂拓扑、版本兼容和更多失败回滚；直接 Circuit 文件不携带 manifest 参数元数据，带 `parameter_bindings` 的文件需使用完整 ModulePackage；商业模型仍需授权证据 |
| 17 | 外部载入 NAM/NAM A2 文件 | 部分完成 | 编辑器/C ABI/Standalone 原生 Import 入口会真实校验 NAM/NAM A2、生成动态模块目录项并将 `sourceKind=nam`/源路径写入 preset；Standalone runtime 与 VST3 state v3 可按引用重建并进入 Prepare/Process/Reset | 真实模型回归、真实宿主回灌和资源错误恢复 |
| 18 | IR 箱体模拟 | 已验证（runtime 层） | `ImpulseResponseConvolver` 和 IR 处理测试 | 接入完整箱头/功放链与真实宿主，并明确延迟、采样率和通道语义 |
| 19 | 外部导入 IR | 已验证（受限导入层） | 编辑器/C ABI/Standalone 原生 Import 入口会真实校验 WAV/IR、生成动态模块目录项并将 `sourceKind=ir`/源路径写入 preset；Standalone runtime 与 VST3 state v3 可重建卷积模块，另有格式/长度/采样率适配测试 | 真实宿主回灌、取消/错误恢复、大文件资源限制和完整产品流程 |
| 20 | 全功能/边界测试 | 部分完成 | 当前核心 `613/613`、Windows UI `7/7`、CTest `5/5`、VST3 专项 `9/9`、VST3 Validator `47/47` 及 UI/ABI/C# contract；M23-B 增加完整外部管线回归，M23-C1/C2a/C2b/C2c/C2d/C2e/C2f/C2g/C2h/C3a/C3b/C3c 增加十三类数学 golden 的真实 Circuit v2 MNA 对照；M23-D1/D2b2/D3b 增加 ASIO 生命周期、整数边界、同步/异步重采样契约、Standalone 控件、统一 Drive+Delay 图、Tuner 捕获和真实硬件回调；M23-D3c/D3d 增加模块包校验、动态目录、preset 引用、Standalone runtime 和 C ABI 重开回归；M23-D3e 增加直接 NAM/IR 引用持久化、编辑器重开和 direct IR runtime 回归；M23-D3f 增加 VST3 state 外部 IR 重建与模块皮肤绑定回归；M23-D3h 增加 SDK factory 级组件生命周期和 state restore 回归；M23-D3i 增加直接 Circuit v2 文件解析、端口描述、preset 重开和动态目录回归；M23-D3j 增加皮肤图像相对路径校验、WIC→Direct2D 解码缓存、绘制回归和非法资源回退；M23-D3k 增加 Standalone 原生 Import 入口、文件过滤、成功后 runtime 重载和失败状态保持回归；M23-D3l 增加 Standalone ReloadPreset 失败回滚和重新启动失败回滚契约 | 多设备真实硬件、非 192 kHz 硬件实测、授权参考、真实断连恢复、真实 DAW 宿主回灌和覆盖矩阵逐项闭环 |
| 21 | 默认效果器外观与自定义皮肤 | 部分完成 | 默认皮肤回退、外部皮肤 JSON 校验、ModulePackage `skinFile` 路径生命周期、模块级皮肤绑定、Win32 默认旋钮/拨档/按钮/踩钉几何绘制，以及可选相对位图资源的 WIC 解码/Direct2D 缓存绘制已接通；有编辑器资源边界、重复 UI Paint 和图像绘制计数回归 | 自定义皮肤矢量/动画资源、真实窗口视觉基线、不同 DPI/显卡驱动下的视觉验收 |
| 22 | 逻辑、元属性、外观三层模块包 | 部分完成 | `ModulePackage` manifest、logic、descriptor/metadata、外部 Circuit/NAM/IR 入口；编辑器动态目录、manifest 包与直接 Circuit/NAM/IR preset 引用、Standalone 注册/处理自测、VST3 state v3 重建和 C ABI 重开回归已接通；模块级 skin JSON 可声明相对图像资源，非法/缺失时回退默认控件 | 真实 VST3 宿主、多模块皮肤视觉产品绑定、矢量/动画资源生命周期/版本迁移和更复杂模拟逻辑与数字逻辑统一运行契约 |
| 23 | 多 ASIO 设备与任意 I/O 通道 | 部分完成 | `AsioDeviceCoordinator` 已支持最多四个 ASIO host、固定容量跨设备 ring、任意物理输入/输出路由文本、每设备 buffer 配置、异步时钟离线契约和 Recover 入口；单设备 `Midiplus USB Audio` 真实回调通过 | 至少两个可用 ASIO 设备的真实同步/异步回归、真实断连/重置恢复和更多硬件 |

## 白盒专用覆盖

当前白盒主线可以拆成四层：

1. **元件层**：无源器件、非线性器件、动态电源、变压器/磁芯、电子管、扬声器负载和控制元件。
2. **电路层**：Circuit v2 → Compile → MNA Prepare/Solve，含固定容量多输入、多输出和动态状态。
3. **拓扑层**：命名端口图、串并联、fan-in/fan-out、延迟补偿、显式延迟反馈和多声道独立电路。
4. **证据/宿主层**：实时边界、性能计量、SPICE/golden/实机参考以及 VST3/Standalone/ASIO。

当前第 1～3 层已经有较强的 generic 运行基础，M22-B5 已锁定命名 MNA 的实时拒绝边界，M23-B 已完成一个完整外部 generic 管线的端到端组合，M23-C1/C2a/C2b/C2c/C2d/C2e/C2f/C2g/C2h/C3a/C3b/C3c 已把十三类自有数学 golden 绑定到真实 Circuit v2 MNA 求解路径；第 4 层仍未闭环。因此当前状态应写作“generic whitebox runtime substantially implemented, whitebox Release not complete”。

## 白盒完成目标（Release gate）

白盒完成不以“代码分支已经存在”为终点，按以下顺序收口：

1. **运行时闭环**：代表性白盒模块均完成 Parse/Compile → Prepare → Process → Reset，固定容量、非有限输入、拓扑/端口错误和实时线程拒绝边界保持稳定；当前 generic 主线与 `613/613` 核心回归已达到这一阶段。
2. **宿主一致性**：VST3、Standalone 和编辑器共用同一 `RigPreset`/`RigRuntime` 图配置；真实宿主验证 setup、变 block、停止重配、状态恢复和音频输出，不能只依赖单元测试或 Validator。
3. **音频后端验收**：Standalone 保持内部 192 kHz/Float32；完成至少两个真实 ASIO 设备的任意 I/O、异步时钟、buffer 差异、reset/断连恢复，并保存可复验的探针输出。当前最多四设备协调、离线异步时钟契约和单设备硬件探针已完成，真实多设备证据仍是门禁。
4. **独立参考证据**：为选定白盒覆盖集补齐授权 SPICE、golden waveform 或实机测量，并记录采样率、激励、容差、版本和授权来源；缺资料的模块继续标为 `受阻`，不得用 generic 或数学自证替代。
5. **Release 回归**：核心、CTest、VST3 Validator、Windows UI/backend ABI、C# contract、真实宿主和硬件探针全部通过；更新矩阵后再做一次不包含用户原始需求文件的详细提交。

当前可执行的下一目标是 **M23-D3 Release evidence**：先完成真实双 ASIO/故障恢复与宿主回灌；若环境无法提供第二个可用设备或授权参考，则保留代码交付和明确的受阻清单，不宣称白盒最终完成。

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

本矩阵是收口门禁，不是白盒完成声明。M23-B 已完成首个完整 generic 组合样例，M23-C1/C2a/C2b/C2c/C2d/C2e/C2f/C2g/C2h/C3a/C3b/C3c 已完成 self-authored mathematical golden 的可复验门禁，M23-D1/D2 已完成单设备真实 ASIO 主机、Standalone 设备控制、设备原生率同步重采样和 Drive+Delay 图回调探针；本轮又补齐了最多四个并发 ASIO host、固定容量跨设备缓冲、受限时钟伺服、恢复 API、任意 I/O 路由文本和 UI 入口。多设备真实硬件尚未完成实测，白盒完成仍需通过 M23-C 的授权/实测参考证据、M23-D2 的多设备硬件/故障恢复子门禁和最终 Release gate。

## M23-B 首个完整外部 generic 管线

- Fixture `fixtures/whitebox/m23b_generic_tube_amp_power_transformer_load.json` 以真实 Circuit v2 网表表达输入耦合、电压供电、triode 前级、pentode 功放级、级间耦合、saturating-hysteretic transformer 和 electrodynamic speaker load；输出测量点为变压器次级与扬声器正端共享的 `secondary_dot`。
- `tests/named_audio_graph_tests.cpp` 将该网表复制到外部模块包，经过 ModulePackage Parse、Named MNA Compile、命名端口图 Prepare/Process/Reset；验收条件为 192 kHz、Float32、4x、双声道、声明延迟、有限非平凡输出和 Reset 后逐样本一致。
- 该里程碑证明“完整 generic 外部组合可运行”，不证明商业箱头、具体管型、真实磁芯、箱体声学、SOA/保护、SPICE/golden、实机测量或 `reference-1to-1`。下一目标是 M23-C：为代表性类别建立独立参考数据与误差门禁。

## M23-C1 自有数学 golden 与 MNA 参考门禁

- `fixtures/whitebox/m23c_passive_divider_*` 固定无源分压网表、公式和 192 kHz/4x 输入输出样本；测试通过 `PrepareWhiteboxMnaV2Json` 进入真实 Circuit v2 → MNA Compile/Prepare/Solve 路径，并以 `CompareEvidenceSamples` 校验误差。
- `fixtures/whitebox/m23c_rc_step_*` 固定串联电阻/对地电容低通网表；测试独立重算每个 backward-Euler oversampled 子步，再与实际 MNA 输出逐样本比较，避免只验证 fixture JSON 或只验证解析器。
- `fixtures/whitebox/m23c_passive_evidence.json` 将 golden comparison 标为 `passed`，同时保留 SPICE comparison 为 `blocked`；`EvaluateEvidencePackage` 因此仍返回 `blocked`，不会把自有数学回归冒充授权 SPICE、实机或 `reference-1to-1`。
- M23-C1 本轮独立复验：核心 `573/573`、CTest `5/5`、VST3 Validator `47/47`、Windows UI、backend ABI、C# contract 全通过；build manifest MD5 `02D133DBFE9629C2BA85599A6BDE86F1`。
- M23-C2a 新增 Shockley 二极管静态曲线网表、独立方程 golden 和证据包；测试按每个输入点用独立 Newton 方程求解，再对照真实 Circuit v2 MNA 的静态输出。SPICE comparison 仍为 `blocked`，不把 self-authored 数学结果升级为器件校准。
- M23-C2b 新增 RC 离散频响 golden 和证据包；测试按独立 backward-Euler 离散传递函数生成幅度/相位参考，再用真实 MNA 运行 512 点预热与 1024 点测量逐频率比较。SPICE comparison 仍为 `blocked`，频响结果只代表自有数学模型。
- M23-C2c 新增带结电容的 Shockley 动态网表、独立 backward-Euler/Newton golden 和证据包；测试逐个重算 192 kHz/4x 的每个历史子步，再对照真实 MNA 的动态输出。SPICE comparison 仍为 `blocked`，不把自有动态模型当作商业结电容校准。
- M23-C2d 新增带 5 V 集电极供电的 Ebers–Moll NPN BJT 静态网表、独立 2x2 Newton golden 和证据包；测试逐个重算基极偏置下的集电极/发射极工作点，再对照真实 MNA 输出。SPICE comparison 仍为 `blocked`，不把 self-authored BJT 方程当作晶体管型号校准。
- M23-C2e 新增带 5 V 漏极供电的 square-law N-channel JFET 静态网表、独立 2x2 Newton golden 和证据包；测试逐个重算 pinch-off/三极区与饱和区工作点，再对照真实 MNA 输出。SPICE comparison 仍为 `blocked`，不把 self-authored JFET 方程当作器件型号校准。
- M23-C2f 新增带 5 V 漏极供电的 enhancement square-law N-channel MOSFET 静态网表、独立标量 Newton golden 和证据包；测试逐个重算截止、饱和与三极区工作点，再对照真实 MNA 输出。SPICE comparison 仍为 `blocked`，不把 self-authored MOSFET 方程当作器件型号校准。
- M23-C2g 新增带 12 V 阳极供电的 space-charge triode 静态网表、独立标量 Newton golden 和证据包；测试逐个重算截止与导通区阳极工作点，再对照真实 MNA 输出。SPICE comparison 仍为 `blocked`，不把 self-authored triode 方程当作具体管型校准。
- M23-C2h 新增带栅极 RC 历史的 space-charge triode 动态网表、独立 192 kHz/4x backward-Euler/Newton golden 和证据包；测试逐个重算每个 oversampling 子步的栅极电容历史与阳极工作点，再对照真实 MNA 动态输出并验证 Reset。SPICE comparison 仍为 `blocked`，不把 self-authored triode 动态方程当作具体管型校准。
- M23-C3a 新增带栅极 RC 历史、屏极供电与阳极负载的 space-charge pentode 动态网表、独立 2x2 backward-Euler/Newton golden 和证据包；测试逐个重算每个 oversampling 子步的栅极历史、屏极/阳极耦合工作点，再对照真实 MNA 动态输出。SPICE comparison 仍为 `blocked`，不把 self-authored pentode 动态方程当作具体功率管校准。
- M23-C3b 新增 pentode → lossy-linear coupled-inductor → electrodynamic speaker 的动态组合网表、独立 7 元素 backward-Euler/Newton golden 和证据包；测试逐个重算栅极、屏极/阳极、变压器一次/二次电流与扬声器机械状态，并验证真实 MNA 输出和 Reset 重放。该组合仍是 self-authored generic 模型，SPICE comparison 保持 `blocked`。
- M23-C3c 新增动态 rectified-reservoir 供电、space-charge pentode、saturating-hysteretic transformer 和二次侧负载的组合网表、独立 8 元素 backward-Euler/Newton golden 和证据包；测试逐个重算栅极、屏极/阳极、供电 Sag、磁芯磁通/磁滞及变压器一次/二次电流，并验证真实 MNA 输出和 Reset 重放。该组合仍是 self-authored generic 模型，SPICE comparison 保持 `blocked`。
- 本轮独立复验：核心 `611/611`、CTest `5/5`、VST3 Validator `47/47`、Windows UI、backend ABI、C# contract 全通过；M23-D1/D2b2/D3b 的 ASIO 主机、Int16/Int24/Int32 转换、同步重采样、离线异步时钟、统一默认图和 Tuner 捕获用例，M23-D3c/D3d 的模块包校验、动态目录、preset 引用、Standalone runtime 和 C ABI 重开用例，M23-D3e 的直接 NAM/IR 引用和 direct-IR runtime 用例，以及 M23-D3f 的 VST3 state 外部 IR 重建和模块皮肤绑定用例已纳入回归。真实 `Midiplus USB Audio` 探针在 192 kHz 下经 Drive+Delay 图完成约 3000 个 64-sample 回调，reset/dropout 均为 0。下一步是补齐非 192 kHz/多设备硬件实测、真实故障恢复、真实 VST3 宿主回灌、皮肤视觉回归和授权/实测参考。

## M23-D1 单设备真实 ASIO 主机（已完成）

- `src/standalone/asio_host.*` 读取 Windows ASIO 注册表并通过驱动 COM 类发现/打开设备；控制线程完成 `init → getChannels/getBufferSize/getLatencies/getChannelInfo → createBuffers`，显式 `Start/Stop/Close` 生命周期不进入 C ABI UI 创建路径。
- 真实回调使用固定容量指针数组；Float32 LSB 直接映射，Int16/Int24/Int32 LSB 在预分配 scratch 中转换为内部 Float32，输出在回调尾部转换回驱动格式。Process 回调不分配、不加锁、不读文件、不解析 JSON。
- `StandaloneAudioEngine` 在控制线程按实际 preferred block size 构建当前编辑器 `RigPreset` 的 `RigRuntime`，再通过预分配 `AudioBuffer` 把 ASIO 2x2 Float32 边界接入 Drive+Delay 图；`tools/asio_probe.cpp --stream <driver>` 现在验证该完整图而不是旧直通回调。当前机器 `Midiplus USB Audio` 实测 8 入/8 出、192 kHz、缓冲范围 32..8192、preferred 64，并运行 1000 ms 得到 3000 次 64-sample 图回调；`KATANA` 因未连接设备被明确拒绝。
- 该里程碑仍不宣称多 ASIO 实例、异步时钟、任意物理 I/O、断连/重置恢复或硬件过载恢复。没有授权参考数据时，M23-C 的 SPICE gate 仍保持 `blocked`。

## M23-D2：Standalone 完整效果图接入（D2b2 代码阶段已交付）

- 新增 `src/standalone/standalone_audio_engine.*`，控制线程完成驱动探测、192 kHz/Float32 `AudioFormat`、当前编辑器 preset 的 `RigRuntimeBuilder → RigRuntime::Prepare`，再创建 ASIO buffers；ASIO callback 只复制预分配缓冲并调用无分配 `RigRuntime::Process`。
- `StandaloneAudioEngine::OpenWithRouting` 接受显式单设备物理输入/输出到 processing channel 的路由表；`OpenWithDevices` 与路由文本进一步支持多设备物理映射，均复用同一套验证、原生设备 block size 和同步重采样路径。
- `AsioHost` 的 ASIO callback 分发已从单全局槽位扩展为最多四个并发 host 槽位，`AsioDeviceCoordinator` 在其上完成跨设备 ring、聚合处理和独立输出节拍；代码门禁已具备，真实多设备硬件证据仍待补齐。
- `AsioDeviceCoordinator` 已接入 `StandaloneAudioEngine::OpenWithDevices`：每个被路由设备独立完成 probe/open/start，主输出设备在内部 192 kHz 图上处理，其他输入/输出通过固定容量 SPSC frame ring 与有界 ±2% fill-level clock servo 对齐；ASIO reset/overload 状态可由 `RecoverAudio` 重新打开保存的设备组。
- Win32 路由编辑框支持 `设备索引:in=物理通道列表;out=物理通道列表`，多个设备用 `|` 分隔，例如 `0:in=0,1;out=0,1|1:in=0;out=1`；解析和重复/越界前的结构拒绝均有单元测试。
- Win32 Standalone 工具栏新增 ASIO Refresh/Open/Start/Close 与设备选择；显式 Open 才访问驱动，关闭窗口按 `Stop → Close → RigRuntime::Shutdown` 顺序释放，打开新 preset 后会在停机边界重建效果图。
- 真实验证：`SoloRigAsioProbe.exe --stream "Midiplus USB Audio"` 经 Drive+Delay 图运行 1000 ms，`callbacks=3000`、`samplesPerCallback=64`、`reset=0`、`dropout=0`；核心 `611/611`、CTest `5/5`、VST3 Validator `47/47`、Windows UI/backend ABI/C# contract 全通过。当前只有单设备 192 kHz 硬件实测，离线异步时钟契约通过；多设备异步、非 192 kHz 和断连恢复仍缺真实设备证据。
- 剩余边界：多设备异步协调的真实硬件实测、设备断连/ASIO reset 的真实恢复演练、真实 VST3 宿主回灌、硬件过载统计和授权 SPICE/golden/实机参考仍未完成；代码路径和离线门禁已具备，但不能替代外部设备证据。
