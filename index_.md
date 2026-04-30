# 工程导航索引

这份索引的目标不是记录所有实现细节，而是帮助自己在忘记函数名或调用关系时，能在几十秒内重新定位代码。

## 当前项目主分层

### TaxiSystem

负责什么：

1. 作为业务编排入口
2. 管 taxi 注册、上下线、状态切换、位置更新
3. 在业务动作中调用空间索引和派单策略
4. 保证 taxi 状态和空间索引状态尽量同步

不负责什么：

1. 不直接实现空间搜索算法
2. 不直接实现具体派单策略
3. 不应该长期承担完整订单生命周期细节

最常用入口：

- `create_taxi`
- `register_taxi`
- `set_taxi_online`
- `set_taxi_offline`
- `update_taxi_position`
- `update_taxi_status`
- `dispatch_nearest`

代码位置：

- `include/taxi_system.h`
- `src/taxi_system.cpp`

---

### ISpatialIndex / KdTreeSpatialIndex

负责什么：

1. 管理空闲 taxi 的空间索引
2. 提供按半径搜索候选 taxi 的能力
3. 提供索引重建能力

不负责什么：

1. 不管理订单或请求生命周期
2. 不决定最终派给哪辆车
3. 不应该承载业务层状态机

最常用入口：

- `upsert`
- `erase`
- `radius_search`
- `rebuild`
- `size`
- `clear`

代码位置：

- `include/spatial_index.h`
- `include/kd_tree_spatial_index.h`
- `src/kd_tree_spatial_index.cpp`

---

### IDispatchStrategy / NearestFreeTaxiStrategy

负责什么：

1. 在给定候选状态下决定选哪辆 taxi
2. 实现可替换的派单策略
3. 作为后续最近车、batch matching、MCMF 的扩展点

不负责什么：

1. 不应该直接管理 taxi 生命周期
2. 不应该直接记录 request 和 taxi 的绑定事实
3. 不应该成为业务状态机本体

当前注意点：

1. 当前默认策略里混入了少量 stale index 清理逻辑
2. 后续如果上 batch matching / dry-run / MCMF，最好逐步收敛为“纯决策接口”

最常用入口：

- `select_taxi`

代码位置：

- `include/dispatch_strategy.h`
- `include/nearest_free_taxi_strategy.h`
- `src/dispatch_strategy.cpp`

---

### Taxi / Point / TaxiStatus

负责什么：

1. 定义当前系统最基础的领域对象
2. 作为业务层、索引层、策略层共享的数据表示

当前核心状态：

- `TaxiStatus::free`
- `TaxiStatus::occupy`
- `TaxiStatus::offline`

代码位置：

- `include/taxi_domain.h`
- `src/taxi_domain.cpp`

## 下一步准备新增的层

### RequestContext

目标：

补上“请求生命周期和 request-taxi 绑定事实”这一层，解决当前系统只有 taxi busy/free 状态、但没有显式请求绑定的问题。

负责什么：

1. 管 request 生命周期
2. 管 request 和 taxi 的绑定关系
3. 提供 request 状态查询

不负责什么：

1. 不做派单算法
2. 不做空间索引维护
3. 不直接更新 taxi 位置
4. 不替代 TaxiSystem 做总编排

建议最小状态集合：

- `pending`
- `dispatched`
- `serving`
- `completed`
- `canceled`

建议最小入口：

- `create_request`
- `assign_taxi`
- `start_trip`
- `complete_request`
- `cancel_request`

关键约束：

1. 一个 request 同时只能绑定一辆 taxi
2. 一个 serving 中的 request 不能无语义消失
3. 一个 occupy 的 taxi 不应直接下线而不处理绑定 request

## 以后忘了函数时怎么找

1. 先判断这是“业务编排、空间搜索、派单决策、请求绑定”中的哪一类问题
2. 再进入对应模块找入口函数
3. 不要先全局搜模糊动词，比如 `update`、`handle`、`process`
4. 先找业务句子式函数名，比如 `set_taxi_online`、`dispatch_nearest`、`assign_taxi`

## 当前项目最重要的设计原则

1. 不过度设计
2. 频繁变更的地方留接口
3. 相对稳定的地方保持薄接口、可读、易找回
4. 先把业务生命周期闭环补齐，再考虑更复杂的算法层
