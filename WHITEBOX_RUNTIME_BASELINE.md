# M13 白盒运行时基线

本文件记录 M13 在 M12 不可变 `WhiteboxCompiledGraph` 之上的 clean-room 实现边界。实现没有读取、复制或改写 PedalKernel 的源码、模型、数据或测试资产，也不构成 PedalKernel 等价验证。

## 已交付

- `PrepareWhiteboxRuntime` 将已校验的通用图编译为预分配运行时印章；准备阶段建立节点编号、支路编号、状态数组、稠密 MNA 矩阵和 Newton 工作区。
- MNA 路径支持任意二端通用图中的电阻、backward-Euler 电容、电感、独立电压源，以及带 `nonlinearGroup` 标识的受限三次非线性导纳。非线性器件在同一预分配矩阵中进行有界 Newton 联合迭代；迭代上限、绝对/相对容差、初态上限和过采样上限均在 Prepare 校验。
- `Process`、`SolveDc` 和 `Reset` 只使用准备阶段拥有的向量/矩阵，不解析 JSON、不访问文件、不加锁、不调用实时分配接口；输入、矩阵奇异、非有限值和不收敛均返回稳定错误枚举。
- M13 当时的 `BuildWhiteboxWdfPlan` 只生成 Series/Parallel + R-type adaptor 计划；其后的 M18-A 已将该占位边界替换为下面记录的真实 WDF scattering 运行时。

## M18-A / M18-B 后续增量

- M18-A 已实现 WDF one-port、Series/Parallel/R-type adaptor 与受限线性 Series/Parallel 运行时，覆盖电阻、backward-Euler 电容/电感和数学变压器，并提供 Reset、采样率更新和预分配 Process。
- M18-B 在 topology lowering 层加入确定性 Q/P/S 约简树和 residual R：完全串并可约简的输入到参考地图由嵌套 WDF tree 执行，内部输出节点按 stage 边界传播；不可约简的二端线性图作为单个 R stage 进入通用 MNA。
- `PrepareWhiteboxStageRuntime` 校验 signal order、assembly IDs、依赖和 graph 元件集合；输入顺序变化保持稳定 IR，篡改计划、未知 one-port、未绑定跨网络、多引脚器件和无效选项均拒绝。

## 明确拒绝/未实现

- M18-B 前端已保留 SPQR/rigid IR，但当前分解不是 canonical 三连通 SPQR；无法串并约简的剩余二端网络整体作为 residual R/MNA，不宣称多端口 R scattering 或 WDF/R 混合树已经实现。
- 未绑定 subcircuit、未知多引脚器件、非线性器件数据库和联合模型查找尚未交付；编译器保留 IR 不等于运行时支持。
- JFET、MOSFET、运放非理想模型、具体变压器/磁芯、BBD/光耦/弹簧等仍不属于本运行时的新覆盖；M10 受限 BJT/MNA 已独立实验归档，但不作为 M18-B 或完整器件覆盖证据。
- 没有 PedalKernel 源码/模型授权、SPICE/golden 数据或许可证审查结论，因此不能声明等价、1:1 或商业分发合规。

## 测试证据

`tests/whitebox_runtime_tests.cpp` 覆盖 MNA 通用分压、动态状态 Reset、过采样、非线性收敛/不收敛、奇异矩阵、非法初态、未知器件、非法后端、非有限输入和 `noexcept` 实时边界。`tests/wdf_runtime_tests.cpp` 覆盖 M18-A scattering；`tests/m15_planning_tests.cpp` 的 M18-B 增量覆盖重排稳定性、Q/P/S 嵌套 WDF↔MNA、动态状态、两种 residual R 图、Reset、采样率、非有限输入、篡改 plan 和跨网络/多引脚拒绝。完整门禁结果以项目管理文档为准。
