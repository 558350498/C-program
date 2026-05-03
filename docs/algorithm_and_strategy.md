# Algorithm and Strategy

这份文档记录当前阶段的非机器学习调度与调价思路。目标不是训练一个黑盒模型，而是用离线回放、候选边、供需张力、区域热度和机会成本构造一套可解释的规则算法。

## 1. 基本定位

当前项目的核心原则：

- 不使用机器学习预测价格。
- 不训练模型，不引入神经网络、回归器或黑盒参数。
- 使用历史订单做离线 replay，观察不同规则参数下的调度效果。
- 用规则、约束和可解释指标调整候选集规模、接单成本和价格因子。

可以称为：

```text
基于离线回放的非机器学习调度与调价策略
```

或：

```text
offline replay-based rule tuning
```

## 2. Top-k 候选集策略

在 batch dispatch 中，每个 request 可以保留最多 k 个候选司机：

```text
CandidateEdgeOptions.max_edges_per_request = k
```

k 的作用不是直接影响价格，而是影响匹配空间：

- k 小：候选边少，MCMF 计算快，但可能错过更优匹配。
- k 大：候选边多，匹配更充分，但计算成本上升，边可能冗余。
- k 无限：理论空间最大，但不一定带来更高完成率。

建议用离线 replay 扫描：

```text
k = 1, 2, 3, 5, 8, 10, 20, 50, unlimited
```

观察指标：

- completion_rate
- assignment_rate
- unserved_requests
- candidate_edges_total
- requests_without_edges_total
- mcmf_cost_total
- average_pickup_cost

选择规则：

```text
在完成率达到目标阈值的前提下，选择候选边数量较少、平均接驾成本较低的 k。
```

例如：

```text
completion_rate >= 98%
candidate_edges 尽量少
average_pickup_cost 尽量低
```

这属于参数搜索和规则校准，不属于机器学习。

## 3. 供需紧张程度

纯供需比可以作为第一层信号：

```text
supply_demand_ratio = available_drivers / pending_requests
```

解释：

- ratio 高：司机相对充足。
- ratio 低：订单相对过多，供给紧张。
- ratio 接近 1：系统处于临界状态。

但纯供需比只在虚空行走模型下比较干净。一旦引入格点地图、道路代价或拥堵，单纯的司机数和订单数不够，因为司机和订单的空间结构会显著影响可服务性。

更稳妥的张力指标应该结合候选边：

```text
tension = requests_without_edges / pending_requests
```

以及：

```text
avg_candidates_per_request = candidate_edges / pending_requests
```

含义：

- 无候选边 request 多：不是 k 的问题，优先扩大 radius 或增加供给。
- 平均候选数低：局部供给紧张，k 或 radius 可能不足。
- 候选边很多但完成率不涨：候选边冗余，k 可以降低。

## 4. 区域热度与机会成本

价格不应该只看本单距离，还要看司机完成本单后所在区域是否容易接到下一单。

核心想法：

```text
去热门区域，司机后续更容易接下一单，本单机会成本较低。
去冷门区域，司机后续可能空驶返回，本单机会成本较高。
```

可以定义区域热度：

```text
zone_heat(tile, time_window) = recent_pickups_near_tile
```

或者用 KD-Tree 查询某个点附近的历史 pickup 数：

```text
hotspot_score(point, radius, time_window)
```

KD-Tree 的用途：

- 快速查询某个 pickup/dropoff 附近的历史订单密度。
- 找出峰值区域，也就是高频 pickup 区。
- 估计司机完成订单后附近再次接单的概率。

初版不需要预测，只统计历史窗口：

```text
过去 N 分钟 / 同时段历史数据中，dropoff 附近 radius 范围内的 pickup 数。
```

## 5. 热区的双重影响

热区不是单向利好，它有两个相反作用。

第一，机会收益：

```text
订单终点靠近热区 -> 更容易接下一单 -> 本单机会成本下降
```

第二，拥堵成本：

```text
路线经过热区或高峰时段 -> 更可能慢 -> 本单服务时间上升
```

所以不能简单地说“经过热门区域就减价”或“热门区域就加价”。应该拆成两个因子：

```text
price = base_price
      + trip_cost
      + pickup_cost
      + low_opportunity_penalty
      + congestion_penalty
      - hotspot_opportunity_discount
```

其中：

- `hotspot_opportunity_discount`：终点或路线靠近高订单区时降低机会成本。
- `low_opportunity_penalty`：终点进入冷区时增加机会成本。
- `congestion_penalty`：高峰时段、长时间、慢速区域带来的服务成本。

这样可以同时表达：

```text
热区容易接下一单，所以机会成本低。
热区可能拥堵，所以行驶成本高。
```

最终价格取决于两个因子的净效应。

## 6. 基于时间和距离的调价

在虚空行走模型中，pickup cost 目前来自几何距离：

```text
pickup_cost = distance * seconds_per_distance_unit
```

后续进入格点地图或道路代价后，应逐步替换为：

```text
effective_time_cost = distance_cost + time_of_day_penalty + congestion_penalty
```

可以先用简单规则：

```text
if peak_hour:
    congestion_penalty += route_distance * peak_factor

if route_crosses_hot_tiles:
    congestion_penalty += hot_tile_cross_count * hot_tile_penalty

if dropoff_near_hotspot:
    hotspot_opportunity_discount += hotspot_score * discount_factor

if dropoff_near_cold_area:
    low_opportunity_penalty += cold_area_penalty
```

这里仍然不是机器学习，因为所有因子都来自明确统计和手写规则。

## 7. 多因子和递减收益

当前 Go 实验 runner 已支持热点调价试验：

- `linear`：直接使用热点因子。
- `diminishing`：对热点因子应用分段递减收益。
- `price_floor` / `price_cap`：限制价格因子的下限和上限。

分段递减收益类似游戏数值里的护甲收益递减：

```text
0.0 - 0.3: 100% 生效
0.3 - 0.6: 80% 生效
0.6 - 0.9: 50% 生效
0.9 - 1.0: 20% 生效
```

目的：

- 保留热点加价趋势。
- 避免极端热区价格因子过度放大。
- 让策略仍然保持非机器学习、可解释、可调参。

当前实验公式：

```text
price_factor = clamp(
  1
  + pickup_hot_weight * pickup_hotspot
  - dropoff_hot_discount * dropoff_hotspot
  + cold_dropoff_penalty * cold_dropoff,
  price_floor,
  price_cap
)
```

实验输出关注：

- `avg_price_factor`
- `max_price_factor`
- `hotspot_completed_revenue`
- `hotspot_net_revenue`
- `hotspot_net_delta`

这套实验只估算价格变化对收入的影响，暂未建模价格上涨导致的需求流失。

## 8. 当前工程状态

当前已落地：

- `k_sweep`：扫描候选集规模和半径。
- `go_experiments`：补充供需比、订单里程收入、接驾成本、热点调价和粗略净收入。
- KD-Tree 侧表化查询：`radius_query / nearest_k -> id + distance_sq`。
- indexed 候选边对照路径：通过 KD-Tree 查询司机 id，再从 side table 取 `DriverSnapshot`。

indexed 候选边路径暂不替换 replay 默认路径，先作为性能和正确性对照。

## 9. 初版可实现策略

第一阶段不直接做复杂价格，只做 replay 指标和参数扫描：

1. 用 `-window-seconds` 生成连续时间窗口订单。
2. 对多个 k 跑 replay。
3. 输出 k、完成率、候选边数量、平均接驾成本。
4. 选出满足完成率约束下的最小 k。

第二阶段加入区域热度：

1. 用历史 pickup 构建 KD-Tree 或 tile heat map。
2. 对每个 request 计算 pickup 热度、dropoff 热度。
3. 给 report 增加热区 / 冷区统计。
4. 比较去热区订单和去冷区订单的完成率与后续接单机会。

第三阶段加入调价因子：

1. 基础价格来自距离和时间。
2. 冷区终点增加机会成本。
3. 热区终点降低机会成本。
4. 热区路径和高峰时段增加拥堵成本。
5. replay 对比调价前后的服务率、平均成本和未服务订单。

## 10. 当前不做

暂时不做：

- 机器学习价格预测。
- 真实道路最短路。
- 实时交通 API。
- 神经网络热度预测。
- 多目标复杂优化器。

当前先坚持：

```text
历史订单 replay -> 规则指标 -> 参数扫描 -> 可解释调价
```

这条路线工程上更稳，也更符合非机器学习选题。
