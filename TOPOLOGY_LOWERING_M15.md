# M15 拓扑分解与行为 lowering 基线

本里程碑继续使用 SoloRig 自有 clean-room IR，不读取、复制或改写未知 PedalKernel compiler、模型或测试资产，也不宣称等价验证。

## 拓扑分析

`AnalyzeWhiteboxGraphV2Json` 在非实时编译边界调用现有 v2 图编译器：

- Series/Parallel 图生成确定性的 partition、stage、端口边界和信号顺序 IR。
- blockwise offset 表和 K-method 系数表生成 `planned` 记录，保留 stage ID 与排序；它们是可审计的表结构，不是完整数值分解实现。
- feedback 识别、完整 SPQR/rigid 分解和跨网络路由目前不会强行生成运行时，分别返回 `FeedbackLoop`、`NonDecomposable` 或 `CrossNetworkBoundary`。
- 刚性多引脚图仍保留 `Rigid` 分类；子电路跨边界图明确拒绝，避免把边界路由误当作已完成。

`PrepareWhiteboxStageRuntime` 是 stage 所有权边界。目前 assembly 状态为 `Unsupported`，因此 Prepare 返回拒绝；`WhiteboxStageRuntime::Process` 是 `noexcept` 且未准备实例返回 `NotPrepared`。没有 JSON、文件、锁或动态分配进入该 Process。

## 行为模块 lowering

`LowerBehaviorModule` 只接受 `bbd`、`vco`、`vca`、`spring` 四种通用绑定种类。当前所有种类的契约状态为 `blocked`：没有授权模型或数据时返回 `BlockedModel`，未知种类返回 `UnknownBehavior`。不会识别固定单块、内置具体波形或静默退化成数字 DSP。

## 后续边界

完整 SPQR/WDF scattering、运放反馈求解、stage/blockwise/K-method 数值装配、跨网络路由和 BBD/VCO/VCA/spring 实际运行时 lowering 仍需后续授权与验证数据；M10 BJT/MNA 工作树实验改动不属于本提交。
