# M15 拓扑分解基线与 M18-B 可执行收口

本里程碑继续使用 SoloRig 自有 clean-room IR，不读取、复制或改写未知 PedalKernel compiler、模型或测试资产，也不宣称等价验证。

## 拓扑分析

`AnalyzeWhiteboxGraphV2Json` 在非实时编译边界调用 v2 图编译器。M18-B 在 M15 计划 IR 上补齐了第一条可执行闭环：

- 每个二端元件先成为 Q 叶；算法反复执行确定性的平行边约简和非边界二度节点串联约简，生成后序 Q/P/S stage、依赖 stage ID、边界方向和信号顺序。组件及 pin 输入顺序变化不改变执行 IR。
- 完全可串并约简且以输入/参考地为根的图由预分配 WDF tree 执行；Series/Parallel 波散射、Q one-port 状态、内部观察节点电压、Reset 和采样率更新均在实际运行时路径中。
- 无法继续串并约简的二端线性图形成单个 residual R stage，由通用 MNA 运行时执行；R 内部元件不会被虚假列为独立 WDF 执行 stage。
- 无向被动电路的环不再被误报为有向 `FeedbackLoop`。未知多引脚元件返回 `NonDecomposable`；未绑定 subcircuit 返回 `CrossNetworkBoundary`。
- blockwise offset 和 assembly 顺序现在与可执行 stage 同步为 `Ready`；K-method 系数仍保持 `Planned`，不是增量数值优化实现。
- M18-B stage 装配最多接受 256 个元件，防止串并树递归深度和稠密 MNA 工作区无界增长；超限图在非实时分析边界拒绝。

`PrepareWhiteboxStageRuntime` 是控制线程所有权边界：它校验 plan/graph 一致性，准备全部 one-port、树阻抗或 MNA 工作区。`WhiteboxStageRuntime::Process` 是 `noexcept`，只遍历预分配 stage 和缓冲；未准备实例返回 `NotPrepared`，非有限输入稳定拒绝。没有 JSON、文件、锁或容器扩容进入 Process。

## 行为模块 lowering

`LowerBehaviorModule` 只接受 `bbd`、`vco`、`vca`、`spring` 四种通用绑定种类。当前所有种类的契约状态为 `blocked`：没有授权模型或数据时返回 `BlockedModel`，未知种类返回 `UnknownBehavior`。不会识别固定单块、内置具体波形或静默退化成数字 DSP。

## 后续边界

本次实现是“串并约简 + residual R”的可执行 S/P/Q/R 子集，不宣称 canonical 三连通 SPQR tree，也没有多端口 R scattering 或 WDF/R 混合树组合；这类图目前会整体进入 residual R/MNA。K-method 增量优化、subcircuit/行为岛跨网络路由、BBD/VCO/VCA/spring 运行时和非线性器件族仍属于后续范围。
