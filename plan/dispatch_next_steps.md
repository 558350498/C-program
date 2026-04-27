# 调度系统下一阶段计划

## 当前判断

下一步不要直接开写完整 MCMF。更稳的顺序是：

1. 先补批量匹配的数据入口和候选边生成。
2. 再写一个批量贪心 baseline。
3. 最后把 MCMF 作为新的 `IDispatchStrategy` / batch strategy 实现接进来。

原因很简单：真正容易乱的是输入批次、候选边、请求状态释放和结果回写。

## 目标

在当前 `TaxiSystem + IRequestContext + ISpatialIndex + IDispatchStrategy` 骨架上，继续补齐离线数据回放和批量匹配能力，为后续接入 Kaggle CSV、tile 地图和最小费用最大流策略做准备。

当前优先级仍然是把 C++ 作业版本跑稳，不提前引入复杂服务化和跨语言工程。

## 下一阶段主线

### 1. 数据标准化

- 定义请求、行程、车辆状态、tile 标识等内部统一类型。
- 增加 CSV 预处理层，不让调度层直接依赖原始字段名。
- 统一时间单位、坐标清洗规则和无效数据过滤规则。

建议最小类型：

- `PassengerRequest`
- `TripRecord`
- `DriverSnapshot`
- `TileId`
- `Assignment`

### 2. 批量匹配输入

- 定义一个 batch 输入结构，包含一批可用 drivers 和一批 pending requests。
- batch 输入只使用快照数据，不直接暴露 `TaxiSystem` 内部容器。
- batch 输出统一为 `Assignment` 列表。

建议先不要让 batch strategy 直接修改系统状态。策略只负责给出结果，状态回写由 `TaxiSystem` 或后续的回放器处理。

### 3. 候选边生成

- 先用半径搜索或 top-k 近邻生成候选边。
- 每条边记录 `taxi_id`、`request_id`、`pickup_cost`。
- 第一版 cost 用接驾距离或估算接驾时间。
- 暂时不接真实路网。

这一步是 MCMF 的前置，不建议跳过。

### 4. 批量贪心 baseline

- 在同一套 batch 输入和候选边上先实现一个简单贪心策略。
- 用它验证数据流、候选边和结果回写是否正确。
- 后续 MCMF 的测试可以和 baseline 做对比。

### 5. MCMF 批量匹配策略

- 在候选边稳定后实现最小费用最大流。
- 第一版目标函数：最小化总接驾 cost。
- 第二版再考虑 request 等待时间。
- 第三版再考虑未服务惩罚。

当前不做：

- 不做全纽约全图完全匹配。
- 不做真实道路最短路。
- 不做收益预测和重定位收益。

### 6. 空间离散化

- 先做固定 tile 网格。
- 建立 `tile -> requests / free drivers` 状态容器。
- 保留 KD-Tree 作为 tile 内或候选集合内的细筛能力。

### 7. 事件驱动回放

- 做统一事件时间轴。
- 支持请求到达、车辆完成订单、车辆重新空闲等事件。
- 先用小时间窗口数据回放验证流程。

### 8. 指标与日志

- 记录匹配率、平均等待时间、平均接驾时间、未服务请求数。
- 对批次构图、未服务决策、状态释放失败补日志。

## 低优先级探索：Go + C++ 跨语言方案

这部分不是当前主线任务，只作为后续工程化方向保留。当前阶段不要因为它打断 C++ 作业版本。

### 方案 A：Go 调 C++ CLI

优先级：低，但如果以后要跨语言，这是最平滑的第一步。

思路：

- C++ 负责编译成 `matcher.exe` 这类算法可执行文件。
- Go 负责 CSV、tile、事件回放、Redis、WebSocket、HTTP API。
- Go 把一批 drivers 和 requests 序列化成 JSON / CSV / protobuf。
- C++ 从 stdin 或文件读取输入，输出 assignments。

适合边界：

- 输入：driver snapshot + request snapshot。
- 输出：assignment list。
- 不跨语言传 C++ 对象、指针、STL 容器或生命周期。

### 方案 B：Go 调 C++ 算法服务

优先级：更低，等主线稳定后再考虑。

思路：

- C++ 单独启动 matcher service。
- Go 通过 HTTP / gRPC 调用 C++ 算法服务。
- 两边通过明确的 DTO 通信。

### 当前不建议：cgo / dll

暂时不建议做 `Go + cgo + C++ dll`。

原因：

- C++ 类不能直接安全暴露给 Go。
- 需要包 C ABI。
- 内存分配和释放边界容易出错。
- Windows 链接配置麻烦。
- 对当前作业主线收益不高。

## 当前阶段原则

- 先保证 C++ 作业版本能独立交付。
- 跨语言只传快照数据，不传对象生命周期。
- Go 如果接入，优先负责非底层工程能力。
- C++ 优先保留算法核心和数据结构实验能力。
- 不因为未来可能服务化而提前污染当前接口。
