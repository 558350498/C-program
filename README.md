# Taxi Dispatch Replay Lab

这是一个离线出租车调度 replay 与空间统计实验项目。

项目目标是把 NYC taxi 原始数据转成可复盘的离线事实流：预处理订单和司机快照，运行 C++ replay，比较候选边和匹配策略，导出静态可视化 artifact，再用 Map Viewer 解释调度结果、热区/冷区、计价估算和路线证据。

它不是在线打车平台、不是订单后台，也不是机器学习定价系统。展示层可以解释 replay 事实，但不能反向定义 replay、dispatch、MCMF cost 或订单生命周期。

## Fixed Kernel

```text
Kaggle CSV
-> normalized requests/drivers CSV
-> C++ replay / candidate edges / MCMF
-> grouped metrics and per-request outcomes
-> static JSON/GeoJSON artifacts
-> MapLibre viewer explanation
```

## First-Pass Docs

| Need | Entry |
|---|---|
| Agent/task map | `AGENTS.md` |
| Architecture overview | `ARCHITECTURE.md` |
| Full docs map | `docs/index.md` |
| Current status | `docs/exec-plans/active/project-status.md` |
| Terminology | `docs/design-docs/glossary.md` |

## Windows Run Note

建议把 GitHub zip 解压到纯英文路径后再构建运行

## Verification

Run the normal pre-submit gate before packaging or handoff:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\pre_submit_check.ps1
```

For a lighter documentation-only check:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\project_doctor.ps1
```

Generated reports, CSV evidence packets, viewer data, and local builds belong
under ignored output directories such as `build-local/` and `build-*`.
