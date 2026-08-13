# M11 通用白盒契约基线

本文件是 M11 的盘点和边界契约，不是通用白盒运行时的完成声明。当前工作树中的 M10 BJT、既有 RC/二极管 MNA、受控源和电位器路径都仍是局部实验或受限实现，不能替代本契约要求。

## 目标分层

### 1. 版本化结构输入

`circuit.json` v2 的通用前端必须在非实时阶段完成：

- 图身份、节点和参考地；元件引脚及端口方向；供电和初始条件。
- 子电路边界、端口映射、模型引用及资源安全路径。
- solver 后端选择、过采样、初态/DC 工作点和收敛限制。
- stage/blockwise/K-method 与行为模块 lowering 的声明，而不是把它们识别后硬编码成单个效果器。

未知字段可以按版本策略保留，但未知器件、未知模型、歧义引脚、悬空端口、危险路径和不完整子电路必须返回结构化错误，不产生半成品运行时。

### 2. 编译与拓扑分类

编译器应先生成不可变中间图，再进行拓扑分类：

- 串联、并联、SPQR 分解和刚性/反馈子图必须有明确分类结果。
- 不能用“识别到某个固定电路”替代通用图编译。
- MNA 与 WDF/adaptor 是可选的后端接口；后端选择发生在 Compile/Prepare，不能在音频回调中解析 JSON 或改变拓扑。

### 3. 求解运行时

统一运行时契约必须能表达：线性状态、非线性联合求解分组、DC 工作点、反馈/stage/blockwise/K-method 状态、过采样和确定性错误。至少定义 `SingularMatrix`、`NonFinite`、`NonConvergent`、`InvalidInitialState`、`UnsupportedBackend` 和 `MissingModelAuthorization` 等边界。

`Process` 的硬约束是零文件访问、零 JSON 解析、零动态分配、零阻塞锁。所有矩阵、WDF 状态、非线性工作区和错误传播路径都必须在 Compile/Prepare 阶段拥有并验证。

### 4. 模型与证据

模型只能通过审计记录进入数据库：型号、SPICE/紧凑模型类型、参数/默认值、来源版本、许可证证据、适用范围和证据引用必须完整。没有来源或授权的模型只能保持 `blocked`，不能进入可交付模块。

每个模块必须提供网表、DC 工作点、频响、静态曲线、动态响应、SPICE 对比、golden 对比、收敛记录和误差阈值。模板见 `fixtures/whitebox/module_evidence.template.json`。

## 当前状态与阻塞

当前仓库没有 PedalKernel 源码、授权模型库、SPICE/golden 数据或商业许可结论。仓库中 Eigen 的 SPQR 支持目录不等于已交付 SPQR 编译器。AGPLv3 与商业授权审查完成前，不复制、改写、链接未知来源代码或模型，不宣称源码级等价。

契约 fixtures：

- `fixtures/whitebox/whitebox_contract_baseline.json`：能力、错误和实时边界总表。
- `fixtures/whitebox/graph_valid.contract.json`：包含节点/引脚/端口/供电/子电路/模型引用的待编译图。
- `fixtures/whitebox/graph_invalid_unsafe_model.contract.json`：危险路径和无授权模型必须拒绝。
- `fixtures/whitebox/model_audit.template.json`：模型数据库审计字段。
- `fixtures/whitebox/module_evidence.template.json`：模块验证证据字段。

这些 fixtures 的测试只验证契约形状、拒绝语义和“待实现/受阻”标志；通过不代表编译器、WDF/MNA 运行时或模型数据库已经实现。

## M12 开工前置条件

1. 项目管理侧确认 PedalKernel 源码/版本/API 是否可用，并完成 AGPLv3、商业授权或 clean-room 边界结论。
2. 提供可合法使用的模型参数、SPICE 网表、golden 数据和误差阈值来源。
3. 评审并冻结通用图 IR、SPQR/刚性分类结果、MNA/WDF backend ABI、DC/非线性错误枚举和实时所有权。
4. 为每一项前置条件增加可读取的证据文件后，才开始 M12 通用编译器前端；不得由局部固定拓扑实现倒推通用能力。
