# SoloRig 白盒效果器支持说明

> 状态日期：2026-08-15。本文描述通用白盒电路路径的真实能力边界，不包含 NAM/NAM A2 黑盒覆盖，也不把原生数字 DSP 误记为元件级白盒。

## 结论（当前白盒主线）

当前白盒运行时已经具备常见无源器件、独立电源、基础受控源，以及 generic dynamic diode/Zener、BJT、JFET、MOSFET、dynamic opamp、bounded OTA、triode、pentode、beam tetrode、vari-mu、lossy-linear coupled-inductor 和 saturating-hysteretic transformer/core 模型。C8b-C9e 已由 `9b06c70` 提交；C10a/C10b、D1a/D1b1/D1b2/D1b3 和 D2a generic clocked BBD/delay 已由 `9dc9368` 提交，D2b1 VCO、D2b2 VCA、D2c1 envelope follower、D2c2 generic LFO、D2d1 optocoupler/LDR response 已分别由 `4774d79`、`4a4bfa1`、`dc2f1b6`、`ebd2438`、`f4c5b14` 提交。D2d2 generic modal spring response、D2e1-D2e7 behavior graph Compile/lowering、固定容量 runtime、typed port/route、控制域 runtime 和 envelope + VCA composite 已随 M19-A1 提交 `2e6a8bf`；M18-D3a 动态 BBD 时钟链为未提交本地候选，M18-D3b/M19-A2 单延迟反馈链也为未提交本地候选。这些仍只构成 self-authored clean-room generic 能力，不构成商业器件或 1:1 证据。

当前仍不能宣称覆盖“绝大多数单块效果器和箱头”。主要阻塞不只是元件数量，还包括实时旋钮/开关、行为模块、复杂拓扑编译、器件高阶物理、功放与电源/负载交互，以及 SPICE/golden/实机误差验证。

### 当前状态覆盖说明（2026-08-15）

- `a321a0f` 已提交 M18-D3a、M18-D3b/M19-A2 和 M19-B1 阶段 2b/3b；历史计划表中这些阶段的“未提交本地候选”文字为历史记录，不覆盖当前提交状态。
- M19-C/M20-B4 当前已提交：16 个代表性类别全部具有可运行 generic 证据，包含 Wah、Phaser、Chorus/Flanger 和 caller-owned optical hardware 电气组合；这不等于商业型号或任意拓扑覆盖。
- M20-B4 已提交时的验证为核心 `538/538`、CTest `5/5`、VST3 Validator `47/47`、VST3/UI/backend ABI/C# contract 全通过；manifest `ba897feda5a24b1782ae2da3901a6ec9`。
- M20-C1 已提交：typed behavior graph 的 signal 端口支持固定容量多连接；同域 fan-in 在 runtime 求和，fan-out 复制到多个下游/边界输出；编译阶段仍拒绝非法域、循环、缺端口和容量超限。最新验证为核心 `540/540`、CTest `5/5`、VST3 Validator `47/47`、VST3/UI/backend ABI/C# contract 全通过；manifest `a179267303ca48b196fa93a57e806bc7`。
- M20-C2 已提交：通用 typed behavior → MNA control binding composite 将 control/resistance 输出经调用方显式范围映射写入预编译 MNA 控制槽，覆盖 envelope/LFO/optocoupler 类跨域组合；未宣称任意多端 MNA 或商业 sidechain。最新验证为核心 `545/545`、CTest `5/5`、VST3 Validator `47/47`、VST3/UI/backend ABI/C# contract 全通过；manifest `ed44119e36484516814c808b0bbd9d39`。
- M22-A1 已提交 `b75200d`：MNA 编译/求解器支持固定容量多输入、多输出端口，新增 span 输入和输出快照，旧单输入 `Solve(double)` 保持兼容；双输入双输出电阻网络、输入数量、非有限和 192 kHz 边界通过核心回归。
- M22-B1 已提交：circuit v2 parser 接受多输入端口；外部 MNA runtime 支持“输入端口数=输出端口数、每端口单声道、按 AudioBuffer 声道序映射”的固定容量闭环，并拒绝空输入、不等数量、超过 8 端口或 descriptor 不匹配。命名端口图、独立 sidechain、不同 I/O 数量和任意多端实时图仍未接入。提交时核心为 `555/555`，构建 manifest MD5 为 `f6de0cfd474ade51bb033e50939f6e53`。

## M20-C3 产品级审计与白盒完成计划（当前进行中）

本阶段先锁定“已验证、部分完成、阻塞”三种口径，再按门禁逐项补齐。当前代码基线不能把静态性能估算、路由验证器或证据 schema 写成真实产品功能。

### 已验证的基础能力

- 图级声明延迟、固定容量实时边界、192 kHz/过采样专项、Reset/非有限/稳定拒绝，以及核心、CTest、VST3 Validator、Windows UI、backend ABI 和 C# contract 回归均有证据；M20-C2 最近一次基线为核心 `545/545`、CTest `5/5`、Validator `47/47`。
- `RealtimeLoadMeter` 已接入 `AudioGraph::Process`，每个节点在真实图处理时记录最近/峰值 block load，并由控制线程快照读取；当前独立程序 UI 仍展示模块目录静态 `estimated_cpu_percent`，尚未把实测值接入 UI/ASIO 面板。
- ASIO 路由层可以验证任意通道映射、采样率转换和异步时钟需求，但尚无真实 ASIO SDK 后端、设备枚举、打开/关闭、回调和多设备重采样。
- 外部 Circuit v2 包可以进入受限 MNA 组件运行时；外部数字逻辑的九种声明操作（Gain、Delay、Feedback、Mix、Filter、Waveshaper、Dynamics、PitchShift、Bypass）均已进入固定容量 runtime，但仍是线性操作序列，不等于任意数字图或 sidechain。
- reference/golden 目前只有 schema、解析器、评估器和“缺少授权资料时拒绝”的测试；仓库没有可授权的 SPICE、golden waveform 或实机测量数据，因此不能标记 `reference-1to1`。

### 完成白盒的计划目标与验收门禁

1. **M20-C3：产品性能与宿主真实性**。已完成 AudioGraph 节点级实测 block load 与控制线程快照；仍需把快照接入 UI/ASIO 性能面板并提供过载状态。明确区分图级实测与模块目录估算；补齐 VST3/独立程序生命周期、ASIO 设备枚举与真实回调，没有 SDK 或硬件条件时只能保留 `blocked`，不能用模拟路由测试替代。
2. **M21：外部模块可运行闭环**。定义并冻结模块包版本、逻辑/电路/皮肤三层契约；已补齐当前九种声明数字操作的受限 runtime。每个外部包必须经过 Parse → Compile/Prepare → Process → Reset，并验证参数、文件、错误和实时线程边界；任意数字图、sidechain 和多端口语义仍属于后续 M22。
3. **M22：通用白盒拓扑**。M22-A1 已完成并提交；M22-B1 已先把受限多端口契约接入外部 MNA runtime，但仍需把端口数据接入 typed behavior/AudioGraph 的命名端口，扩展经过审计的阶段化接口，覆盖串联、并联、控制、sidechain、反馈和电源/负载组合；每个不支持的拓扑必须稳定拒绝，不能靠固定产品识别或静默数字替代。
4. **M23：参考与覆盖集**。为选定的失真、滤波/调制、压缩、BBD、混响、电子管前后级和箱头负载建立可授权的网表、元件/模型版本、供电/负载条件和测量基线；预先固定静态、频响、动态、谐波/互调、旋钮扫动误差阈值，逐类生成 golden/SPICE/实机证据。
5. **Release 白盒门**。只有在支持矩阵中的每一类同时具备可运行路径、稳定拒绝边界、实时无分配/无锁/无文件证明、独立参考证据和全量回归，且真实宿主/ASIO 条件通过时，才可称为“白盒完成”。generic self-authored 模型仍需明确标注为 generic，不得自动升级为商业型号 `reference-1to1`。

上述目标是完成条件，不是当前完成声明；M20-C3 仍是进行中，M21 已完成第一阶段数字操作闭环但尚未完成完整外部包/拓扑验收，M22-M23 尚未开始验收。

### M21 第一阶段：外部数字操作 runtime（已提交 `ec21511`）

- `DigitalFilter` 是预分配的一阶低通，操作参数按 Hz 解释并在 Prepare 时严格限制到 Nyquist 内。
- `DigitalDynamics` 是有界 envelope compressor/limiter，操作参数为 `[0, 1]` 的 amount；零值透明，满值为高比例限制。
- `DigitalPitchShift` 是固定容量双 grain 处理器，操作参数为 `[-24, 24]` 半音；grain/ring 状态在 Prepare 分配，Process 不分配、不读文件、不解析 JSON。
- M21-A1 已提交 `ec21511`：新增独立 DSP 测试、外部 package 集成测试和参数拒绝测试；提交时核心测试为 `552/552`，CTest `5/5`，VST3 Validator `47/47`。这些测试证明 self-authored generic 算法的运行边界，不证明商业算法、音质 1:1 或任意数字拓扑。

### M22-A1 第一阶段：MNA 固定容量多端口边界（已提交 `b75200d`）

- `MnaPreparedCircuit` 预分配多个输入端口电压源分支和多个输出测量节点；`Solve(std::span<const double>)` 不在实时路径分配，`OutputValue(index)` 返回最近一次已求解端口值。
- 旧单输入电路继续使用 `Solve(double)`；多输入电路通过错误 `InvalidInputCount` 拒绝错误调用，非有限输入仍返回 `NonFinite`。
- 当前证据是双输入双输出 passive fixture，不宣称外部 package、typed behavior、sidechain 或任意多端网络已经消费该接口。

### M22-B1 第一阶段：外部 MNA 多端口 runtime（已提交）

- parser 不再把第二个输入端口视为非法；runtime 在 Prepare 预留单个多端 MNA 状态，Process 每个采样以固定 `std::array` 收集端口输入，调用 `Solve(span)` 并按输出端口写回 AudioBuffer 声道。
- 合同限定输入/输出端口数量相等、最多 8 个、每个端口单声道；descriptor 必须逐端匹配 circuit port，且多端口 descriptor 的 `maxChannels` 必须为 1。旧单输入/单输出模块继续按每音频声道独立电路运行。
- 双输入双输出外部 package 已通过 192 kHz 运行测试，证明 Parse → Compile → Prepare → Process 的固定容量闭环；不同 I/O 数量、命名端口图、typed behavior、sidechain、反馈和多设备通道路由仍是后续门禁。

## 当前状态补充：M20-A1

- M19-C 已提交为 `6c0d46e`：15 个代表性类别中 12 类具有可运行 generic 证据，Wah/Phaser、Chorus/Flanger 和真实 optical hardware 三类保持 stable refusal。
- M20-A1 已提交为 `b2010d4`：typed behavior 支持 audio/control/detector/emitter 多边界输入、audio/control/resistance 多边界输出，并统一调度七类已授权 generic runtime。
- M20-A1 最新验证为核心 `524/524`、CTest `5/5`、VST3 Validator `47/47`、VST3/UI/backend ABI/C# contract 全通过；manifest `4b2a0fac94234011af10efda7fb0f30d`。
- M20-A2a 已提交：新增版本化 `solorig-behavior-graph` JSON 到 typed graph plan 的 lowering，保留底层端口/域/拓扑拒绝；最新验证为核心 `527/527`、CTest `5/5`、VST3 Validator `47/47`、VST3/UI/backend ABI/C# contract 全通过，manifest `3cd2c978bf4748d588496fa386b5b79e`。
- M20-A2b 已提交：新增 `circuit.json v2` → `CircuitNetlist` → `CompileMnaCircuit` → `Prepare` 的真实 MNA 边界；最新验证为核心 `530/530`、CTest `5/5`、VST3 Validator `47/47`、VST3/UI/backend ABI/C# contract 全通过，manifest `1fffc91f29044f4d88bb2d980261cde7`。
- M20-B1 已提交：`TypedVcoBbdClockedModel` 支持 caller-owned VCO/clocked BBD 模型与有界内部延迟反馈；最新验证为核心 `531/531`、CTest `5/5`、VST3 Validator `47/47`、VST3/UI/backend ABI/C# contract 全通过，manifest `7d64aa3710da46eca439c98450c6107d`。
- M20-B2 已提交：`TypedLfoMnaRuntime` 支持 caller-owned generic LFO 通过 parameter binding 调制真实 MNA 电位器网络；最新验证为核心 `533/533`、CTest `5/5`、VST3 Validator `47/47`、VST3/UI/backend ABI/C# contract 全通过，manifest `6ad672c7e6824178b344d23ccea13a85`。
- M20-B3 已提交：`TypedLfoMnaRuntime` 支持固定容量多 parameter binding，并驱动两级 caller-owned R/C Phaser MNA 网络；Phaser 已从 StableRefusal 提升为 RunnableGeneric；最新验证为核心 `534/534`、CTest `5/5`、VST3 Validator `47/47`、VST3/UI/backend ABI/C# contract 全通过，manifest `c99dfc1983874a6aaa2d63d2f4889d33`。
- M20-B4 已提交：新增四端 caller-owned optical hardware 电气组件，emitter 电阻、LDR 受光电阻、attack/release 状态和 MNA 联合求解均进入同一 Circuit v2/MNA 契约；核心 `538/538`、CTest `5/5`、Validator `47/47`、VST3/UI/backend ABI/C# contract 全通过，manifest `ba897feda5a24b1782ae2da3901a6ec9`。
- 通用 fan-out、延迟反馈、JSON 和自动 MNA 绑定仍排除；D3b/M19-A2 单延迟反馈继续由专用路径承担。

## 状态口径

| 状态 | 含义 |
| --- | --- |
| 已交付 | 已进入 Git 基线，并有解析、Compile/Prepare/Solve 或对应运行时测试证据。 |
| 本地候选 | 已在工作树实现并通过本地门禁，但尚未提交，不属于已发布基线。 |
| 受限 | 只有静态、理想化、契约解析或受限拓扑能力，不能按完整元件使用。 |
| 未实现 | 当前没有可运行实现，或运行时会明确拒绝。 |

“元件已交付”只说明通用模型能够运行，不代表某个商业型号已经校准，也不代表由这些元件组成的任意电路都能编译，更不代表具体设备达到 `reference-1to1`。

## 器件族代码组织规则

- 非线性器件按族维护独立的 `src/dsp/nonlinear_<family>.hpp/.cpp`，当前边界为 diode/Zener、BJT、JFET、MOSFET、opamp/OTA、triode、pentode、beam tetrode 和 vari-mu；transformer/core 等后续实现不得重新堆入单一大型文件。
- `src/dsp/nonlinear_device_common.hpp` 只保存跨族公共枚举，`nonlinear_device.hpp` 仅作为兼容 umbrella；通用且确实共享的有界数学辅助可以独立存在，但不得借共享辅助隐藏某一器件族的模型语义。
- 每个器件族的 evaluator 单元测试放在对应 `tests/nonlinear_<family>_tests.cpp`。网表 parser、ModelDatabase、Compile/Prepare/Solve 和外部 runtime 集成测试仍按各自系统边界集中维护，并以器件族命名的测试用例和 fixtures 保持可审计性。
- 中央 parser/dispatch 和通用 Newton/MNA 矩阵基础可以共享。stamp/binding 只有在不暴露求解器内部状态、不改变 POD/ABI 且能保持实时预分配契约时才独立拆分；不能为了文件数量制造跨层回调或动态分派。
- 新器件族必须先建立独立文件和测试边界，再接入中央注册/解析/求解入口；不得先向兼容 umbrella 或已有器件族文件追加实现，事后再补拆分。

## 当前元件支持矩阵

### 线性、供电与控制元件

| 元件/能力 | 状态 | 当前边界 |
| --- | --- | --- |
| 电阻 | 已交付 | 正值线性电阻。 |
| 电容 | 已交付 | backward-Euler 电压历史和初态。 |
| 电感 | 已交付 | backward-Euler 分支电流历史和初态。 |
| 独立电压源/供电 | 已交付 | ideal supply 使用 MNA branch unknown；C10a 增加 generic rectified-reservoir 的源阻抗、Sag、纹波与恢复状态，不代表商业电源或逐二极管整流波形。 |
| electrodynamic speaker load | 已交付 | 两端 voice-coil/electromechanical BE 状态与有界 suspension；可与既有反馈输出级联合 MNA，不是箱体 IR、商业扬声器或完整声辐射模型。 |
| 电位器 | 已交付 | Compile 时展开为两个正电阻；版本化 binding、原子邮箱和子步 BE 平滑可实时扫动，旧静态结果兼容。 |
| 两端开关 | 已交付 | `open/closed` 映射为声明的有限正电阻，支持原子 state 自动化与平滑。 |
| dynamic relay | 已交付 | 单线圈/单常开触点，含线圈 BE、pull-in/drop-out hysteresis、operate/release 和确定性有界 bounce；不含多刀/转换触点或商业 relay。 |
| multi-contact relay routing | 已交付 | 1..4 pole、共享线圈/机械状态、固定 `[common,NC,NO]` routing、有限 gap break-before-make；不含任意开关矩阵或商业 relay。 |
| inline passive subcircuit | 已交付 | 显式局部端口到父节点映射，编译期确定性扁平化 R/C/L 并保留动态初态；外部、递归、主动、非线性和多端 macro-model 仍拒绝。 |
| 供电参数自动化 | 已交付 | nominal voltage 可实时有界更新；C10a Sag/ripple 状态不因参数更新重置。 |
| VCCS | 已交付 | 四端线性跨导原语。 |
| 基础理想运放/VCVS | 已交付 | 有限增益及可选限幅；不代表真实芯片。 |
| WDF one-port、Series/Parallel/R-type adaptor | 受限 | 已有可执行子集和理想数学变压器散射；尚非任意 WDF 树或真实磁性变压器。 |

### 非线性与有源元件

| 元件/能力 | 状态 | 当前边界 |
| --- | --- | --- |
| Shockley diode | 已交付 | 保持通用静态 IV/Jacobian，C9a 增加 generic 电压相关结电容、BE 历史、静态温度/漏电修正和解析 Jacobian；没有反向恢复、噪声或商业型号校准。 |
| Zener | 已交付 | 在双指数静态模型上复用 C9a 动态结状态和温度/漏电修正；没有商业型号校准或 reverse-recovery/载流子存储。 |
| NPN/PNP BJT | 已交付 | clean-room Ebers-Moll v1 兼容，并增加 generic Early 有限输出、BE/BC depletion companion 与静态温度修正；不含 charge-storage/reverse-recovery、击穿、自热、噪声或商业器件语义。 |
| N/P channel JFET | 已交付 | clean-room square-law v1 兼容，并增加有限 channel-length modulation、静态温度、GS/GD PN leakage 与 depletion companion；不含 avalanche、charge storage、reverse recovery、自热、噪声或商业器件语义。 |
| N/P channel MOSFET | 已交付 | enhancement square-law v1 兼容，并增加沟道 CLM/subthreshold、静态温度、source-tied body-to-drain PN 支路和 GS/GD depletion companion；沟道保持 drain/source 对称，body diode 固定方向并有意打破整体对称。 |
| bounded/dynamic opamp | 已交付 | v1 为无频率状态的有界电压放大器；C9e 沿用四端拓扑增加有限 GBW/主极点、slew、输入偏置/失调、输入/输出阻抗、输出限流、内部供电轨边界和 Reset 状态，不含显式供电脚或商业芯片语义。 |
| bounded OTA | 已交付 | 无频率状态的有界跨导放大器；没有真实芯片控制、电源、频率和噪声特性。 |
| triode | 已交付 | M18-C6 generic space-charge v1 已由 `667d1f2` 提交；没有栅极导通/漏电、极间电容、加热器、温度、自热、老化、microphonics 和商业管参数。 |
| pentode | 已交付 | M18-C7a generic space-charge/screen-share v1 已由 `cd5f1bc` 提交；没有商业管型、真实 suppressor-grid/beam-forming、secondary emission、反向/栅极导通、动态电容和热行为。 |
| beam tetrode | 已交付 | M18-C7b generic beam-capture v1 已由 `75f061d` 提交；平方有理 capture 不是实际 beam-forming，不含 secondary-emission kink、反向/栅极导通、动态电容、热行为或商业管参数。 |
| vari-mu | 已交付 | M18-C7c generic 三端 remote-cutoff v1 已由 `bf47d2a` 提交；sidechain/envelope、attack/release、商业管型、动态电容、栅流和热行为不在范围内。 |
| coupled-inductor / transformer-core | 部分交付 | C8a `lossy-linear` 保持兼容；C8b 增加独立 `saturating-hysteretic` generic 模型，`magnetizing-inductance` 明确定义零点小信号磁通斜率，并进入 tanh 饱和和 Newton 动态印章；另含磁滞状态、剩磁初态、限幅和双绕组 BE/Reset/过采样路径。没有商业磁芯参数、真实磁滞回线、涡流、温度或 1:1 设备验收。 |

## 效果器类别覆盖判断

| 效果器类别 | 当前判断 | 主要依据或缺口 |
| --- | --- | --- |
| 被动滤波、基础 EQ、音调网络 | 基础可表达 | R/C/L 与 D1a 实时电位器已具备；任意复杂拓扑仍受限。 |
| 二极管削波、限幅、简单整流 | 通用模型可实现 | diode/Zener 静态及 C9a generic 动态结路径已交付；reverse-recovery、高精度电荷模型和具体型号校准未完成。 |
| 晶体管 Fuzz、Boost、简单失真级 | 通用模型可实现 | BJT/JFET/MOSFET 已交付；高阶寄生、温度、容差和实机校准未完成。 |
| 运放 Overdrive/Distortion、基础有源滤波 | 结构上可实现 | bounded/dynamic opamp 已交付有限主极点、slew、阻抗、限流和内部轨边界；没有显式供电脚、复杂多极点或商业模型。 |
| OTA/VCA 压缩、滤波和调制 | 部分可表达 | OTA、D2b2 generic VCA 和 D2c1 envelope follower 已交付；完整 sidechain、控制馈通和具体芯片行为仍不完整。 |
| Wah、Phaser、Tremolo | 部分可表达 | 基础滤波、D1a 实时控制和 D2c2 generic LFO 已交付；当前仅有独立 runtime 与 D2e4/D2e7 的受限 typed 控制/组合路径，光学耦合及完整行为图仍缺失。 |
| 光学压缩、光学 Tremolo、Uni-Vibe | 部分可表达 | D2d1 已交付 generic emitter/LDR 非线性映射及 attack/release 记忆，D2e6 提供受限 emitter -> resistance typed runtime；仍缺真实 LED/灯泡电气语义、自动电路连接和完整行为图。 |
| Chorus、Flanger、Analog Delay | 部分可表达 | D2a generic clocked BBD/delay、D2b1 VCO、D2c2 LFO 和 D3a 显式 VCO -> BBD clock typed runtime 已交付；clock feedthrough、抗混叠、反馈组合和完整行为图仍缺失。 |
| Spring Reverb | 部分可表达 | D2d2 已有固定容量 generic modal spring ringing/decay，D2e2 提供严格单 signal BBD/VCO/spring block 的固定容量单速率串联执行；当前仍只有独立 runtime 与受限组合路径，不含 drive/pickup coil 电气阻抗、逐段传播、非线性碰撞、商业 tank 校准或完整行为图。 |
| 真空管前级单块/前级通道 | 通用模型初步能力 | generic triode/pentode/beam tetrode/vari-mu 已交付；真实栅流、极间电容、供电和具体管型尚未完成。 |

上述“可实现”表示通用模型具备搭建条件，不表示仓库已经交付相应产品模块，也不表示声音达到具体设备的 1:1 精度。

## 箱头覆盖的关键缺口

要覆盖绝大多数电子管箱头，仅有 generic 管模型不够，至少还需要：

1. 继续补齐屏栅、抑制栅和功率管高阶行为；
2. 输出变压器的耦合电感、绕组损耗、漏感、励磁、磁芯饱和与磁滞；
3. 整流器、储能电容、供电阻抗、纹波、Sag 和负载恢复；
4. 扬声器随频率和振幅变化的复阻抗负载，以及它与输出级/负反馈的交互；
5. 更完整的 triode 栅极导通、漏电、极间电容、温度和动态行为；
6. 多级反馈、相位反相器、功放和电源耦合所需的通用拓扑与联合求解能力；
7. 噪声、热效应和元件容差；
8. 具体型号的可审计参数来源及 SPICE/golden/硬件测量校准。

晶体管或混合箱头还需要补齐功率 BJT/MOSFET 的 body/结电容、SOA、温度、自热和保护网络行为。当前 IR 箱体卷积属于输出后的数字箱体处理，不能代替功放看到的动态扬声器负载。

## 非元件平台缺口

即使补齐所有器件族，以下平台能力不完成，也不能证明覆盖绝大多数电路：

- canonical SPQR/rigid decomposition、任意嵌套 WDF tree、多端口 R scattering 和 WDF/MNA 混合树；
- WDF 非线性联合求解、DC 工作点和 K-method 增量优化；
- 外部/递归/主动/多端 subcircuit macro-model、跨实例状态共享和完整运行时行为绑定；D1b2 仅覆盖显式端口映射的 inline R/C/L 编译期扁平化；
- 无锁、无分配的实时参数邮箱，以及电位器、开关、继电器和供电变化；
- 行为岛之间的完整任意路由；当前已具备 D2e2-D2e7 的受限 single-signal、typed port/route、控制域、detector、emitter -> resistance、envelope -> VCA composite，以及 D3b 的单一延迟反馈路径和独立 runtime，但一般 feedback、sidechain、动态端口、跨图状态、JSON 图加载与完整效果器语义仍缺失；
- 完整且授权可审计的型号数据库；
- 每个模块的 DC、频响、静态曲线、谐波/互调、动态响应、参数扫动、采样率、收敛及误差阈值证据。

## 纳入项目计划

后续工作按下表纳入 M18-M20。登记计划不等于完成；M18-C7c 已由 `bf47d2a` 提交，M18-C8a 已由 `f33664a` 提交，C8b 及后续子里程碑仍需逐项登记范围、测试和禁止扩展。

| 阶段 | 计划内容 | 完成判据摘要 |
| --- | --- | --- |
| M18-C6 | generic triode 主线化 | 已由 `667d1f2` 完成提交；保持商业管型及高阶物理边界。 |
| M18-C7a | generic pentode | 已由 `cd5f1bc` 完成提交；保持商业管型及高阶物理边界。 |
| M18-C7b | generic beam tetrode | 已由 `75f061d` 完成提交；保持真实 beam-forming、secondary emission、商业管型及高阶物理边界。 |
| M18-C7c | vari-mu | 已由 `bf47d2a` 完成提交；商业管型与压缩器行为仍排除。 |
| M18-C8a | coupled inductor、lossy-linear transformer/core | 已由 `f33664a` 提交；核心 283/283、C ABI 9/9、VST3 7/7、Windows UI 6/6、CTest 5/5、Validator 47/47、C# 契约；manifest `517fdf472e9d40dd891595daf640c71d`。饱和/磁滞/剩磁留给 C8b。 |
| M18-C8b | nonlinear transformer/core | 已由 `9b06c70` 交付 self-authored generic `saturating-hysteretic` v1；不得冒充商业磁芯。 |
| M18-C9a | diode/Zener 动态结 | 已由 `9b06c70` 交付 generic depletion 结电容、静态温度/漏电修正、BE/Reset/过采样/外部 runtime；不含 reverse-recovery、商业型号或 SPICE 等价。 |
| M18-C9b+ | 其余半导体与运放高阶物理 | C9b-C9e generic BJT/JFET/MOSFET/dynamic opamp 已由 `9b06c70` 交付；更复杂载流子存储、显式供电脚、自热、噪声与商业模型仍排除。 |
| M18-C10 | 电源与负载交互 | C10a/C10b 已由 `9dc9368` 交付 generic 整流储能/Sag 与 electrodynamic speaker/feedback；不构成完整箱头能量闭环。 |
| M18-D1 | 实时控制和跨网络语义 | D1a、D1b1/D1b3 relay、D1b2 inline passive flattening 已由 `9dc9368` 交付；外部/递归 macro-model 仍待后续。 |
| M18-D2 | 行为模块 | D2a-D2d2 已交付独立 BBD/VCO/VCA/envelope/LFO/optocoupler/modal spring；D2e1-D2e7 的 Compile/lowering、固定容量 runtime、typed port/route、控制域、detector、emitter -> resistance 和 envelope + VCA composite 已随 M19-A1 提交 `2e6a8bf`。这些仍是受限显式链路，不做隐式域转换或外部输入 fan-out；feedback、sidechain、动态端口、跨图状态、JSON 外部加载和商业/SPICE/1:1 语义仍明确排除。 |
| M18-B 后续收口 | 通用拓扑编译与混合求解 | canonical SPQR/rigid、多端口 R、WDF/MNA 混合树、非线性联合求解和 DC/K-method 闭环。 |
| M18-D3a | 显式 BBD 时钟控制链 | 已形成未提交本地候选：独立 `generic_bbd_clocked.*` 与 typed `VCO.out(control) -> BBD.clock` runtime 支持显式 `input.audio -> BBD.audio_in`、`BBD.out(audio) -> output.audio`；核心 `496/496`、CTest `5/5`、Validator `47/47` 及宿主/UI/C ABI/C# contract 全通过，manifest `1396cc27618d40dba80d21fcf378cdae`。仍排除 fixed-clock 替代、feedback/sidechain、clock feedthrough、电荷传输、商业抗混叠、JSON、SPICE/1:1。 |
| M18-D3b | 受限单反馈 BBD/delay 链 | 已形成未提交本地候选：独立 `generic_bbd_feedback.*` 与 typed feedback runtime 仅允许一个显式 `BBD.out(audio) -> BBD.feedback(audio)` 延迟边，反馈值来自上一采样/子步状态；核心 `504/504`、CTest `5/5`、Validator `47/47` 及宿主/UI/C ABI/C# contract 全通过，manifest `6b027d3ed7df4e54a7758b8f98b3f0e4`。一般代数环、无延迟 cycle、多反馈、隐式 fan-out、sidechain 和跨图状态继续拒绝。 |
| M19 | 代表性单块与箱头验证 | M19-A1 已由 `2e6a8bf` 完成：以 self-authored generic fixture 验证 D2e2-D2e7 五条受限效果链的 Compile/Prepare、Reset、采样率/oversampling、确定性和拒绝边界；核心 `489/489`、CTest `5/5`、Validator `47/47` 及宿主/UI/C ABI/C# contract 全通过，manifest `75151e09517543caa45357ec695cf122`。完整 SPICE/golden/实机校准、商业效果器和 `reference-1to1` 仍需后续资料，资料不足保持 `blocked`。 |
| M19-A2 | 反馈代表性验证 | 已形成未提交本地候选：self-authored generic VCO -> BBD 单延迟反馈 fixture 验证 Compile/Prepare、上一采样/子步反馈、Reset、采样率/oversampling、非有限和稳定拒绝；不宣称完整 Chorus/Flanger、真实 BBD clock feedthrough、商业硬件、SPICE 或 `reference-1to1`。 |
| M19-B1 阶段 1 | generic 箱头前后级闭环接口审计 | 已完成静态审计：circuit v2 与 MNA 底层可以在同一实例中容纳 triode/pentode/beam tetrode、C10a Sag、C8b transformer/core 和 C10b speaker，但当前只有单 input/单 output 的 netlist 端口和组件 pins，没有前级/功放/电源/变压器/负载的阶段化 typed route contract。阶段 2 先实现显式 Compile-only contract 与 stable refusal；不在 content 层固定拓扑、控制值或隐藏状态。 |
| M19-B1 阶段 2b/3b | generic 箱头真实 pin/net contract 与 MNA lowering/runtime | 未提交本地候选：2b 使用真实 triode/pentode/beam、C10a、transformer 四端和 speaker 两端；secondary return 保留真实 pin 名但显式接到 `ground`，并将 terminal 合并进 ground net，output 仍复用真实 `secondary_dot`。3b 已证明 caller-owned netlist 可 Compile/Prepare/Solve，48/96/192 kHz、1x/4x、Reset 和有限确定性序列通过；浮动 secondary 回归稳定拒绝。 |
| M20-A1 | 通用 typed behavior 多边界路由与统一 runtime | 已由 `b2010d4` 提交：显式支持 audio/control/detector/emitter 输入和 audio/control/resistance 输出，统一调度七类已授权 generic runtime；核心 `524/524`、CTest `5/5`、Validator `47/47`、manifest `4b2a0fac94234011af10efda7fb0f30d`。通用 fan-out、延迟反馈、JSON 和自动 MNA 绑定仍排除。 |
| M20-A2a | 版本化 typed behavior JSON lowering | 已提交：`solorig-behavior-graph` JSON 解析到已验证多域 typed plan；核心 `527/527`、CTest `5/5`、Validator `47/47`、manifest `3cd2c978bf4748d588496fa386b5b79e`。参数自动合成、外部模型加载和 JSON 到 CircuitNetlist/MNA 仍排除。 |
| M20-A2b | caller-owned circuit v2 JSON 到实际 MNA | 已提交：JSON 解析、MNA Compile、Prepare 和实际 Solve 已串通；核心 `530/530`、CTest `5/5`、Validator `47/47`、manifest `1fffc91f29044f4d88bb2d980261cde7`。typed behavior 自动转电路、通用反馈、商业模型和参考等价仍排除。 |
| M20-B1 | caller-owned 动态时钟 BBD 与有界反馈组合 | 已提交：显式 VCO/BBD model payload、动态 clock、内部延迟 feedback、Reset 和非法模型拒绝；M19-C Chorus/Flanger 代表类提升为 RunnableGeneric；核心 `531/531`、CTest `5/5`、Validator `47/47`、manifest `7d64aa3710da46eca439c98450c6107d`。 |
| M20-B2 | Wah/Phaser caller-owned 调制组合 | 已提交：Wah 已通过 `TypedLfoMnaRuntime` 完成 LFO → parameter binding → MNA Solve 的真实组合；M20-B2 时 M19-C 拆分为 16 类后 14 类 RunnableGeneric。 |
| M20-B3 | Phaser 多级调制组合与 optical hardware 电气边界 | 已提交：固定容量多 binding 驱动两级 R/C dry/wet MNA 网络，Phaser 已提升为 RunnableGeneric；核心 `534/534`、CTest `5/5`、Validator `47/47`、manifest `c99dfc1983874a6aaa2d63d2f4889d33`。 |
| M20-B4 | optical hardware 电气模型与代表性组合 | 已提交：四端 `Optocoupler` 进入 Circuit v2 parser/Compile/Prepare/Solve/Reset；emitter 电气端驱动 LDR 受光电阻端，代表性 fixture 覆盖 192 kHz/4x 和稳定拒绝；不宣称商业光耦校准、SPICE/golden 或 reference-1to-1。 |
| M20-C3 | 性能与产品全量回归 | 部分完成：AudioGraph 节点实测 load、192 kHz/过采样、延迟、实时契约、C ABI、VST3、UI 和 Validator 有回归证据；独立程序 UI 仍仅展示静态 CPU 估算，真实 ASIO 和 SPICE/golden/实机证据仍未完成。 |
| M22-A1 | MNA 固定容量多输入/多输出求解边界 | 已提交 `b75200d`：span 输入、多个输出快照、旧单输入兼容和稳定错误边界；不等于 runtime/typed behavior/sidechain 已完成。 |
| M22-B1 | 外部 MNA 多端口 runtime 第一阶段 | 已提交：等数量单声道端口按 AudioBuffer 声道序映射，最多 8 端口，Parse/Compile/Prepare/Process 和 descriptor 拒绝边界有测试；命名端口图和任意多端拓扑仍未完成。 |

## 本地候选：M19-C 代表性 generic 单块/箱头覆盖审计

### 范围与证据

- `fixtures/whitebox/m19c_representative_pedal_fixtures.*` 提供被动音色、二极管过载、BJT/JFET/MOSFET boost/fuzz 和 dynamic opamp 的 caller-owned MNA fixture；M19-A1/M19-A2/M19-B1 提供其余已授权 generic behavior、反馈和 tube-amp 代表链路。
- `tests/m19c_validation_harness.*` 对代表性类别统一检查 Compile、Prepare、有限非平凡输出、Reset、192 kHz/过采样和拒绝边界；M20-B4 后当前 16 类均为 RunnableGeneric，剩余风险转为通用拓扑、商业模型与外部参考证据，而非把代表性 fixture 当成完整产品覆盖。
- M19-C/M20-B4 只证明代表性 generic 结构和拒绝边界，不证明完整效果器产品、商业硬件、SPICE/golden 或 `reference-1to-1`；原先三类 StableRefusal 已通过 M20-B2/B3/B4 的代表性组合处理，通用拓扑与产品级外部证据仍未完成。

## M19-B1 阶段 1 审计结论

- `circuit.json v2` 的真实表达是一个版本化 netlist：`nodes`/`ports`/`supplies`/`components`/`parameter_bindings` 和 solver 配置。M22-A1/B1 已解除单 input 的 parser/求解/runtime 限制，但仍没有独立的前级、功放、输出变压器或 speaker stage/route 类型，也没有 named-port AudioGraph contract。
- generic triode 使用 `[plate, grid, cathode]` 三节点 KCL/Jacobian，无 branch unknown 或持久动态 history；pentode 与 beam tetrode 使用 `[plate, screen, grid, cathode]` 四节点 KCL/Jacobian，同样没有独立 branch。Compile 会拒绝控制节点复用、缺 pin、错误模型或未授权 model reference。
- C10a `rectified-reservoir` supply 是 `[positive, negative]` 两端电压源 branch unknown，Newton stamp 读取 branch current 并维护 Sag/ripple phase；Prepare 预分配 state，Reset 恢复 `initialSag`，失败稳定返回 `NonFinite`、`SingularMatrix` 或 `NonConvergent`。
- C8b `saturating-hysteretic` coupled-inductor/core 使用四个绕组节点、primary/secondary 两个 branch unknown，以及独立 flux/hysteresis history；C10b electrodynamic speaker 使用两节点动态 branch unknown 和 current/velocity/displacement history。两者均在同一 MNA Newton/BE/oversampling 路径中 Prepare/Reset，并遵守 Solve 的无分配、无锁、无文件边界。
- 已有 `dynamic opamp` 负反馈驱动 speaker 的 MNA 测试证明“一个已声明网表内的输出级与动态负载联合求解”可行；现有 triode/pentode/beam/core/supply 证据分别覆盖 Compile/Prepare/Solve、Reset、过采样和奇异/不收敛边界。但尚无一个可审计的 generic 前级 -> 功放 -> transformer/core -> speaker 阶段契约，也没有证据证明这些阶段已经以稳定接口组合。

### 阶段 2b/3b 准入结论与 stable refusal

底层 MNA 数学和 branch/state 资源足以容纳 self-authored generic 最小网表；阶段 2b 已落地独立 typed Compile plan（固定容量、真实 pin、显式节点/端口方向和域、完整 terminal membership、无隐藏状态），阶段 3b 将 Ready plan 与 caller-owned netlist 做映射校验后调用既有 MNA Compile/Prepare/Solve。无法满足任一契约的图统一返回 stable refusal。

阶段 2b/3b 的最小 fixture 由真实 circuit nodes 和 MNA 元件组成，不能由 content 固定数值、数字 IR/EQ、隐藏反馈或商业命名补齐。secondary return 显式连接 reference ground 后，fixture 已取得有限 Solve；Newton 上限 `256` 是 fixture 调用方声明值，不是 content 默认值。浮动次级回归必须稳定拒绝，不能静默修复。

风险保持明确：现有 dynamic opamp 不抽取 C10a supply branch 的功率级电流，speaker 只提供 generic electromechanical 负载，transformer/core 不是商业磁芯，triode/pentode/beam 不是具体功率管；因此当前不能宣称完整电源-功放-变压器-扬声器能量闭环、箱体声学、SOA/保护、SPICE/golden 或 `reference-1to-1`。

## M19-B1 阶段 2b/3b 本地候选

- 独立 `src/content/generic_tube_amp_compile.*`、`fixtures/whitebox/m19b1_generic_tube_amp_compile_fixture.*` 和 `tests/generic_tube_amp_compile_tests.cpp` 定义固定容量 `std::array` typed request/plan。成功只表示结构契约可供后续 lowering，不表示箱头闭环已运行。
- 允许的最小结构由真实 terminal/net 表达：外部 input 节点经 `input_coupling` 到 triode grid；triode plate 经 `interstage_coupling` 到 pentode/beam grid；单一 C10a positive/negative rail 通过声明的 `triode_plate_load`、`screen_feed`、`power_plate_load` 供电；功率管 plate 与 transformer `primary_dot` 相连、`primary_return` 回到 supply rail；transformer 四端 secondary 与 speaker 两端相连，speaker positive/secondary dot 节点同时作为唯一 output measurement。不存在虚构的 `plate_supply`、`screen_supply` 或 `speaker.output` 端口。共享模拟 net 仅接受 contract 列出的完整 terminal membership，额外 terminal、缺失 terminal、短接、跨域和未声明连接稳定拒绝。
- Compile 为 `noexcept`、无文件/JSON 访问、无 runtime 分配、无 MNA/Solve 副作用；失败返回单一错误且不带 partially prepared plan。错误分类包括 `MissingStage`、`DuplicateStage`、`DuplicateRoute`、`InvalidModelReference`、`IncompatibleConnection`、`CapacityExceeded`、`NonFinite`、fan-in/out、cycle 和供电/变压器连接拒绝。
- 本地候选验证：`generic_tube_amp_compile` 专项用例已并入核心，覆盖合法 pentode/beam-tetrode、缺失/重复/错接、node/pin 篡改、fan-in/fan-out、cycle、模型/容量/非有限和失败原子性；本次增量构建核心 `513/513`、CTest `5/5`、VST3 Validator `47/47`、VST3/UI/backend ABI/C# contract 均通过，manifest `8660354af48f41f6a78486645060b361`。
- 2b Compile 固定容量为 11 个 net、11 个 caller-owned nodes、最多 9 个 terminal/net，`noexcept`、无 JSON/文件/MNA 副作用；3b 的独立 lowering/runtime 不生成组件、节点、参数，也不修改共享 MNA。ground net 显式承载 supply negative、偏置回路和 transformer/speaker secondary return，secondary pin/domain 仍保持真实定义。专项测试覆盖节点/pin、供电/transformer polarity/speaker output、缺失/额外器件、浮动次级拒绝、Prepare 清理、Reset、48/96/192 kHz、1x/4x、有限确定性序列和 NaN `NonFinite`。不得引入商业箱头、SPICE/golden/实机等价、功率管 SOA/保护、真实箱体声学、复杂反馈或外部任意 JSON 加载。

## 覆盖完成口径

“覆盖绝大多数单块效果器和箱头”至少同时满足以下条件后才能评估：

- 上述核心器件、控制元件和行为模块均有可运行实现与稳定拒绝边界；
- 代表性拓扑不依赖固定电路识别或专用硬编码旁路；
- 旋钮、开关、供电和状态变化能在实时约束内工作；
- 选定的覆盖测试集包含失真/Boost/Fuzz、滤波/Wah/Phaser、压缩/光学、调制/BBD、混响、电子管前后级和功放负载；
- 每类至少有独立参考证据，具体设备只有满足完整资料与误差阈值时才标记 `reference-1to1`。

相关契约和管理基线见 [CIRCUIT_JSON_V2.md](CIRCUIT_JSON_V2.md)、[MODEL_DATABASE_V1.md](MODEL_DATABASE_V1.md) 与 [项目需求进度与注意事项.md](项目需求进度与注意事项.md)。
