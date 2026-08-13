# M13 白盒运行时基线

本文件记录 M13 在 M12 不可变 `WhiteboxCompiledGraph` 之上的 clean-room 实现边界。实现没有读取、复制或改写 PedalKernel 的源码、模型、数据或测试资产，也不构成 PedalKernel 等价验证。

## 已交付

- `PrepareWhiteboxRuntime` 将已校验的通用图编译为预分配运行时印章；准备阶段建立节点编号、支路编号、状态数组、稠密 MNA 矩阵和 Newton 工作区。
- MNA 路径支持任意二端通用图中的电阻、backward-Euler 电容、电感、独立电压源，以及带 `nonlinearGroup` 标识的受限三次非线性导纳。非线性器件在同一预分配矩阵中进行有界 Newton 联合迭代；迭代上限、绝对/相对容差、初态上限和过采样上限均在 Prepare 校验。
- `Process`、`SolveDc` 和 `Reset` 只使用准备阶段拥有的向量/矩阵，不解析 JSON、不访问文件、不加锁、不调用实时分配接口；输入、矩阵奇异、非有限值和不收敛均返回稳定错误枚举。
- `BuildWhiteboxWdfPlan` 为 M12 已分类的 Series/Parallel 图生成可审计的 Series/Parallel + R-type adaptor 计划。WDF scattering 运行时尚未实现，选择 WDF 后明确返回 `UnsupportedBackend`。

## 明确拒绝/未实现

- SPQR/刚性拓扑在 M12 编译阶段仍返回 `UnsupportedTopology`；M13 没有用固定单块识别替代通用图编译。
- WDF tree、完整 Series/Parallel/R-type scattering、WDF 状态更新、复杂网络分解、非线性器件数据库和联合模型查找尚未交付。
- 二极管、BJT、JFET、MOSFET、运放非理想模型、变压器/磁芯、BBD/光耦/弹簧等行为模块仍不属于本运行时的新覆盖；工作树中的 M10 BJT/MNA 改动保持未提交实验状态。
- 没有 PedalKernel 源码/模型授权、SPICE/golden 数据或许可证审查结论，因此不能声明等价、1:1 或商业分发合规。

## 测试证据

`tests/whitebox_runtime_tests.cpp` 覆盖通用分压、动态状态 Reset、过采样、WDF 计划拒绝、非线性收敛/不收敛、奇异矩阵、非法初态、未知器件、非法后端、非有限输入和 `noexcept` 实时边界。M13 只在本地增量目标和 CTest 上验收；完整全量回归应在后续里程碑按管理文档执行。
