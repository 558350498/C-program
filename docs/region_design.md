# 区域设计笔记

这份文档记录 tile / region / heat 的边界。当前阶段已经实现受约束离线 UF region map 原型，但它只服务统计和审计，不接入 dispatch、MCMF cost、动态区域重划或候选粗筛。

## 1. 核心分层

当前空间建模分成三层：

```text
raw lat/lon -> tile_id -> region_id / zone_id
```

- `tile_id` 是最小空间事实单元，由 Go preprocess 从标准化输入中生成。
- `region_id` / `zone_id` 是多个 tile 聚合出来的解释层，不替代 tile。
- `TileGridStats` 是当前唯一落地的 tile/grid 统计模块，负责 pickup/dropoff count、初始 free driver count、hotspot/cold score。
- heat/cold 是快变状态，可以按实验、批次或时间窗口重算；region map 是慢变结构，不应每个 batch 动态重划。

这个边界的目标是让 replay outcome、热区报告、未来候选粗筛和机会成本估算可以共享同一套空间口径，同时不把项目提前拖进完整 GIS 或道路系统。

## 2. 当前结论

第一阶段已经实现受约束 UF region map，但它不是完整自适应区域系统。仍然避免复杂动态区域，原因：

- 裸 Union-Find 只按“相邻 + 相似”合并，会有链式合并风险。类似中央大道这种高连通走廊可能把大量 tile 串成一个巨型 region。
- Tarjan / SCC 更适合研究有向 OD 图中的闭环流动社区，不是当前派单主线瓶颈。
- 每个 batch 动态重划区域会让 replay 实验不可比，也会把区域边界和实时热度混在一起。
- 当前真正需要的是稳定的 tile 统计、hot/cold 分组指标、region 审计输出和未来候选粗筛挂点，而不是完整城市分区算法。

因此当前默认路线是：

```text
fixed tile stats -> constrained UF region map -> region stats / flow matrix -> strategy research
```

## 3. Region Map 原则

后续如果引入 region map，应满足：

- region map 慢变，按离线窗口、小时级或天级预处理生成。
- region 内部 tile 尽量地理紧凑，但区域划分不只依赖地理相邻，也可以参考供需行为相似。
- region 不直接决定派单正确性。第一阶段只用于统计、审计、报告和未来粗筛参考。
- region map 应以 `tile_id -> region_id` 形式输出，避免改动原始 request / driver CSV schema。
- heat/cold、supply/demand、completion rate 等指标挂在 tile 或 region side table 上，不写死进核心调度对象。

当前 C++ 受约束 UF 原型由 `tile_region_map` 实现，输入 `TileGridStats`，输出 `tile_id -> region_id` 和 region 聚合明细。UF 只负责执行合并，不负责判断区域好坏。合并前必须由约束函数判断：

```text
can_merge = local_similarity
            + merged_region_size_limit
            + merged_region_bbox_limit
            + merged_region_shape_limit
            + behavior_consistency_limit
```

中心点不应使用 UF parent。parent 只是数据结构代表，没有业务含义。区域中心应来自聚合属性，例如按 pickup/dropoff count 加权的 centroid，或维护 bbox / tile count 等轻量形状指标。

`region_stats.csv` 中的 km 字段来自 bbox 粗估：

```text
approx_width_km
approx_height_km
approx_diagonal_km
approx_area_km2
```

这些字段按 Go `simpleTile()` 的 100x100 NYC 粗网格反推，不是道路距离，也不是司机真实行驶里程。它们用于审计 UF 合并结果的几何尺度，例如观察区域是否被拉成长条，或者是否出现过大的连通块。

## 4. 后续路线

Phase 1: fixed tile stats

- 已完成。
- `TileGridStats` 输出 `tile_id,pickup_count,dropoff_count,available_driver_count,hotspot_score,cold_score`。
- `k_sweep --tile-stats-csv` 可导出 tile 明细。

Phase 2: constrained UF region map

- 已完成第一版统计/审计原型。
- UF 只连接 4-neighbor tile，不使用 8-neighbor。
- 默认相似度阈值为 0.75，合并约束为 `max_tiles=25`、`max_bbox_width=5`、`max_bbox_height=5`、`max_aspect_ratio=3.0`。
- `k_sweep --region-map-csv` 输出 `tile_id,region_id`。
- `k_sweep --region-stats-csv` 输出 region 聚合明细和 bbox km 粗估。

Phase 3: region stats / flow matrix

- 聚合 pickup/dropoff/free driver/completion/candidate coverage。
- 输出 `region_stats.csv`。
- 输出 `region_pickup -> region_dropoff` 的 flow matrix，用于观察潮汐和流向。

Phase 4: constrained UF evolution

- 后续再评估是否加入时间窗口、最小样本量、flow matrix 或 request outcome 指标。
- 仍然不进入当前 dispatch 主干。

Phase 5: strategy integration

- 先用于报告和候选覆盖审计。
- 再考虑候选粗筛。
- 最后才评估是否把 opportunity adjustment 接入 MCMF cost 或动态定价估算。

## 5. 当前不做

- 不把 UF adaptive region 接入派单主线。
- 不实现 Tarjan / SCC 区域缩点。
- 不每个 batch 动态重划 region。
- 不把 region 作为硬派单边界。
- 不把区域机会成本写进 MCMF cost。
- 不做真实道路、拥堵传播或完整格点路径规划。
