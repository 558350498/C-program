# PPT 生成 Prompt：出租车调度系统展示

这份文档给另一个 agent 直接生成 PPT 使用。请按中文技术展示风格生成，目标时长 8-12 分钟，建议 12-14 页。观众默认有一定编程基础，但不一定懂前端、GIS 或网约车调度。

## 总体要求

- 展示主线：离线数据进入系统，经过空间索引、状态机、候选边、MCMF、replay 仿真，再导出静态 artifact 进入 Map Viewer 解释。
- 重点放在系统架构与算法闭环，不把前端 UI 当主菜。
- 前端定位必须说清楚：Map Viewer 是 replay artifact viewer，不是生产后台、不调后端 API、不实时派单。
- 计价、hot/cold、真实道路 route 默认是解释层或实验层；只有显式 `--cell-stats-grid-cols`、`dispatch_cost` side table / cost-scale 选项会影响 matching，不反写 `pickup_cost`、完成率或 replay 时间事实。
- tile 矩形来自固定网格 `simpleTile(grid_cols)`，不是 UF region 产物；UF 只把相邻相似 tile 合并成离线审计 region。

## 建议 PPT 结构

### 1. 项目标题与一句话目标

- 标题：基于离线 Replay 的出租车批量调度与可解释展示系统。
- 核心内容：用真实订单 CSV 构建离线调度实验闭环，对比派单策略，并用地图解释订单、区域热度和计价估算。
- 建议图示：一条总流程箭头：CSV -> C++ Dispatch Replay -> Go Exporters -> Map Viewer。
- 讲解重点：这是一个算法和实验系统，不是完整网约车商业后台。
- 避免误讲：不要说已经做了在线生产派单服务。

### 2. 技术栈总览

- 核心内容：
  - C++17：TaxiSystem、RequestContext、KD-tree、CandidateEdge、MCMF、DispatchReplaySimulator。
  - Go：CSV 预处理、实验编排、GeoJSON/JSON artifact 导出、pricing 报表。
  - React + TypeScript + Vite：前端 UI 状态、构建和本地静态服务。
  - MapLibre GL JS：地图底图、tile polygon、路线、点和高亮图层。
  - CSV / JSON / GeoJSON：层间静态文件接口。
- 建议图示：三层架构图：算法层 C++、实验层 Go、展示层 Web。
- 讲解重点：C++ 是系统内核，Go 是实验胶水，前端是解释器。
- 避免误讲：不要把 Go replay 说成核心算法层；Go 主要负责编排和导出。

### 3. 数据流与静态文件边界

- 核心内容：
  - Kaggle `NYC.csv` 由 Go 预处理为标准化 `requests.csv` / `drivers.csv`。
  - C++ 只读取标准化 CSV，不依赖 Kaggle 原始 schema。
  - replay 输出 metrics、batch logs、request outcomes。
  - Go exporters 再转成 `tile_stats.geojson`、`replay_batches.json`、`sampled_order_explanations.json` 等前端 artifact。
- 建议图示：文件流水线。
- 讲解重点：通过文件边界降低耦合，保证实验可复现。
- 避免误讲：浏览器 `fetch("/data/...")` 只是读 Vite 静态文件，不是请求后端调度 API。

### 4. KD-tree：梦开始的地方

- 核心内容：
  - `ISpatialIndex` 提供 `upsert`、`erase`、`radius_query`、`nearest_k`。
  - KD-tree 只存 `Point{id, x, y}`，业务对象通过 side table 获取。
  - 当前用于 nearest taxi baseline 和 indexed candidate generation 对照。
- 建议图示：点集、半径查询圆、side table。
- 讲解重点：KD-tree 是点级空间查询 baseline，后续可被 grid / H3-like CellIndex 替换或并存。
- 避免误讲：不要说 KD-tree 保存了完整 taxi/request 业务对象。

### 5. TaxiSystem 与 RequestContext 状态机

- 核心内容：
  - Taxi 状态：`offline`、`free`、`occupy`。
  - Request 状态：`pending`、`dispatched`、`serving`、`completed`、`canceled`。
  - `TaxiSystem::apply_assignment` 统一回写 MCMF 结果，保证 taxi 和 request 状态一致。
- 建议图示：两个状态机并排，assignment 连接两边。
- 讲解重点：状态机保证 replay 不是“只算一个匹配结果”，而是可验证的系统推进。
- 避免误讲：不要绕过 `RequestContext` 直接把 taxi 改回 free。

### 6. CandidateEdge：把派单变成图问题

- 核心内容：
  - 候选边结构：`taxi_id -> request_id, pickup_cost`。
  - 只连接当前 batch 中可用司机和 pending request。
  - 支持 radius、per-request top-k、可选 same tile 粗筛。
- 默认边权是接驾成本，来自几何距离换算；显式实验可用 `dispatch_cost` 接入 route-cost CSV 或 hot/cold adjustment。
- 建议图示：司机点、订单点、候选边集合。
- 讲解重点：CandidateEdge 是真实业务和图算法之间的接口。
- 避免误讲：不要说默认边权已经用了热区、计价或实时真实道路 ETA。

### 7. MCMF 批量派单

- 核心内容：
  - 建图：`source -> taxi -> request -> sink`。
  - 每辆 taxi 容量 1，每个 request 容量 1。
  - `taxi -> request` 边费用是 `pickup_cost`。
  - 目标：先最大化匹配数量，再最小化总接驾成本。
  - greedy 只作为 baseline，正式 replay 回写使用 MCMF。
- 建议图示：四层流网络。
- 讲解重点：MCMF 比单纯最大流多了费用维度，可以在“尽量多派单”的同时优化总成本。
- 避免误讲：当前实现是朴素 successive shortest augmenting path / SPFA 风格，不是优化版 potentials + Dijkstra，也不是 Dinic 当前弧优化。

### 8. Replay：离线时间发动机

- 核心内容：
  - 事件类型：`request_arrival`、`batch_dispatch`、`pickup_arrival`、`trip_complete`。
  - priority queue 按时间推进。
  - 每隔 `batch_interval_seconds` 收集 pending requests 和 free drivers，生成候选边并跑 MCMF。
  - 派单成功后生成未来 pickup 和 complete 事件。
- 建议图示：时间轴和事件队列。
- 讲解重点：replay 模拟的是“订单到达、司机释放、批量派单、行程完成”的动态过程。
- 避免误讲：不要把 replay 说成静态地把一批订单一次性匹配完。

### 9. Tile 与 Region：空间解释层

- 核心内容：
  - Go preprocess 用固定 NYC bbox + `grid_cols` 把坐标映射到 `tile_id = row * grid_cols + col`。
  - `TileGridStats` 统计 pickup/dropoff/free driver、hotspot_score、cold_score。
  - 前端矩形 tile 是固定网格 bucket。
  - `TileRegionMap` 用受约束 UF 合并相邻相似 tile，输出离线 region 审计，不参与 dispatch。
- 建议图示：固定网格、热度颜色、UF region 轮廓示意。
- 讲解重点：tile 是最小空间事实单元，region 是慢变解释层。
- 避免误讲：不要说前端当前看到的矩形是 UF 合并后的 region。

### 10. Pricing v1：可解释计价估算

- 核心公式：

```text
fare = distance * rate * factor - pickup cost
factor = clamp(
  1 + pickup_hot + cold_dropoff - hot_dropoff,
  floor,
  cap
)
```

- 核心内容：
  - `base_revenue = trip_km * fare_per_km`。
  - factor 来自 pickup hotspot、cold dropoff penalty、hot dropoff discount。
  - `estimated_net = estimated_revenue - estimated_pickup_cost`。
  - 只进入 Go 报表和抽样订单解释。
- 建议图示：公式分解条。
- 讲解重点：这是解释和实验层，用来说明订单价值，不改变派单结果。
- 避免误讲：不要说乘客真实价格或平台真实收入已经建模完成。

### 11. Map Viewer：展示层怎么工作

- 核心内容：
  - React 管 UI 状态：live/batch 切换、layer 开关、Orders 抽屉、选中订单。
  - TypeScript 描述 artifact 数据结构，降低 JSON 字段错误风险。
  - Vite 提供本地开发服务和静态文件读取。
  - MapLibre 负责渲染底图、GeoJSON polygon、点、路线和高亮图层。
- 建议图示：React state -> MapLibre source/layer。
- 讲解重点：前端读取静态 artifact，不直接触发 replay 或派单。
- 避免误讲：不要把 Map Viewer 说成实时地图后台。

### 12. Demo 路线

- 推荐 3 分钟演示路径：
  1. 打开 Map Viewer，看 tile heat。
  2. 切换 Tiles / Points / Witness，解释矩形 bucket 和 pickup 证据。
  3. 切到 Live，看真实道路 route polyline 和 taxi 插值移动。
  4. 切到 Batch，看 batch tick 与 tile activity overlay。
  5. 打开 Orders，选一个样本订单，看 Lifecycle / Dispatch / Pricing。
- 建议图示：一页 demo checklist。
- 讲解重点：用前端收尾，让算法结果变成可检查证据。
- 避免误讲：不要展示成订单 CRUD 或运营后台。

### 13. 当前结论与边界

- 核心结论：
  - C++ replay 已形成可复现实验事实源。
  - MCMF 在候选边集合上完成全局最小接驾成本匹配。
  - tile/hot-cold/pricing/order explanation 构成解释闭环。
  - Map Viewer 已能展示 live、batch、tile、sample orders。
- 边界：
  - 不做在线派单服务。
  - 不把真实道路 ETA 写入 `pickup_cost`。
  - 不让 pricing v1 在没有显式 cost-scale 时改变 MCMF cost。
  - 不让 UF region 作为硬派单边界。
- 建议图示：已完成 / 不做 / 下一步三栏。

### 14. 后续深化方向

- 可选方向：
  - local multi-resolution hex grid / H3-like CellIndex：替代 naive rectangular tile，支持更自然的邻域扩散和多尺度供需统计。
  - region 审计图层：把 `region_stats.csv` / `region_map.csv` 作为只读图层接入 Map Viewer。
  - 真实 ETA 成本实验：当前已有候选对 route-cost CSV 入口，并支持去重/缓存；下一步是在 OSRM 可用时生成正式对比报告，不在 replay loop 内实时请求路由。
  - CandidateEdgeGenerator 抽象：为 KD-tree、tile、hex grid 候选生成器做可替换接口。
- 建议图示：路线图。
- 讲解重点：当前系统已经可以展示，后续是从课程项目走向毕设级系统的扩展。
- 避免误讲：不要承诺已经完成 H3 或全量候选对真实 ETA 派单。

## 生成风格要求

- 页面文字要少，优先用流程图、状态机图、网络流图、地图截图占主要空间。
- 每页标题直白，不要写营销式标题。
- 技术名词保留英文，如 `CandidateEdge`、`MCMF`、`DispatchReplaySimulator`、`MapLibre`。
- 不要把所有指标塞到一页；指标只用于支撑“可复现、可对比、可解释”。
- 重点页建议是第 6-10 页：CandidateEdge、MCMF、Replay、Tile/Region、Pricing。

## 可直接使用的一句话总结

本项目用 C++ 实现调度内核和离线 replay，用 Go 完成数据预处理、实验编排与 artifact 导出，用 React + MapLibre 构建静态解释型前端；系统通过 CandidateEdge 和 MCMF 完成批量派单，用 tile/hot-cold/pricing 和 sample orders 把 replay 结果解释给人看。
