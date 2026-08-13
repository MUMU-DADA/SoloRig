# M10 受限 BJT/MNA 实验收口记录

## 状态与用途

本记录归档此前留在工作树中的 M10 BJT/MNA 实验。实验代码经过重新审计、测试先行修正和当前源码全量回归后进入代码树，目的是保存一条可复现、可继续评估的受限 Ebers-Moll 路径。

该提交不是 M10 原里程碑的恢复完成，也不是 M18-C、PedalKernel 等价、完整器件模型数据库或 `reference-1to1` 交付。后续主线若采用、重写或删除该路径，仍需按新的授权里程碑和独立模型证据执行。

## 文件范围

- `src/content/circuit_v2.hpp/.cpp`：增加 `bjt` 三端组件和版本化 `bjtModel` 解析、范围及字段互斥校验。
- `src/dsp/mna_solver.hpp/.cpp`：增加预分配 Ebers-Moll 端电流、显式 Jacobian、NPN/PNP 极性、指数/电流限制和结电压步长阻尼。
- `tests/circuit_v2_tests.cpp`：覆盖解析、非法范围、字段歧义、NPN 截止/有源/饱和、PNP 镜像、Reset/Prepare、非有限、假收敛、非收敛和奇异矩阵。
- `tests/module_runtime_tests.cpp`：证明外部 v2 模块实际通过统一预编译 MNA 运行时处理 BJT，而不是静默降级到数字算法。
- `CIRCUIT_JSON_V2.md`：记录实验契约、初始猜测语义、稳定拒绝和未交付范围。

## 本次审计发现与修正

1. 旧实验会接受 BJT 上不适用的 `value` 等字段并静默忽略。现在解析和直接 Compile 都拒绝 `value`、`tolerance`、旧 `model` 及其它器件专用模型字段，避免同一网表出现两套含义。
2. 旧 Newton 判断会把极小 `maxStep` 阻尼后的微小变化误当成收敛，即使未阻尼解仍远离当前猜测。现在发生 BJT 结步长阻尼的迭代只能算进展，必须在后续未受限迭代满足 tolerance 才能成功；耗尽预算稳定返回 `NonConvergent`。
3. 新增 PNP 对 NPN 共射极参考的符号镜像测试，验证端电流和 Jacobian 的极性一致性；清除了原测试中的无效自赋值/未使用参数占位。

## 数值与实时边界

- 三端顺序固定为 `[collector, base, emitter]`；`initial` 仅作为基极节点电压的 Newton 初始猜测。
- 指数参数限制在 `[-60, 60]`，正反结电流分别受 `maxCurrent` 限制；`maxStep` 约束 BE/BC 结电压变化。
- 网表解析、节点索引、矩阵、解向量和 Newton 工作区均在非实时 Compile/Prepare 阶段完成；Solve 复用既有存储，不解析 JSON、不访问文件、不分配容器。
- 受限模型未包含 Early 效应、结电容、寄生参数、温度/自热、噪声、击穿、具体型号参数或供电动态。

## 验证证据

- 测试先行红灯：BJT 歧义 `value` 被接受；极小 `maxStep` 出现假收敛。两项根因修复后定向核心测试通过。
- 当前源码默认 `build.ps1` Release 全量退出码为 `0`；manifest `buildRunId=7f89c048781945ec95c4a4f540fca7f1`、`sourceMode=current-source`、`testsExecuted=true`。
- 核心 `196/196`、C ABI `9/9`、VST3 专项 `7/7`、Windows UI `6/6`、CTest `5/5`、官方 VST3 Validator `47/47`、Avalonia/C# 契约测试全部通过。
- 构建只有第三方 NeuralAmpModelerCore 的 3 条既有 MSVC C4819 代码页警告；本次自有源码未新增编译警告。

## 明确未交付

- 不构成完整 Gummel-Poon、真实晶体管型号、JFET/MOSFET、真空管、变压器/磁芯或其它器件族实现。
- 不构成通用非线性分组、SPQR/rigid stage、K-method、模型数据库运行时绑定、SPICE/golden 或硬件测量验证。
- 不证明任何商业设备 1:1，也不改变 M18-A 与后续 M18-B/M18-C 的授权边界。
