# 白盒模型数据库审计模板

每个模型进入运行时数据库前，必须创建一份对应的 JSON 审计记录。模板位于 `fixtures/whitebox/model_audit.template.json`。

必填内容：

| 字段 | 要求 |
| --- | --- |
| `modelId` / `deviceFamily` / `partNumber` | 稳定标识、器件族和型号；未知型号不得用临时别名交付 |
| `modelKind` | SPICE、compact 或经批准的等价模型类型 |
| `parameters` / `defaults` | 参数名、单位、默认值、边界和数值缩放 |
| `source` | URI、来源版本、获取时间和完整性信息 |
| `license` | SPDX、许可文本/授权证据、审核状态 |
| `validity` | 温度、电压、电流、频率和收敛适用范围 |
| `evidenceRefs` | 网表、工作点、SPICE/golden 和测量证据引用 |

`license.reviewStatus` 不是 `approved` 时，加载器必须返回 `MissingModelAuthorization` 或等价结构化拒绝，不得把模型放进可运行数据库。AGPLv3/商业授权冲突的结论必须由项目管理和法务确认，不能由代码默认推断。
