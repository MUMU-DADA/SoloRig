# SoloRig 白盒效果器支持说明

> 状态日期：2026-08-14。本文描述通用白盒电路路径的真实能力边界，不包含 NAM/NAM A2 黑盒覆盖，也不把原生数字 DSP 误记为元件级白盒。

## 结论

当前白盒运行时已经具备常见无源器件、独立电源、基础受控源，以及 diode/Zener、BJT、JFET、MOSFET、bounded opamp/OTA、triode、generic pentode 和 generic beam tetrode 等第一批通用有源模型；generic vari-mu 已完成本地实现和全量门禁，尚待提交进入 Git 基线。它可以作为简单模拟削波、Fuzz、Boost、滤波、音调网络和部分运放/OTA、电子管级电路的结构基础。

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
| 独立电压源/供电 | 已交付 | MNA branch unknown；供电可进入网表，但尚无完整电源 Sag/纹波模型。 |
| 静态电位器 | 受限 | Compile 时展开为两个电阻；位置变化需要重新 Compile/Prepare，不能在音频处理中实时扫动。 |
| 开关 | 受限 | `circuit.json v2` 可以解析和校验，MNA 运行时返回 `UnsupportedComponent`。 |
| VCCS | 已交付 | 四端线性跨导原语。 |
| 基础理想运放/VCVS | 已交付 | 有限增益及可选限幅；不代表真实芯片。 |
| WDF one-port、Series/Parallel/R-type adaptor | 受限 | 已有可执行子集和理想数学变压器散射；尚非任意 WDF 树或真实磁性变压器。 |

### 非线性与有源元件

| 元件/能力 | 状态 | 当前边界 |
| --- | --- | --- |
| Shockley diode | 已交付 | 通用静态 IV/Jacobian；没有结电容、反向恢复、温度和噪声。 |
| Zener | 已交付 | 通用双指数静态模型；没有商业型号校准和动态击穿行为。 |
| NPN/PNP BJT | 已交付 | clean-room Ebers-Moll v1；没有 Early 效应、结电容/电荷存储、击穿、温度/自热和噪声。 |
| N/P channel JFET | 已交付 | 通用对称 square-law v1；栅极电流为零，没有 PN 栅结、寄生电容、沟道长度调制和击穿。 |
| N/P channel MOSFET | 已交付 | 通用 enhancement square-law v1；没有 body 端、body diode/effect、亚阈值、寄生电容/电荷和沟道长度调制。 |
| bounded opamp | 已交付 | 无频率状态的有界电压放大器；没有供电脚、阻抗、偏置/失调、共模范围、slew rate、GBW/极点、PSRR/CMRR 和噪声。 |
| bounded OTA | 已交付 | 无频率状态的有界跨导放大器；没有真实芯片控制、电源、频率和噪声特性。 |
| triode | 已交付 | M18-C6 generic space-charge v1 已由 `667d1f2` 提交；没有栅极导通/漏电、极间电容、加热器、温度、自热、老化、microphonics 和商业管参数。 |
| pentode | 已交付 | M18-C7a generic space-charge/screen-share v1 已由 `cd5f1bc` 提交；没有商业管型、真实 suppressor-grid/beam-forming、secondary emission、反向/栅极导通、动态电容和热行为。 |
| beam tetrode | 已交付 | M18-C7b generic beam-capture v1 已由 `75f061d` 提交；平方有理 capture 不是实际 beam-forming，不含 secondary-emission kink、反向/栅极导通、动态电容、热行为或商业管参数。 |
| vari-mu | 本地候选 | M18-C7c generic 三端 remote-cutoff v1 已通过全量门禁、尚待提交；sidechain/envelope、attack/release、商业管型、动态电容、栅流和热行为不在范围内。 |
| 真实变压器/磁芯 | 未实现 | 没有绕组电阻、励磁/漏感、饱和及磁滞。 |

## 效果器类别覆盖判断

| 效果器类别 | 当前判断 | 主要依据或缺口 |
| --- | --- | --- |
| 被动滤波、基础 EQ、音调网络 | 基础可表达 | R/C/L 和静态电位器已具备；实时旋钮和任意复杂拓扑仍受限。 |
| 二极管削波、限幅、简单整流 | 通用模型可实现 | diode/Zener 已交付；高精度动态二极管和具体型号校准未完成。 |
| 晶体管 Fuzz、Boost、简单失真级 | 通用模型可实现 | BJT/JFET/MOSFET 已交付；高阶寄生、温度、容差和实机校准未完成。 |
| 运放 Overdrive/Distortion、基础有源滤波 | 结构上可实现 | bounded opamp 已交付；真实运放频率、电源和瞬态限制未完成。 |
| OTA 压缩、滤波和调制 | 部分可表达 | OTA 核心存在；包络、控制电流、实时参数和具体芯片行为不完整。 |
| Wah、Phaser、Tremolo | 部分可表达 | 基础滤波和有源器件存在；实时电位器、LFO、开关及器件动态特性仍缺失。 |
| 光学压缩、光学 Tremolo、Uni-Vibe | 未完整覆盖 | 缺少 LED/LDR/灯泡耦合、记忆和时间常数模型。 |
| Chorus、Flanger、Analog Delay | 未完整覆盖 | 缺少 BBD、clock、VCO/LFO、插值/抗混叠和行为岛真实绑定。 |
| Spring Reverb | 未完整覆盖 | 只有行为 lowering/契约基础，没有可运行弹簧与换能器模型。 |
| 真空管前级单块/前级通道 | 通用模型初步能力 | generic triode/pentode/beam tetrode 已交付，vari-mu 是本地候选；真实栅流、极间电容、供电和具体管型尚未完成。 |

上述“可实现”表示通用模型具备搭建条件，不表示仓库已经交付相应产品模块，也不表示声音达到具体设备的 1:1 精度。

## 箱头覆盖的关键缺口

要覆盖绝大多数电子管箱头，仅有 triode 前级模型不够，至少还需要：

1. 提交并独立复验 generic vari-mu；继续补齐屏栅、抑制栅和功率管高阶行为；
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
- subcircuit 跨网络行为绑定、完整端口语义和运行时状态管理；
- 无锁、无分配的实时参数邮箱，以及电位器、开关、继电器和供电变化；
- BBD、delay、VCO、VCA、envelope、LFO、spring、optocoupler 等行为模块；
- 完整且授权可审计的型号数据库；
- 每个模块的 DC、频响、静态曲线、谐波/互调、动态响应、参数扫动、采样率、收敛及误差阈值证据。

## 纳入项目计划

后续工作按下表纳入 M18-M20。登记计划不等于完成；M18-C7b 已提交，M18-C7c 已完成本地实现与全量门禁、尚待提交，C8 及后续子里程碑仍需逐项登记范围、测试和禁止扩展。

| 阶段 | 计划内容 | 完成判据摘要 |
| --- | --- | --- |
| M18-C6 | generic triode 主线化 | 已由 `667d1f2` 完成提交；保持商业管型及高阶物理边界。 |
| M18-C7a | generic pentode | 已由 `cd5f1bc` 完成提交；保持商业管型及高阶物理边界。 |
| M18-C7b | generic beam tetrode | 已由 `75f061d` 完成提交；保持真实 beam-forming、secondary emission、商业管型及高阶物理边界。 |
| M18-C7c | vari-mu | generic 三端 remote-cutoff 模型、五参数授权及静态/DC/动态/收敛/Reset/实时门禁已本地通过；待提交进入 Git 基线，商业管型与压缩器行为仍排除。 |
| M18-C8 | coupled inductor、transformer/core | 匝比、绕组损耗、励磁/漏感、饱和/磁滞的状态与能量/稳定性证据。 |
| M18-C9 | 半导体与运放高阶物理 | 补齐二极管动态、BJT/JFET/MOSFET 寄生与温度、真实运放电源/频率/瞬态行为；逐项与参考模型比较。 |
| M18-C10 | 电源与负载交互 | 整流、纹波、Sag、供电恢复、扬声器动态阻抗及功放反馈联调。 |
| M18-D1 | 实时控制和跨网络语义 | 电位器、开关、继电器、供电和子电路参数经有界实时邮箱更新，具备平滑、状态、自动化和 Reset 测试。 |
| M18-D2 | 行为模块 | BBD/delay、VCO/VCA、envelope/LFO、光耦/LDR/灯泡、spring 可运行绑定和跨网络测试。 |
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
