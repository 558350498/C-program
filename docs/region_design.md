# 区域设计笔记

这份文档记录 tile / region / heat 的边界。当前阶段已经实现受约束离线 UF region map 原型，并新增 `100 / 200 / 400` 多分辨率 tile sweep；它们都只服务统计和审计，不接入 dispatch、MCMF cost、动态区域重划或候选粗筛。

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

## 2. 空间网格路线选择

当前已落地的是 Go preprocess 里的 `simpleTile(lon, lat, grid_cols)`：

- 默认 100x100，兼容早期实验。
- 已支持 100/200/400 多分辨率对照。
- 编码简单：`tile_id = row * grid_cols + col`。
- 优点是没有外部依赖、可解释、调试成本低。
- 缺点是经纬度矩形网格存在面积变形，邻居和距离语义比专业地理网格弱。

H3 是后续最值得讨论的升级路线，但当前还没有接入：

- H3 更适合真实地理分析、层级 cell、邻居查询、热区聚合和供需流向。
- H3 的 hex cell 比方格更适合做半径近似和空间平滑。
- 引入 H3 会带来外部依赖、跨语言绑定和历史 CSV 兼容问题。
- 如果接入，应先通过抽象层隔离，而不是把 H3 API 直接写进 replay 或 dispatch。

建议的抽象边界：

```text
CellIndex
  encode(lon, lat) -> cell_id
  neighbors(cell_id) -> cell_id list
  boundary(cell_id) -> polygon / bbox
  parent(cell_id, resolution) -> cell_id
```

第一步可以让 `simpleTile` 实现这个接口；如果后续确认需要更真实的地理网格，再增加 H3 实现。地图瓦片、真实道路路由、OSM 中间件继续后置，不作为当前 region / heat 统计的前置条件；前端在线 raster 底图只用于视觉对齐参考。

## 3. 当前结论

第一阶段已经实现受约束 UF region map，但它不是完整自适应区域系统。仍然避免复杂动态区域，原因：

- 裸 Union-Find 只按“相邻 + 相似”合并，会有链式合并风险。类似中央大道这种高连通走廊可能把大量 tile 串成一个巨型 region。
- Tarjan / SCC 更适合研究有向 OD 图中的闭环流动社区，不是当前派单主线瓶颈。
- 每个 batch 动态重划区域会让 replay 实验不可比，也会把区域边界和实时热度混在一起。
- 当前真正需要的是稳定的 tile 统计、hot/cold 分组指标、region 审计输出和未来候选粗筛挂点，而不是完整城市分区算法。

因此当前默认路线是：

```text
fixed tile stats -> constrained UF region map -> region stats / flow matrix -> strategy research
```

## 4. Region Map 原则

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

这些字段按 Go `simpleTile()` 的当前 grid cols 反推，不是道路距离，也不是司机真实行驶里程。默认兼容 100x100 NYC 粗网格；多分辨率实验会显式使用 `--tile-grid-cols 100/200/400` 对照同一批请求在不同 tile 粒度下的 region 尺度。它们用于审计 UF 合并结果的几何尺度，例如观察区域是否被拉成长条，或者是否出现过大的连通块。

## 5. 后续路线

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

Phase 3: multi-resolution tile sweep

- 当前实现路线：保留 100x100 作为 baseline，同时支持 `-tile-grid-cols 100,200,400`。
- `tools/go_csv_preprocess -tile-grid-cols N` 只改变 tile id 编码，不改变 `requests.csv` / `drivers.csv` schema。
- `k_sweep --tile-grid-cols N` 只影响 region map 的 row/col 反解和 bbox km 粗估，不改变 replay、candidate generation 或 MCMF 行为。
- `tools/go_batch_experiments -tile-grid-cols 100,200,400` 会按 `normalized/grid_<N>/limit_<M>` 分目录保存 normalized CSV、`tile_stats.csv`、`region_map.csv` 和 `region_stats.csv`，并额外输出 `grid_<N>/summary.csv`；总表增加 `tile_grid_cols`。
- `tools/go_experiment_summary` 会按 `tile_grid_cols` 分组，并在存在 `region_stats.csv` 时输出 `region_count`、`avg_region_tile_count`、`max_region_diag_km`、`avg_region_diag_km`、`max_region_area_km2`。
- 这一阶段的判断目标是确认 region 过大来自 tile 太粗、UF 约束太松，还是高分辨率后热区过碎。

Phase 3.5: tile GeoJSON visualization

- 当前第一步只导出 `tile_stats.csv -> tile_stats.geojson`。
- `web/map_viewer` 先画真实 tile 方格和 hotspot 颜色，用于确认 simpleTile bbox 反解、热区分布和前端加载链路。
- `requests.csv -> tile_corner_witnesses.geojson` 用于 hover 某个 tile 时显示四角最近 pickup witness，解释为什么粗矩形可能只覆盖少量街道却仍有订单证据。
- witness 点不是道路裁剪，也不是水域过滤；它只帮助审计 fixed tile 与真实订单点之间的关系。
- region 可视化继续后置；等 tile 图层稳定后，再接 `region_stats.csv` 和 `region_map.csv`。
- 这一阶段仍然不引入后端 API、WebSocket 或真实道路服务；在线 OSM raster 底图只作为前端可关闭的开发展示层。

Phase 4: region stats / flow matrix

- 聚合 pickup/dropoff/free driver/completion/candidate coverage。
- 输出 `region_stats.csv`。
- 输出 `region_pickup -> region_dropoff` 的 flow matrix，用于观察潮汐和流向。

Phase 5: constrained UF evolution

- 后续再评估是否加入时间窗口、最小样本量、flow matrix 或 request outcome 指标。
- 仍然不进入当前 dispatch 主干。

Phase 6: strategy integration

- 先用于报告和候选覆盖审计。
- 再考虑候选粗筛。
- 最后才评估是否把 opportunity adjustment 接入 MCMF cost 或动态定价估算。

## 6. 当前不做

- 不把 UF adaptive region 接入派单主线。
- 不实现 Tarjan / SCC 区域缩点。
- 不每个 batch 动态重划 region。
- 不把 region 作为硬派单边界。
- 不把区域机会成本写进 MCMF cost。
- 不做真实道路、拥堵传播或完整格点路径规划。
- 不在当前阶段引入地图瓦片服务器或真实路由中间件。
