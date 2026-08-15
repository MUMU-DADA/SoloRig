# SoloRig 白盒效果器支持说明

> 状态日期：2026-08-15。本文描述通用白盒电路路径的真实能力边界，不包含 NAM/NAM A2 黑盒覆盖，也不把原生数字 DSP 误记为元件级白盒。

## 结论

当前白盒运行时已经具备常见无源器件、独立电源、基础受控源，以及 generic dynamic diode/Zener、BJT、JFET、MOSFET、dynamic opamp、bounded OTA、triode、pentode、beam tetrode、vari-mu、lossy-linear coupled-inductor 和 saturating-hysteretic transformer/core 模型。C8b-C9e 已由 `9b06c70` 提交；C10a/C10b、D1a/D1b1/D1b2/D1b3 和 D2a generic clocked BBD/delay 已由 `9dc9368` 提交，D2b1 VCO、D2b2 VCA、D2c1 envelope follower、D2c2 generic LFO、D2d1 optocoupler/LDR response 已分别由 `4774d79`、`4a4bfa1`、`dc2f1b6`、`ebd2438`、`f4c5b14` 提交。D2d2 generic modal spring response 已形成通过统一门禁的本地候选，D2e1 behavior graph Compile/lowering route contract 也已形成本地候选。这些仍只构成 self-authored clean-room generic 能力，不构成商业器件或 1:1 证据。

当前仍不能宣称覆盖“绝大多数单块效果器和箱头”。主要阻塞不只是元件数量，还包括实时旋钮/开关、行为模块、复杂拓扑编译、器件高阶物理、功放与电源/负载交互，以及 SPICE/golden/实机误差验证。

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
| Wah、Phaser、Tremolo | 部分可表达 | 基础滤波、D1a 实时控制和 D2c2 generic LFO 已交付；光学耦合及完整行为图仍缺失。 |
| 光学压缩、光学 Tremolo、Uni-Vibe | 部分可表达 | D2d1 已交付 generic emitter/LDR 非线性映射及 attack/release 记忆；仍缺真实 LED/灯泡电气语义、自动电路连接和完整行为图。 |
| Chorus、Flanger、Analog Delay | 部分可表达 | D2a generic clocked BBD/delay、D2b1 VCO 和 D2c2 LFO 已交付；尚未形成 BBD clock 联接、clock feedthrough、抗混叠或完整行为图。 |
| Spring Reverb | 部分可表达 | D2d2 已有固定容量 generic modal spring ringing/decay 本地候选；不含 drive/pickup coil 电气阻抗、逐段传播、非线性碰撞、商业 tank 校准或完整行为图。 |
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
- 行为岛之间的完整路由；D2a-D2d2 已交付彼此独立的 BBD/VCO/VCA/envelope/LFO/optocoupler/modal spring，D2e1 仅推进图级 Compile/lowering 契约，均不代表完整效果器模拟；
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
| M18-D2 | 行为模块 | D2a-D2d2 已交付独立 BBD/VCO/VCA/envelope/LFO/optocoupler/modal spring；D2e1 固定容量 graph Compile/lowering 为本地候选，块级执行、真实换能器和完整行为图仍待逐项推进。 |
| M18-B 后续收口 | 通用拓扑编译与混合求解 | canonical SPQR/rigid、多端口 R、WDF/MNA 混合树、非线性联合求解和 DC/K-method 闭环。 |
| M19 | 代表性单块与箱头验证 | 先以 generic 模块验证覆盖面，再对资料完整的目标做 SPICE/golden/实机校准；资料不足保持 `blocked`。 |
| M20 | 性能与产品全量回归 | 192 kHz/过采样、CPU、延迟、无分配、稳定性、C ABI、VST3、UI 和 Validator 全量通过。 |

## 覆盖完成口径

“覆盖绝大多数单块效果器和箱头”至少同时满足以下条件后才能评估：

- 上述核心器件、控制元件和行为模块均有可运行实现与稳定拒绝边界；
- 代表性拓扑不依赖固定电路识别或专用硬编码旁路；
- 旋钮、开关、供电和状态变化能在实时约束内工作；
- 选定的覆盖测试集包含失真/Boost/Fuzz、滤波/Wah/Phaser、压缩/光学、调制/BBD、混响、电子管前后级和功放负载；
- 每类至少有独立参考证据，具体设备只有满足完整资料与误差阈值时才标记 `reference-1to1`。

相关契约和管理基线见 [CIRCUIT_JSON_V2.md](CIRCUIT_JSON_V2.md)、[MODEL_DATABASE_V1.md](MODEL_DATABASE_V1.md) 与 [项目需求进度与注意事项.md](项目需求进度与注意事项.md)。
