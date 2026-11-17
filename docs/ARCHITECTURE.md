# 代码组织与架构总览

> 面向外部顾问 / 新接手开发者的项目说明。接口签名与字段细节见
> [`DRIVER_API.md`](DRIVER_API.md)，接线与烧录见 [`README.md`](../README.md)。

## 1. 项目概况

ESP32-S3（ESP-IDF 6.1 / FreeRTOS）麦克纳姆轮工程车：

- 5 路 DRV8825 步进（4 轮 + 1 升降，**1/16 细分**，STEP/DIR 控制）
- 8 路模拟循迹（TCA9548A CH0/CH1 → 双 ADS1115）
- LU9685 16 路舵机（底座 CH0 / 物料盘 CH4 / 夹爪 CH8）
- K230D（CanMV/MicroPython）视觉板，UART 自定义二进制协议
- 自动比赛流程：循迹到站 → 视觉识别 → 对位 → 夹取 → 装盘 → 送至靶环 → 放置
- 内嵌 Web 调试台：手动遥控、8 路电压、PID 在线调参、舵机与比赛控制

| 指标 | 数值 |
|---|---|
| C/H 代码 | 约 7.2k 行 |
| K230 Python | 约 0.6k 行 |
| 最近构建 | 0 warning / 0 error |
| 固件 | `0xEC150`（≈944 KiB），1500K app 分区剩余 38% |

## 2. 目录与分层

```
main/
├── app_config.h          # 唯一硬件/机械/速度参数入口
├── main.c                # 初始化顺序、低频健康检查
│
├─ 驱动层
│  ├── stepper.c/.h       # 5 轴 DDS 脉冲、S 形加减速、开环定步/位置
│  ├── i2c_bus.c/.h       # I2C + TCA9548A 复位/选通/总线锁
│  ├── ads1115.c/.h       # 双 ADS1115 乒乓采样任务
│  ├── lu9685.c/.h        # 舵机三协议；位置舵机角度/软限位/步进；连续舵机点动
│  └── mecanum.c/.h       # O 型麦轮逆运动学与步频换算
│
├─ 控制层
│  ├── chassis.c/.h       # 100Hz 仲裁：手动/循迹/定步平移、升降、看门狗
│  ├── line_follow.c/.h   # 8 路模拟量加权 PID
│  └── station_detector.c # 横向站点线检测（宽线 + 去抖 + 重新武装）
│
├─ 视觉层
│  ├── vision_protocol.c  # A5 5A 帧、CRC8、流式解析
│  └── k230_link.c/.h     # UART1、模式命令/ACK 重试、结果新鲜度
│
├─ 比赛层
│  ├── manipulator.c      # 取放动作脚本（Z 定步 + 舵机时序）
│  ├── tray_controller.c  # 6 盘位连续舵机分度状态机（递归锁）
│  └── competition.c      # 16 状态比赛状态机、配置、超时、急停
│
├─ 服务层
│  ├── wifi_sta.c         # STA 连接 + 扫描诊断 + AP 兜底热点
│  ├── web_server.c       # 19 个 HTTP GET 接口
│  ├── web_page.h         # 内嵌单页控制台（HTML/CSS/JS）
│  ├── settings.c/.h      # NVS 持久化（PID/阈值/速度/舵机校准/机械臂示教标定）
│  └── safety.c/.h        # 全局急停锁存 / 解除
│
k230/                     # CanMV 端：config / protocol / vision / main
docs/DRIVER_API.md        # 驱动 API 手册
docs/ARCHITECTURE.md      # 本文档
```

## 3. 任务与中断

| 核 | 执行体 | 周期 | 优先级 | 栈 |
|---|---|---|---|---|
| 中断 | GPTimer 50 µs ISR（5 路相位累加 + 计步） | 20 kHz | 硬件 3 | — |
| Core 0 | `step_ramp` 速度斜坡（S 形） | 1 ms | 12 | 4 KB |
| Core 0 | `k230_link` UART 收发/心跳 | 20 ms | 4 | 4 KB |
| Core 0 | `settings` NVS 防抖保存 | 500 ms | 2 | 4 KB |
| Core 0 | Wi-Fi / HTTPD（httpd 栈 12 KB） | 事件 | IDF 默认 | — |
| Core 1 | `chassis` 控制环 | 10 ms | 6 | 4 KB |
| Core 1 | `ads_sample` 8 路采样 | 12 ms | 4 | 4 KB |
| Core 1 | `competition` 状态机 | 10 ms | 3 | 8 KB |
| Core 1 | `srv_pulse` 舵机点动计时 | 5 ms | 3 | 4 KB |

设计原则：脉冲由硬件定时器 ISR 产生，不依赖任务调度；控制环优先级高于采样与
比赛逻辑，采样等待可被控制环抢占。

## 4. 控制链路

```
Web/HTTP ──► chassis(手动/循迹/定步) ──► mecanum ──► 4 轮目标步频 ──┐
ADS1115 ──► 8 路电压 ──► line_follow PID ──► ω ───────────────────┤
K230 ──► 检测帧 ──► competition 状态机 ──► manipulator/tray/chassis ┤
                                                                    ▼
                                              stepper 目标速度 ──► 1kHz S 形斜坡
                                                                    ▼
                                              20kHz GPTimer ISR ──► STEP 脉冲 + 位置计数
```

- **手动**：方向键只给方向，四轮步频峰值归一化到 `chassis_base_speed`。
- **循迹**：基础航速 = `chassis_base_speed`；PID 输出转向；**丢线不停车**，
  保持上次转向继续前进。
- **比赛**：完整流程为收臂 → 循迹 → 刹车 → 识别 → 微调对位 → 取放 → 分度 →
  再次循迹；站点由 8 路探头识别横向宽线触发。当前调试策略为
  **“逢横线即停”**：循迹途中一旦触发横线立即停车进入 `DEBUG_PAUSE`，
  由 Web 人工完成抓取，点“恢复循迹”后自动定步跨线并继续。
- **升降**：开环定步（ISR 计步），带 `[0, z_max]` 软限位；点动/定步互斥。
- **舵机**：CH0 底座、CH4 物料盘为连续舵机，用 **定时点动** 控制——
  CH0 按标定的 `bspeed/bsweep` 把角度差换算成毫秒数，到点自动回中位停止，
  逻辑行程 0~360°，单次最长 10s；CH4 只做盘位分度/点动。
  CH8 夹爪是位置舵机，写角度即保持，软限位取标定的开/合角度区间。
- **急停**：`safety` 模块锁存急停，立即清空步进脉冲、停底盘/升降、取消舵机
  定时并中止比赛；锁存期间运动接口 423，解除后恢复手动控制。

### 4.1 比赛状态机

```
MANUAL → ARM_STOW → SOURCE_TRAVEL → SOURCE_BRAKE → SOURCE_IDENTIFY
       → SOURCE_ALIGN → SOURCE_PICK_LOAD → SOURCE_INDEX
       →（已装<3 回 SOURCE_TRAVEL；否则）TARGET_TRAVEL → TARGET_BRAKE
       → TARGET_IDENTIFY → TARGET_ALIGN → TARGET_SELECT → TARGET_PLACE
       →（已放<3 回 TARGET_TRAVEL；否则）FINISHED
任意状态故障 / 超时 / 急停 → ABORTED
```

当前调试策略下，`SOURCE_TRAVEL` / `TARGET_TRAVEL` 检测到横线后不进入
BRAKE/IDENTIFY/ALIGN/PICK，而是进入 `DEBUG_PAUSE`：

```
TRAVEL ──横线──► DEBUG_PAUSE
                    ├─ execute（物料站）→ SOURCE_PICK_LOAD → SOURCE_INDEX → TRAVEL
                    ├─ execute（靶环站）→ TARGET_PLACE → TRAVEL / FINISHED
                    └─ resume/skip → 定步跨线 → 回到原 TRAVEL
```

暂停期间 HTTP 放行 `/api/move`、`/api/lift`、`/api/servo` 以及物料盘分度；
人工对位后由 `execute` 启动既有动作脚本，全程不依赖 K230。跳过时底盘前移
`DEBUG_RESUME_CROSS_STEPS`（1500 微步 ≈ 96mm），并重置/重新武装站点检测。

示教：暂停时手动把底座/夹爪/升降摆到目标位置，选择步骤并调用
`/api/competition/teach?step=...`，当前角度/Z 位置写入该动作步骤并立即存 NVS
（`tuning/comp`）；标定整体在 `competition_init()` 后自动恢复。

启动前置条件：配置已确认、Z 零点有效、盘位零点有效、两路 EN 已打开、
所有执行器静止；**不要求 K230 在线**。

## 5. 并发与锁

| 锁 | 保护对象 | 说明 |
|---|---|---|
| `s_lock`（比赛互斥） | 比赛状态/配置/零点 | HTTP 与比赛任务共用 |
| `s_control_mux`（自旋锁） | 模式、速度指令、升降指令、代次 | 100Hz 控制环与 HTTP 共用 |
| `s_move_mux`（自旋锁，ISR 也用） | 目标速度、相位、定步进度 | ISR 与斜坡任务共用 |
| 托盘递归锁 | 6 盘位状态/分度过程 | 比赛任务与 HTTP 手动命令共用 |
| I2C 互斥锁 | “选 TCA 通道 + 事务”原子性 | 所有 I2C 器件共用 |
| ADS 数据锁 | 8 路电压缓存 | 采样任务与读取方 |
| settings 互斥锁 | NVS blob | 保存任务与 HTTP |

锁顺序：`比赛锁 → 底盘控制锁 → 步进 move 锁`；托盘锁 / I2C 锁不反向获取比赛锁。
所有运动入口拒绝 NaN/Inf；控制环用“代次（generation）”防止旧周期覆盖新命令。

## 6. 单位与关键参数（1/16 细分）

- 200 整步/圈 × 16 = **3200 微步/圈**；约 **15670 微步/(m/s)**，1 微步 ≈ 0.064 mm。
- 单轴限速 **10000 微步/秒**（ISR 每 50 µs 最多一个脉冲，**10 kHz 硬上限**，约 0.64 m/s）。
- 底盘默认 **2400 微步/秒（150 整步）**，范围 800~4800；
  升降默认 6400 微步/秒（400 整步），范围 1600~10000。
- 默认加速度 19200 微步/秒²，起步 960 微步/秒（底盘/升降分别派生）。
- Z 轴默认标定：运输 0、地面抓取 96000、盘内 40000、靶环 96000、软限位 160000 微步。
- 所有速度/步数/加速度单位都由 `app_config.h` 的 `WHEEL_MICROSTEP` /
  `LIFT_MICROSTEP` 派生，换细分只改这两个宏。

## 7. 对外接口

### 7.1 HTTP（全部 GET，局域网调试用）

`/api/status`、`/api/move`、`/api/mode`、`/api/en`、`/api/lift`、`/api/servo`、
`/api/line`、`/thresh`、`/speed`、`/servo_proto`、`/servo_pulse`、`/servo_center`、
`/api/competition`、`/api/competition/cmd`、`/api/competition/config`、
`/settings`、`/api/stop`。

### 7.2 K230 串口（UART1，GPIO43/44，115200 8N1）

```
A5 5A | ver 0x01 | type | seq | len | payload | CRC8(0x07)
```

| 类型 | 方向 | payload |
|---|---|---|
| `0x01` 模式命令 | ESP32 → K230 | 1 字节：0 IDLE / 1 MATERIAL / 2 RING / 3 TRAY |
| `0x02` 心跳 | 双向 | 空，500 ms |
| `0x03` ACK | K230 → ESP32 | `command_seq, current_mode` |
| `0x10` 检测 | K230 → ESP32 | 11 字节：mode/color/flags/cx/cy/area/conf/reserved |

ESP32 侧命令带 ACK 等待与 2 次重发；检测结果新鲜度 400 ms，链路在线 1500 ms。

## 8. 持久化与调试

- NVS 保存 PID、阈值/极性、加速度、底盘/升降速度、舵机协议与中位；
  格式 v2，上电自动迁移 v1（保留 PID，速度/加速度重置为微步默认）。
- 网页调参实时生效，2 秒防抖自动保存；8 路电压条、阈值线、err/ω 实时显示，
  舵机滑块支持滚轮 1° 微调与中位校准。
- Wi-Fi 连不上时自动开 `mecanum-car` 热点（192.168.4.1）兜底。

## 9. 当前限制 / 待讨论点

1. **全开环**：无编码器，位置=指令脉冲，轮子打滑/丢步无反馈；Z 轴依赖人工零点。
2. **软件脉冲上限 10 kHz**：提高极速需改架构（更高 tick、硬件 PWM 或换驱动），
   1/16 细分下极速约 0.64 m/s。
3. **循迹丢线不停车**：保持上次转向继续前进，赛道断线/反光时有冲出去的风险
   （为比赛连续性做的取舍）。
4. **比赛逻辑绑定固定赛道**：3 取 3 放、横向宽线站点、盘位 6 分度；
   换赛道需重新标定视觉 ROI/阈值/站点参数。
5. **视觉标定依赖现场光照**：LAB 阈值、ROI、面积阈值在 `k230/config.py`，需实车调。
6. **Web 无鉴权、全 GET**：只适合隔离局域网/热点；公网暴露需加 POST/Token/CSRF 防护。
7. **无自动化测试**：验证靠编译 + 台架/实车；协议与状态机经过静态审计但无单元测试。
8. **单网络**：仅一个 SSID + AP 兜底，不支持多 AP 记忆。
9. **升降动作超时**：低速（1600 微步/秒）下 Z 行程较长，极端配置可能触及
   180 秒动作超时。

## 10. 快速定位

| 想改什么 | 看哪里 |
|---|---|
| 引脚、地址、机械尺寸、速度默认值 | `main/app_config.h` |
| 脉冲/加减速/定步 | `main/stepper.c` |
| 手动/循迹/位置模式仲裁 | `main/chassis.c` |
| PID 与丢线逻辑 | `main/line_follow.c` |
| 比赛流程与超时 | `main/competition.c` |
| 取放动作脚本 | `main/manipulator.c` |
| 物料盘分度 | `main/tray_controller.c` |
| K230 协议 | `main/vision_protocol.c`、`k230/protocol.py` |
| HTTP 接口 | `main/web_server.c` |
| 网页 UI | `main/web_page.h` |
| 参数持久化 | `main/settings.c` |
