# FlightBench 参数与功能审计

版本：0.8.0  
环境：Windows 10、Qt 6.11.1、MAVSDK 3.17.1、`px4io/px4-sitl-gazebo:latest`

## 飞行页字段

| 界面字段 | 数据来源 | 处理与单位 | 无数据行为 |
|---|---|---|---|
| 相对高度 | `Telemetry::Position.relative_altitude_m` | 相对起飞参考面，米 | `—` |
| 水平速度 | `Telemetry::PositionVelocityNed.velocity` | `sqrt(north² + east²)`，m/s | `—` |
| 垂直速度 | `Telemetry::PositionVelocityNed.velocity.down_m_s` | NED 的 Down 取负，界面以上升为正，m/s | `—` |
| 横滚 / 俯仰 / 偏航 | `Telemetry::EulerAngle` | 度；姿态仪绑定横滚与俯仰实时绘制 | `—`，姿态仪保持水平占位 |
| 航向 | `Telemetry::Heading.heading_deg` | 归一化到 0–359°，并映射八方位中文 | `—` |
| 陀螺 R/P/Y | `Telemetry::Imu.angular_velocity_frd` | FRD 轴角速度由 rad/s 转为 °/s | `—` |
| GPS | `Telemetry::GpsInfo` | 定位类型与卫星数量 | `—` |
| 电池 | `Telemetry::Battery` | 剩余量 0–100%，电压 V | `—` |
| 飞行模式 | `Telemetry::FlightMode` | 映射为中文模式名 | `未知` |
| 解锁 / 在空中 | `subscribe_armed` / `subscribe_in_air` | PX4 实时布尔状态 | 已锁定 / 未在空中 |

核心遥测只有在位置、NED 速度、欧拉姿态和航向四类数据都至少收到一次后才标记为“遥测正常”。起飞入口和后端动作均要求核心遥测完整。

## 健康检查字段

“陀螺仪校准、加速度计校准、磁力计校准、本地位置、全局位置、Home 点”和“允许解锁”均来自 `Telemetry::Health`，是 PX4 健康判定，不是传感器瞬时读数。陀螺仪校准行的说明区域会另外显示实时 R/P/Y 角速度，避免把“校准通过”误解为“角速度为零”。全部健康项通过后仍需连续稳定 5 秒，才允许起飞。

## Docker 与链路状态

- Docker 可用：`docker version` 成功。
- SITL 运行：`docker inspect flightbench-px4` 返回运行中；程序每 3 秒静默核对一次，外部退出后会自动纠正状态。
- 镜像：`px4io/px4-sitl-gazebo:latest`。
- 模型：由“仿真环境”中已验证的多旋翼机型生成 `PX4_SIM_MODEL`；无界面模式固定为 `HEADLESS=1`。
- MAVSDK：监听 Windows `0.0.0.0:14540`。镜像入口主动把 MAVLink 发往宿主机，容器不发布 `14540/udp`，避免抢占 MAVSDK 的监听端口。
- 系统发现：MAVSDK 新系统通知会投递到 Qt 线程，并由 500 ms 轮询兜底，避免 `has_autopilot()` 状态更新竞态。
- 遥测频率设置使用异步 API，避免阻塞 QML 界面线程。

## 操作与设置

- 启动仿真：仅在 Docker 可用、无同名运行容器且控制器空闲时启用。
- 停止仿真：只停止固定名称 `flightbench-px4`；本程序创建的临时容器在退出时清理。
- 安全起飞：要求已连接、健康稳定、核心遥测完整、未在空中且无动作执行中；先设置 1–20 m 高度，再解锁并起飞。
- 降落：要求已连接且已解锁或在空中；调用 MAVSDK Action 降落。
- 起飞高度：1–20 m，使用 `QSettings` 保存。
- 键盘飞行控制：仅在已连接、健康稳定、核心遥测完整、已解锁且在空中时允许启动；使用 `Offboard::VelocityBodyYawspeed`，前/右/下速度单位为 m/s，偏航角速度单位为 °/s。
- 控制档位：慢速为 0.5/0.3 m/s 与 15°/s，标准为 1.0/0.5 m/s 与 30°/s，快速为 2.0/0.8 m/s 与 60°/s；水平/垂直/偏航分别限幅。
- 精确转向：以当前 `Telemetry::Heading.heading_deg` 加上 ±15°、±45°或±90°生成目标航向；每 50 ms 计算最短有符号角差，偏航速度按误差比例减小并限制在当前档位上限，误差不超过 1.5°时归零完成。手动按键或任何安全归零动作会立即取消闭环。
- 失效保护：QML 每 100 ms 刷新按键轴值，C++ 以 20 Hz 发送指令；超过 350 ms 未收到输入、窗口失焦、页面切换、连接/健康/解锁/在空中状态失效时归零或退出，降落前先停止 Offboard。
- 飞行会话：仿真开始时自动创建，停止仿真或退出程序时结束；记录机型、默认起飞高度、时长、健康与起降结果、最大高度/速度、问题数和采样数。
- 会话事件：Docker、SITL、MAVSDK、PX4 健康状态、起飞、降落和当前问题只写入本次会话，不混入其他运行。
- 遥测 CSV：会话期间以 5 Hz 写入 32 个字段，包含连接/准备/解锁/在空中状态、核心飞行数据、姿态、陀螺仪、GPS、电池、Offboard 是否启用、前/右/上/偏航四项实际控制指令，以及精确转向是否运行和目标航向；布尔值用 `0/1`，数值沿用本表单位。
- 本地保存：每个会话独立保存在“文档\\FlightBench\\Sessions\\<时间-机型>”；列表最多加载最近 50 条但不会自动删除旧目录，导出 CSV 时复制到“文档\\FlightBench\\Exports”。

## 实机（SITL）验证结果

自动测试完成了 Docker 启动、MAVSDK 连接、全部遥测订阅、健康稳定、5 m 指令起飞、4 m 以上验证、键盘 Offboard 前进、45°精确转向及安全退出、降落、解锁复位、会话摘要及三个数据文件落盘和容器清理。验证样本包括：航向闭环误差、GPS、电池、上升/下降速度、水平运动响应和控制指令列，测试最终输出 `FLIGHT_TEST_OK`。

该结论只适用于本机 PX4 SITL 软件仿真，不构成真实无人机适航验证。
