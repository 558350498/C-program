# 调度系统下一阶段计划

这份文件只记录当前进度和下一步。模块导航看 `index_.md`，稳定设计看 `docs/system_modeling.md`。

## 当前状态

已完成：

- `TaxiSystem` 业务编排和 request 生命周期回写。
- `RequestContext` 生命周期状态机。
- KD-Tree 空间索引。
- 单次最近车派单策略。
- 标准化 batch 输入类型。
- 候选边生成、统计和规范化。
- 批量贪心 baseline。
- MCMF 第一版：最大匹配数量 + 最小 pickup cost。
- 事件驱动 replay 第一版。
- replay 总指标和每轮 batch 日志。
- `TaxiSystem` 日志开关，replay 默认 silent。
- C++ 标准化 CSV loader。
- Go raw CSV 预处理工具。
- Go 合成 `drivers.csv`，默认每 2 个有效 request 生成 1 个司机。
- Go 连续时间窗口采样：`-window-seconds`，避免 raw CSV 前 N 行跨越过长时间。
- C++ replay CLI：`replay_csv_demo`，可读取 normalized CSV 并输出 report。
- replay 可选使用 KD-Tree indexed 候选边路径：`--indexed-candidates`。
- `k_sweep` / `go_experiments` 已支持透传 indexed 候选边模式。
- replay 已支持按 batch 输出 CSV：`--batch-log-csv`。
- 非机器学习算法与策略文档：`docs/algorithm_and_strategy.md`。

已验证：

- `ctest --test-dir build-mingw --output-on-failure`
- 当前 smoke 测试包含 C++ replay、CSV loader、MCMF、batch、TaxiSystem。
- Go 工具可从 `NYC.csv` 生成小样本 `requests.csv` / `drivers.csv`。
- `go test ./...`
- `replay_csv_demo` 已跑通 1000 单连续窗口样本。
- `k_sweep` 已输出 top-k / radius 参数扫描 CSV。
- Go 实验 runner 已补充供需比、订单里程收入、接驾成本和粗略净收入估算。
- replay 已补充唯一无候选 request 数、平均派单等待时间。
- 空间索引抽象第一阶段：KD-Tree 已支持侧表挂载友好的 `id + distance_sq` 查询结果。
- 候选边生成 indexed 对照路径：已用 KD-Tree `radius_query` + driver side table 实现。
- replay scan / indexed 两条路径已在测试和 1000 单样本上验证指标一致。
- batch CSV 已在 1000 单样本上输出，1 行 header + 240 轮 batch 记录。
- replay / k_sweep 已补充候选边生成耗时、匹配耗时和总 replay 耗时。
- 1000 单样本下已做 scan / indexed 初步性能对照：当前 indexed 因每轮重建 KD-Tree，在小样本下候选生成明显慢于全量扫描。
- 2026-05-07 本机大样本性能对照已跑通默认矩阵：`limits=1000,5000,20000`、`modes=scan,indexed`、`radii=0.01,0.03,0.05`、`k=1,2,5,unlimited`，结果位于 `build-local/perf-sweeps/summary.csv`。需要注明：在 `window-seconds=86400` 下，`sample_limit=20000` 实际只取得 7162 条有效请求，因此这一档应理解为“一天窗口内可用最大样本”，不是完整 20000 单。
- 本机性能对照结论：scan / indexed 指标一致，但 indexed 当前在所有样本档都慢于 scan。主要瓶颈不是 KD-Tree 查询本身，而是 indexed 候选生成路径每轮 batch 都重新构造整棵司机 KD-Tree；KD-Tree 内部已有替罪羊式局部重建机制，但没有跨 batch 复用索引。
- 2026-05-07 已完成 indexed 第一轮优化：replay 在 indexed 模式下维护跨 batch 的 free-driver KD-Tree，派单成功时 `erase(taxi_id)`，订单完成并更新到 dropoff 后 `upsert(...)`；候选生成新增复用外部 `ISpatialIndex` 的 overload。优化后 1000 单 / radius 0.03 / k unlimited 的 indexed 候选生成从约 372ms 降到约 89ms，5000 单 / radius 0.03 / k=1 从约 23.8s 降到约 2.95s；指标与 scan 保持一致。当前 indexed 仍慢于 scan，后续瓶颈转向 KD-Tree range query / 候选生成细节优化。
- 2026-05-07 已拆分 replay 耗时指标：保留 `matching_ms`，新增 `greedy_matching_ms`、`mcmf_matching_ms`、`assignment_application_ms`、`batch_accounting_ms`。定位样本显示，派单回写不是主要瓶颈：5000 单 / radius 0.03 / k=1 下 scan 的 MCMF 约 28ms、回写约 45ms、候选生成约 1024ms；同条件 indexed 的 MCMF 约 30ms、回写约 63ms、候选生成约 2720ms。`unlimited` 下候选边膨胀到约 378 万，scan 的 greedy 约 1079ms、MCMF 约 4841ms、batch accounting 约 300ms，说明瓶颈主要来自候选边膨胀后的排序/匹配，而不是状态回写逻辑复杂。
- 当前默认实验口径：使用 `scan + finite k`。`indexed` 作为空间索引正确性 / 性能对照路径保留，暂不替换默认候选生成；`unlimited` 只作为理论上界和压力测试，不作为常规策略。
- 已新增批量性能 runner：`tools/go_batch_experiments`，可按 limit 阶梯自动预处理并合并 scan / indexed 实验 CSV。
- Go 实验 runner 已补充轻量 tile 热区统计：pickup/dropoff 热度均值、冷区分数、热区/冷区请求数和比例。
- 已新增实验结果汇总工具：`tools/go_experiment_summary`，用于从总 CSV 中输出紧凑对照和推荐配置。
- replay 已补充 per-request outcome：候选边覆盖、派单、完成、等待时间和接驾成本。
- `replay_csv_demo` 已支持 `--request-outcome-csv` 输出 request 级明细。
- `k_sweep` 已基于真实 replay outcome 输出每个实验 row 的 hot/cold dropoff 分组报告。
- 第一版 opportunity adjustment 已进入实验报表，默认 `cold_dropoff_penalty = 1.0`、`hot_dropoff_discount = 1.0`，暂不影响 dispatch。
- `go_experiments`、`go_batch_experiments` 已透传 opportunity adjustment 参数。
- `go_experiment_summary` 已切换到展示 per-row hot/cold completion / coverage / opportunity 指标。
- `go_experiments` 已新增 `-zone-fixed-pricing` 极端验证模式：完全不看公里数，只按 hot->hot 加价、cold->cold 减价、其他组合不变来估算固定单价收入；该模式只进报表，不影响 dispatch。
- 轻量 tile/grid side table 第一版已落地到 C++：`TileGridStats` 统计 pickup/dropoff heat、初始 free driver count、hotspot/cold score，并接入 `k_sweep` hot/cold dropoff 分组；`k_sweep --tile-stats-csv` 可导出 tile 明细。
- 受约束离线 UF region map 第一版已落地到 C++：`tile_region_map` 使用 `TileGridStats` 构建 `tile_id -> region_id`，`k_sweep --region-map-csv` / `--region-stats-csv` 可导出审计明细；region stats 已包含 bbox km 粗估，不影响 dispatch、候选生成或 MCMF cost。
- 多分辨率 tile / region sweep 第一版已接入：Go preprocess 支持 `-tile-grid-cols`，`k_sweep` 支持 `--tile-grid-cols`，`go_batch_experiments` 支持 `-tile-grid-cols 100,200,400` 并按 `normalized/grid_<N>/limit_<M>` 保存 normalized CSV、tile stats、region map 和 region stats，同时输出 `grid_<N>/summary.csv`；总表新增 `tile_grid_cols`。
- 当前空间网格路线已经收口为：`simpleTile(grid_cols)` 继续作为 baseline；H3 作为后续候选升级方向，等讨论清楚是否需要真实地理层级网格后再接入；地图瓦片和真实路由中间件继续后置。

## 当前数据流

```text
data/datasets/nyc-taxi-trip-duration/raw/NYC.csv
  -> tools/go_csv_preprocess -window-seconds ...
  -> data/normalized/requests.csv
  -> data/normalized/drivers.csv
  -> C++ dispatch_replay_io
  -> replay_csv_demo
  -> DispatchReplaySimulator::run_report
  -> format_dispatch_replay_report
  -> request outcomes / k_sweep grouped metrics
```

## 近期目标

### 0. 当前推荐推进顺序

当前调度主链路已经够用，先不要继续深挖 indexed 细节，也不要直接上完整格点路径规划。

推荐下一步：

1. 保持默认实验为 `scan + finite k`，用 replay 继续产出稳定指标。
2. 做轻量 tile/grid side table：围绕 pickup/dropoff tile heat、冷区分数、机会成本和候选粗筛，不做真实道路路径。
3. 先按 `docs/region_design.md` 保持区域边界清晰：region map 慢变，heat/cold 快变，UF 第一版只做统计审计。
4. 先跑 `100 / 200 / 400` resolution sweep，看 `region_stats.csv` 的 region 数量、平均 tile 数、最大对角线和面积，再决定是否写清洗算法。
5. 先做 GeoJSON 导出和地图展示，把 `tile_stats.csv`、`region_stats.csv`、`region_map.csv` 画出来，形成可解释的项目展示闭环。
6. 地图完成后，再回到计价因子：先整理已有里程收入、接驾成本、hot/cold fixed pricing、opportunity adjustment，再决定是否增加更复杂的时间、拥堵、区域、供需因子。
7. 讨论是否抽象 `CellIndex`：先让 `simpleTile` 实现，如果确实需要更专业地理网格，再增加 H3 实现。
8. indexed 后续只在需要替换候选生成器、做 top-k 空间查询或接入统一 `ISpatialIndex` 时继续优化。

暂缓：

- 完整格点地图路径规划。
- 真实道路最短路。
- 把机会成本写进 MCMF cost。
- Tarjan / SCC 自适应区域缩点。
- 将 UF region map 接入派单硬边界或 MCMF cost。
- 每个 batch 动态重划 region map。
- 继续为 indexed 做底层性能微调，除非 scan + finite k 已经不能支撑目标样本。
- 在 100/200/400 多分辨率数据跑完前，先不写 region 清洗算法或自适应 tile。
- 在讨论清楚 simpleTile/H3 的边界前，先不接地图瓦片服务、OSM 路由或真实道路最短路。
- 在地图展示闭环完成前，先不继续叠复杂计价因子、Redis/RedisGeo、WebSocket 实时流或外部 GIS 中间件。

### 1. 空间索引抽象与 KD-Tree 侧表化

当前开始频繁触碰底层空间查询，先不要把 KD-Tree 写死到业务里。空间索引抽象第一阶段已完成，后续让候选边生成、热区统计、未来格点地图逐步依赖同一层接口。

目标：

- KD-Tree 只负责空间查询，不保存业务对象。
- 业务对象放在外部 side table。
- KD-Tree 查询返回轻量结果：`id + distance_sq`。
- 调用方用 `id` 去 side table 取 `DriverSnapshot`、`PassengerRequest` 或 heat metadata。

建议新增或扩展：

```text
SpatialQueryResult {
  int id;
  double distance_sq;
}
```

查询接口：

```text
radius_query(center, radius) -> vector<SpatialQueryResult>
nearest_k(center, k) -> vector<SpatialQueryResult>
```

约束：

- 保持旧接口可用，避免一次性打碎 `TaxiSystem` 和 dispatch strategy。
- 新接口先由测试覆盖，再接入候选边生成或 hotspot。
- 不在 KD-Tree node 中塞 driver/request/heat 业务字段。
- 后续如果换成 grid index、tile bucket 或其他空间索引，业务层不应该大改。

第一阶段状态：

1. 已增加查询结果结构。
2. 已增加 radius / top-k 查询。
3. 已保持旧 `radius_search` 行为兼容。
4. 已增加 KD-Tree 测试。
5. 候选边生成已新增 indexed 对照路径。
6. replay 已可选使用 indexed 路径。
7. 下一步让热度统计使用同一侧表化接口。

### 2. Replay 实验仪器化

已初步完成，后续作为持续实验工具维护。当前不急着上完整格点地图，先用 replay 作为稳定的实验仪器。

职责：

- 批量扫描候选集规模 `k = max_edges_per_request`。
- 批量扫描候选半径 `radius`。
- 输出结构化实验表，便于比较不同策略。
- 让调度策略、供需紧张度和计算成本可以被量化。
- 支持 scan / indexed 两种候选边生成路径对照。
- 输出 `candidate_generation`、`candidate_generation_ms`、`matching_ms`、`replay_ms`，用于衡量候选边生成和匹配阶段成本。
- `tools/go_batch_experiments` 可循环 `limit`、候选半径、k 值和 scan/indexed 模式，输出一张总表。
- `tools/go_experiment_summary` 可读取总表，按完成率阈值筛选并优先推荐候选边更少、耗时更低、接驾成本更低的配置。
- replay report 现在包含 request 级 outcome，可继续作为更细粒度实验分析的事实源。
- `k_sweep` 已输出 hot/cold dropoff 分组服务效果，用于比较不同 radius / k / scan-indexed 策略。

不负责：

- Kaggle raw CSV 解析。
- Go 预处理。
- 在线服务。
- 数据库。
- 真实道路最短路。

已新增工具：

```text
k_sweep
tools/go_experiments
```

输出字段：

```text
k,radius,driver_every,requests,drivers,assigned,completed,unserved,
assignment_rate,completion_rate,candidate_edges,avg_pickup_cost,mcmf_cost,
hot_dropoff_completion_rate,cold_dropoff_completion_rate,
hot_dropoff_candidate_coverage_rate,cold_dropoff_candidate_coverage_rate,
opportunity_adjustment_avg
```

第一组 k：

```text
1, 2, 3, 5, 8, 10, 20, 50, unlimited
```

### 3. 小样本闭环

已经跑通，后续作为固定 smoke / experiment 输入继续使用：

```text
NYC.csv -> Go preprocess -> normalized CSV -> replay_csv_demo -> report
```

建议先用：

- `limit = 1000`
- `window-seconds = 86400`
- `driver-every = 2`
- `driver-radius = 0.003`
- `batch_interval_seconds = 30`
- `trip_duration_seconds = 600`

候选边参数需要根据经纬度距离调：

- `radius` 先试 `0.01`、`0.03`、`0.05`
- `seconds_per_distance_unit` 先试 `100000`

### 4. Replay 指标增强与性能对照

观察：

- 总请求数
- 合成司机数
- 完成率
- 未服务请求数
- 候选边总数
- 无候选边 request 数
- greedy / MCMF 匹配数和 cost 对比
- scan / indexed 候选边生成耗时对比
- 匹配耗时和总 replay 耗时

下一步补充：

- 平均 pending request 数。
- 平均 available driver 数。
- 每轮平均 candidate edges。
- 运行大样本分级性能对照：1000 / 5000 / 20000 单，观察 scan 和 indexed 的分界点。
- 对照大样本下 hot/cold dropoff 的完成率、候选边覆盖率和接驾成本差异。
- indexed 候选生成继续作为后续优化方向：跨 batch 复用 free-driver spatial index 已完成，后面只有在需要替换默认候选生成器或支持更大样本时，再观察 KD-Tree range query、结果排序、side-table 过滤和每 request top-k 裁剪的耗时占比。树内部继续使用替罪羊式延迟重建，避免回到每轮全量 rebuild。

已补充：

- 唯一无候选边 request 数，而不是只统计每轮累计次数。
- 平均派单等待时间：`assignment_time - request_time`。
- 平均接驾时间：使用 `average_applied_pickup_cost` 表示。
- 按 batch 输出 CSV，便于画曲线。
- replay summary / k_sweep CSV 输出候选生成、匹配、总回放耗时。
- per-request outcome CSV，便于审计单个 request 的候选边覆盖、派单和完成状态。
- hot/cold dropoff 分组指标，便于观察不同策略对热区/冷区终点订单的服务效果。

如果大量 request 没候选边，优先调：

- `driver-radius`
- replay candidate `radius`
- `driver-every`
- tile 粗筛开关

### 5. 轻量 Tile/Grid 区域热度原型

暂时不直接做完整格点地图。先做轻量热度统计：

- 用 tile heat map 或 KD-Tree 统计 pickup 热度。
- 计算 `pickup_hotspot_score` 和 `dropoff_hotspot_score`。
- 判断订单终点是热区还是冷区。
- 估计司机完成本单后的后续接单机会。
- 建议新增明确的 tile/grid side table 口径：`tile_id -> pickup_count / dropoff_count / available_driver_count / hotspot_score / cold_score`。
- 第一版 tile/grid 只服务于统计、报告和粗筛，不承担路径规划。
- 已新增 `TileGridStats` 模块，并让 `k_sweep` 使用统一 side table 替代匿名 hotspot 统计。
- 第一版 tile heat map 已接入 `go_experiments` 输出，默认 `hotspot_score >= 0.7` 为热区，`<= 0.3` 为冷区。
- `tools/go_batch_experiments` 会自动继承这些热区统计列。
- 分组统计已从全局一组推进到每个实验 row：`k_sweep` 基于当前策略 replay outcome 输出 hot/cold dropoff 服务效果。
- 当前分组指标包括请求数、候选边覆盖率、派单率、完成率、平均订单距离、平均接驾成本和机会成本估算。
- region / zone 只作为 tile 之上的慢变解释层，当前 `tile_region_map` 已提供受约束离线 UF 原型，不进入 dispatch 权重或候选生成。
- `region_stats.csv` 的 `approx_width_km`、`approx_height_km`、`approx_diagonal_km`、`approx_area_km2` 用于观察 UF 合并后的区域几何尺度；它们是 bbox 粗估，不是道路里程。
- 多分辨率对照先覆盖 `100 / 200 / 400` 三档网格。不同 grid cols 的 normalized 数据必须分目录保存，避免同一个 `tile_id` 在不同分辨率下语义混用。
- 当前机会成本公式只输出估算，不进入 MCMF cost：

```text
opportunity_adjustment =
  cold_dropoff_penalty * cold_dropoff
  - hot_dropoff_discount * dropoff_hotspot
```

先验证机会成本逻辑：

```text
dropoff 越靠近热区 -> 后续接单机会越高 -> 本单机会成本越低
dropoff 越靠近冷区 -> 后续接单机会越低 -> 本单机会成本越高
```

拥堵成本后置：

```text
经过热区 / 高峰时段 -> 行驶时间可能增加 -> congestion_penalty 增加
```

这两个因子必须分开建模，不能简单把热区等同于加价或减价。

当前另有一个极端验证模式 `-zone-fixed-pricing`，故意不看公里数：

```text
hot->hot = base_fare * hot_hot_factor
cold->cold = base_fare * cold_cold_factor
other = base_fare
```

默认 `base_fare=1.0`、`hot_hot_factor=1.2`、`cold_cold_factor=0.8`。输出 `zone_fixed_completed_revenue`、`zone_fixed_net_delta` 和组合计数，用于观察纯热冷组合定价和距离收入基线的量级差异；不扣接驾成本，也不进入 dispatch。

## 中期可能做

- 增加 CSV 输入后的数据质量报告。
- 增加失败回写原因统计。
- 根据小样本结果决定是否拆 `CandidateEdgeGenerator`。
- 根据 cost 复杂度决定是否抽 `PickupCostModel`。
- 增加 `k_sweep` / `radius_sweep` 实验命令。
- 按时间窗口进一步细化热区 / 冷区统计报告。
- 基于 region 的 `region_stats.csv` 和 pickup/dropoff flow matrix。
- 评估是否需要固定 N x N tile 聚合作为 coarse zone 对照 baseline。
- 对 opportunity adjustment 做参数 sweep，观察 cold penalty / hot discount 对净收入估算的影响。
- 评估是否把机会成本接入 MCMF pickup cost 或价格因子。
- 增加基于机会成本的非机器学习调价规则，但先继续保持 dispatch 不受价格估算影响。
- 再评估是否上格点地图和拥堵成本。
- 根据新空间索引抽象，把 replay 默认候选边生成从全量扫描迁移到 KD-Tree / grid 查询。
- 评估是否抽象 `CandidateEdgeGenerator`。

## 可视化与后续计价顺序

当前建议顺序：

```text
CSV replay artifacts
  -> GeoJSON export
  -> static web map
  -> timeline playback
  -> pricing factor experiments
  -> optional CellIndex / H3
  -> optional Redis / routing / realtime middleware
```

第一阶段可视化只需要文件式产物，不需要 Redis：

- `tile_stats.csv -> tile_stats.geojson`
- `region_stats.csv -> region_stats.geojson`
- `region_map.csv` 用于把 tile 和 region 关系挂到 hover / tooltip。
- region 第一版先画 bbox polygon，不做复杂 polygon union。

复杂计价因子继续后置。当前已有计价/收入口径包括：

- 里程收入：`fare_per_km * total_trip_km * completion_rate`。
- 接驾成本：`applied_pickup_cost` 转换为估算公里成本。
- hot/cold fixed pricing：只按 hot->hot / cold->cold 固定因子验证。
- opportunity adjustment：只做报表估算，不写入 MCMF cost。

后续如果继续做计价，应先保证地图上能解释 hot/cold、region 和完成率，再考虑加入时间窗口、拥堵、供需、区域机会成本等复杂因子。底层空间索引和中间件优化只在展示和计价实验都不够用时再推进。

建议项目目录分层：

```text
C-program/
  src/                  C++ 调度核心
  include/              C++ 头文件
  tests/                C++ 测试
  tools/                离线实验和转换工具
    geojson_export/     后续新增：CSV -> GeoJSON
  web/
    map_viewer/         后续新增：MapLibre 静态地图展示
  docs/
  plan/
  build-local/          本地构建和实验产物，不进入 git
```

边界：

- `tools/geojson_export` 只负责把 `tile_stats.csv` / `region_stats.csv` / `region_map.csv` 转成 GeoJSON。
- `web/map_viewer` 只负责加载 GeoJSON 并用 MapLibre 展示，不承载 replay、计价或派单逻辑。
- 第一版前端保持静态文件工作流，不接 Redis、WebSocket、数据库或真实路由服务。

## 暂时不做

- 数据库。
- API / Socket / WebSocket。
- 多线程 CSV 读取。
- 多线程 replay。
- 完整真实道路最短路。
- 完整格点路径规划。
- Tarjan / SCC 区域缩点。
- 每个 batch 动态重划 region map。
- 将 UF region map 接入派单硬边界。
- 未服务惩罚。
- 复杂等待时间 cost 修正。
- 复杂收益和重定位收益。
- cgo / C++ dll。
- 机器学习价格预测。
- Redis / RedisGeo 在线位置服务。
- WebSocket 实时地图流。
- H3 直接替换当前 CSV 主链路。

## 当前原则

- 先离线文件闭环。
- 先连续时间窗口小样本稳定。
- 先 report 可解释。
- 先 replay 实验表，再写最终定价策略。
- 先稳定空间索引抽象，再把 KD-Tree 用到候选边和热度统计。
- 先区域热度 / 机会成本，再考虑完整格点地图。
- tile 是事实层，region 是慢变解释层，heat/cold 是快变状态。
- KD-Tree、grid index、tile bucket 都应该藏在空间索引接口后面，不写死进业务层。
- Go 负责 raw CSV 和合成输入。
- C++ 负责调度核心和 replay。
- 不为了未来服务化提前污染当前接口。
