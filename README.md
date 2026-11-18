# 工创赛麦克纳姆轮小车 · ESP32-S3 底层驱动 + Web 调试台

基于 **ESP-IDF v5.2+ / v6.x** 与 FreeRTOS 的高实时性底盘、机械臂与自动比赛控制系统。

> 本工程已在本机 **ESP-IDF v6.1.0** 上完成编译验证：**0 warning / 0 error**，
> 固件 0xEC150（966992 字节，约 944 KiB），2MB Flash 的 1500K app 分区剩余 38%。

---

## 1. 目录结构

```
helloworld/
├── CMakeLists.txt              # 顶层工程
├── sdkconfig.defaults          # FreeRTOS 1000Hz / 1.5MB app 分区 / USB 控制台等默认配置
├── README.md
├── docs/
│   ├── ARCHITECTURE.md         # ★ 代码组织与架构总览（交接 / 外部咨询）
│   └── DRIVER_API.md           # ★ 驱动接口手册（二次开发 / 其他 agent 调用）
├── k230/                       # K230D / CanMV 视觉端
│   ├── config.py               # UART、心跳、检测周期、LAB 阈值、ROI
│   ├── main.py                 # 主循环：收命令、ACK、心跳、检测上报、屏幕状态
│   ├── servo_align.py          # ★ DNK230D 专用：ST7701 640x480 + PORT1 UART1 + ROI/协议
│   ├── protocol.py             # A5 5A 帧编解码、CRC8、流式解析、发送序号
│   ├── vision.py               # blob 识别、颜色投票、置信度
│   └── from_Gemini.py          # 原始 Gemini 参考脚本（协议未适配，仅供对照）
└── main/
    ├── CMakeLists.txt          # 组件源文件与依赖
    ├── main.c                  # 启动流程、任务初始化、健康检查
    ├── app_config.h            # ★ 全部引脚 / I2C 地址 / 机械与速度参数
    ├── stepper.h / .c          # 5 轴 DDS 脉冲发生器 + 开环定步/位置计数
    ├── mecanum.h / .c          # O 型麦轮逆运动学
    ├── i2c_bus.h / .c          # I2C 主总线 + TCA9548A 复位/选通道/互斥锁
    ├── ads1115.h / .c          # 双 ADS1115 乒乓流水采样（Core 1）
    ├── lu9685.h / .c           # LU9685：位置舵机角度/软限位/步进 + 连续舵机点动
    ├── line_follow.h / .c      # 8 路模拟量加权 PID
    ├── chassis.h / .c          # 手动 / 循迹 / 定步平移 / 升降统一控制
    ├── vision_protocol.h / .c  # 视觉帧常量、CRC8、解析与编码
    ├── k230_link.h / .c        # UART1 链路：心跳、模式命令、ACK、检测结果
    ├── station_detector.h / .c # 8 路探头横线/工位检测（上升沿触发）
    ├── tray_controller.h / .c  # 6 工位物料盘分度与颜色台账
    ├── manipulator.h / .c      # 升降 + 底座 + 夹爪动作脚本
    ├── competition.h / .c      # 自动比赛状态机、标定与安全约束
    ├── settings.h / .c         # 调参参数 NVS 持久化（PID/阈值/速度/舵机校准）
    ├── safety.h / .c           # 全局急停锁存 / 解除
    ├── wifi_sta.h / .c         # Wi-Fi STA + 断线重连 + AP 兜底热点 + 断开原因日志
    ├── web_server.h / .c       # HTTP API
    └── web_page.h              # 内嵌单页 HTML/JS 控制台（含比赛卡片）
```

---

## 2. 硬件接线（与需求逐项一致）

### 2.1 步进电机（5 × DRV8825，1/16 细分）

> **细分说明**：DRV8825 的 MS1/MS2/MS3 已通过跳线设为 **1/16 细分**（不占 GPIO）。
> 因此固件里所有“步/秒、步数、加速度”都是**微步**单位，是整步数值的 16 倍：
> 150 步/秒（整步）≡ 2400 微步/秒，Z 轴 6000 步 ≡ 96000 微步。
> 若某块驱动仍是整步，把 `app_config.h` 里对应的 `WHEEL_MICROSTEP` /
> `LIFT_MICROSTEP` 改回 1 即可，所有派生参数会自动缩放。

| 轴 | 位置 | STEP | DIR | 说明 |
|---|---|---|---|---|
| M1 | 右后 RR | GPIO 3 | GPIO 46 | |
| M2 | 右前 RF | GPIO 17 | GPIO 18 | |
| M3 | 左前 LF | GPIO 12 | GPIO 11 | 对置安装，DIR 逻辑默认取反 |
| M4 | 左后 LR | GPIO 14 | GPIO 13 | 对置安装，DIR 逻辑默认取反 |
| M5 | 升降轴 | GPIO 15 | GPIO 16 | |

| 使能 | 引脚 | 逻辑 |
|---|---|---|
| 底盘 M1~M4 | GPIO 42 | 低 = 锁轴使能，高 = 脱机释放 |
| 升降 M5 | GPIO 41 | 低 = 锁轴使能，高 = 脱机释放 |

- 上电后 `stepper_init()` **最先执行**，两路 EN 立即拉高释放力矩，避免发热。
- 左侧 DIR 取反可在运行时用 `stepper_set_dir_invert(AXIS_LF/LR, true/false)` 调整。
- 升降轴支持**开环定步**：位置计数来自 ISR 实际发出的 STEP 脉冲，不是机械反馈；
  自动比赛依赖上电后的 Z 零点标定建立坐标。

### 2.2 I2C 与传感器

| 器件 | 连接 |
|---|---|
| I2C 主总线 | SDA = GPIO 8，SCL = GPIO 9，400 kHz |
| TCA9548A | 地址 0x70，RESET = GPIO 40（初始化拉低 20ms 再拉高） |
| CH0 | ADS1115 @0x48，A0~A3 → 探头 **5/6/7/8** |
| CH1 | ADS1115 @0x49，A0~A3 → 探头 **1/2/3/4** |
| CH2 | LU9685 @0x40 或 0x00（自动探测） |

| 舵机 | LU9685 通道 | 类型 |
|---|---|---|
| 底座旋转（MG996R 180°） | CH0 | **位置舵机**，绝对角度 0~180 |
| 物料盘（MG90S） | CH4 | 连续旋转，定时分度（3 槽，每格 120°） |
| 夹爪（MG996 连续） | CH8 | 连续旋转，速度角 + 通电时间开合 |

**LU9685 协议**：默认单字节角度 `[ch, 0~180]`。若你的模块固件使用脉宽寄存器，
可运行时切到双字节 `[ch, pulse_lo, pulse_hi]`（500~2500us）或大端版本，
在网页"协议模式"下拉框或 `/servo_proto?mode=0|1|2` 切换。

### 2.3 K230 视觉串口

| 项目 | 值 |
|---|---|
| ESP32 逻辑串口 | UART1（重映射到已确认的 UART0 排针） |
| ESP32 TX | GPIO 43 → K230 RX |
| ESP32 RX | GPIO 44 ← K230 TX |
| 波特率 / 格式 | 115200，8N1，无硬件流控 |
| K230 端口 | **PORT1（UART1）**：GPIO40=TX、GPIO41=RX，115200 8N1 |
| K230 屏幕 | ST7701 **640×480**，摄像头 VGA 640×480（DNK230D 官方例程配置） |
| ESP32 控制台 | 已迁至 **USB Serial/JTAG**（`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`） |

- 两板必须共地；交叉连接：**K230 GPIO40(TX) → ESP GPIO44(RX)**、
  **K230 GPIO41(RX) ← ESP GPIO43(TX)**。
- 因为 GPIO43/44 被视觉链路占用，`idf.py monitor` 需走 ESP32-S3 原生 USB 口，
  不要再接 UART0 排针。
- 上电后 `k230_link_init()` 会主动发送一次 `IDLE` 命令，把可能停留在上次模式的
  K230 拉回待机。

**帧格式**（ESP32 与 K230 完全一致）：

| 偏移 | 字节 | 字段 |
|---|---|---|
| 0~1 | 2 | 同步字 `A5 5A` |
| 2 | 1 | 版本 `0x01` |
| 3 | 1 | 类型 |
| 4 | 1 | 序号 seq（0~255 循环） |
| 5 | 1 | payload 长度（0~32） |
| 6… | N | payload |
| 末尾 | 1 | CRC8 |

CRC8 参数：多项式 **0x07**、初值 0、无反射、xorout 0；覆盖 **版本到 payload 末尾**
（即帧头 `0xA5 0x5A` 不参与计算）。帧开销固定 7 字节，单帧最大 39 字节。

| 类型 | 名称 | 方向 | payload |
|---|---|---|---|
| `0x01` | COMMAND | ESP32 → K230 | 1 字节模式：0 IDLE / 1 MATERIAL / 2 RING / 3 TRAY |
| `0x02` | HEARTBEAT | 双向 | 空，**500ms** 周期 |
| `0x03` | ACK | K230 → ESP32 | `command_seq, current_mode` |
| `0x10` | DETECTION | K230 → ESP32 | 11 字节，见下 |

DETECTION 的 11 字节布局：

```
mode, color, flags, cx_lo, cx_hi, cy_lo, cy_hi, area_lo, area_hi, confidence, reserved
```

- 坐标与面积均为小端 16 位；`flags` bit0 = detected，bit1 = stable。
- `color`：0 未知 / 1 红 / 2 绿 / 3 蓝。
- 无目标时也会按周期上报，颜色、坐标、面积、置信度清零；ESP32 收到后把
  `stable` 强制为 false。
- K230 在 `DEBUG_UART_LOG=False` 时不会向协议 UART 写任何文本；**正常比赛必须保持关闭**，
  否则 `K230 ...` 文本会污染二进制帧解析。

**K230 端脚本选择**：

| 文件 | 用途 |
|---|---|
| `k230/main.py` | 工程默认视觉节点：模式状态屏幕、心跳、检测上报 |
| `k230/servo_align.py` | **DNK230D 专用**：按官方例程用 `Sensor(VGA)` + `Display.ST7701 640×480`；FPIOA 将 GPIO40/41 映射为 UART1；保留"最大色块 + 准星 + dx/dy"，含 MATERIAL/RING ROI、命令/ACK/心跳/80ms 上报 |
| `k230/from_Gemini.py` | 原始参考脚本，CRC 覆盖范围与串口均未适配，**不要直接用于比赛** |

`servo_align.py` 是 **正点原子 DNK230D 专用版**（ST7701 640×480 + UART1 GPIO40/41），
与 `main.py` 任选其一运行，二者不会同时运行。

---

## 3. 自动比赛流程（三取三放）

### 3.0 比赛策略：横线 → 盲走 → 视觉抓取（全自动）

`main/competition.c` 顶部 `COMPETITION_AUTO_MODE = 1`，比赛全程无人：

1. 循迹检测到物料站横线 → 停车（BRAKE）→ 按 `source_enter_mm` **盲走**到第一个物料进入相机视野。
2. K230 识别颜色 → 按 aim 比例平移对位 → 底座/升降/夹爪完成抓取并装盘。
3. 物料盘转 120°（下一空槽）→ 按 `source_step_mm` 盲走到下一个物料 → 重复直到 3 个抓完。
4. 抓完 3 个：按 `source_exit_mm`（通常为负）盲走回黑线 → 恢复循迹驶向靶环区。
5. 靶环站横线 → 停车 → 按 `target_enter_mm` 盲走到第一个靶环 → 识别颜色 → 对位 →
   从对应盘位取料放置。
6. 按 `target_step_mm` 盲走到下一个靶环，重复 3 个 → `FINISHED`。
7. 全自动下丢线 3s、视觉超时、动作超时、K230 离线都会安全中止。

盲走距离在网页比赛卡片的
“物料：横线→首料 / 物料间距 / 抓完回线 / 靶环：横线→首环 / 靶环间距”中标定，
标定完点 **“一键保存全部（标定 + 调参到 Flash）”** 写入 NVS。

需要人工调试时，把 `COMPETITION_AUTO_MODE` 改成 `0` 重新编译，即回到
“逢横线即停、网页执行/跳过”的半自动模式。

### 3.1 状态机

```
MANUAL → ARM_STOW → SOURCE_TRAVEL → SOURCE_BRAKE
      → BLIND_MOVE(横线→首料) → SOURCE_IDENTIFY → SOURCE_ALIGN → SOURCE_PICK_LOAD
      → SOURCE_INDEX → BLIND_MOVE(物料间距) → SOURCE_IDENTIFY → …（共 3 个）
      → BLIND_MOVE(抓完回线) → SOURCE_TRAVEL（去靶环区）
      → TARGET_BRAKE → BLIND_MOVE(横线→首环) → TARGET_IDENTIFY → TARGET_ALIGN
      → TARGET_SELECT → TARGET_PLACE
      → BLIND_MOVE(靶环间距) → TARGET_IDENTIFY → …（共 3 个）→ FINISHED
任意故障 / 超时 / 手动中止 → ABORTED
```

| 状态 | 行为 |
|---|---|
| `MANUAL` | 待机，网页手动命令可用 |
| `ARM_STOW` | 收臂脚本：张开夹爪 → Z 回运输位 → 底座回运输角 |
| `SOURCE_TRAVEL` | 切循迹模式驶向物料区；横线触发后停车，交给 BRAKE |
| `SOURCE_BRAKE` | 等待四轮速度归零，然后按 `source_enter_mm` 盲走到首料 |
| `SOURCE_IDENTIFY` | K230 进入 MATERIAL 模式，等待稳定颜色与置信度达标 |
| `SOURCE_ALIGN` | 按视觉误差做短距开环平移，直到落在容差内或达到最大次数 |
| `SOURCE_PICK_LOAD` | 执行"地面抓取 → 装入盘内 → 回运输位"动作脚本 |
| `SOURCE_INDEX` | 物料盘分度 120°（3 槽）；随后按 `source_step_mm` 盲走到下一个物料 |
| `TARGET_TRAVEL` | 循迹驶向靶环区；横线触发后停车 |
| `BLIND_MOVE` | 按标定距离定步盲走（离开/返回黑线、物料或靶环间距） |
| `DEBUG_PAUSE` | 仅 `COMPETITION_AUTO_MODE=0` 调试：人工对位后执行/跳过 |
| `TARGET_BRAKE` | 停车后按 `target_enter_mm` 盲走到首环 |
| `TARGET_IDENTIFY` | K230 进入 RING 模式，识别目标颜色 |
| `TARGET_ALIGN` | 按靶环基准做视觉对位 |
| `TARGET_SELECT` | 在物料盘中查找目标颜色并分度到对应工位 |
| `TARGET_PLACE` | 放置动作；随后按 `target_step_mm` 盲走到下一个靶环 |
| `FINISHED` | 已放满 3 个，正常结束 |
| `ABORTED` | 任意故障、超时或手动中止 |

### 3.2 启动前置条件（`competition_start()` 逐项检查）

必须**同时满足**以下条件，否则返回 `ESP_ERR_INVALID_STATE`：

1. 当前未在运行（`running == false`）。
2. 标定已确认（`config_confirmed == true`，网页"保存并确认本次标定"）。
3. Z 零点有效（`z_zero_valid == true`，网页"设当前 Z 为运输零点"）。
4. 盘位零点有效（`tray.zero_valid == true`，网页"设当前盘位为 slot0"）。
5. 两路 EN 都使能（底盘 M1~M4、升降 M5）。
6. LU9685 在线、脉冲后台任务可用，且 CH4 无停止失败故障。
7. ADS1115 已有有效采样帧。
8. 所有执行器静止：物料盘空闲、机械臂空闲、无平移运动、升降空闲。

> 当前流程不要求 K230 在线，视觉板可以完全不接。

启动成功后先 `chassis_stop()` 强制回到手动并平滑停车，再重新应用标定、
安装升降软限位 `[0, z_max]`，然后进入 `ARM_STOW`。

### 3.3 中止语义与超时

- **中止（ABORTED）**：停止机械臂、底盘、物料盘分度，取消所有舵机点动，
  把 K230 切回 IDLE，并**同时作废 Z 零点与盘位零点**。重新启动前必须重新
  设 Z 零点、盘位零点并重新确认标定。
- **6 分钟总时限**：`run_elapsed_ms >= 360000` 立即以 `TIMEOUT` 中止。
- **丢线不停车**：循迹阶段看不到线时保持上一次转向、继续以基础航速前进，
  由站点检测或 6 分钟总时限兜底，不会因短暂断线停车。
- **刹车超时**：停车后等待四轮速度归零，允许时间为
  `max(1800ms, 当前最大轮速 / 加速度 × 1000 + 800ms)`；超时以 `CHASSIS` 中止。
  低加速度（如 100 步/秒²）会自动放宽，避免误报。
- **其他超时**：识别 12s、视觉对位 24s、单次取放动作 180s、传感器帧停滞 500ms。
- 网页 **“■ 全局中止”**（`/api/stop`）等价于手动中止，同样会作废两个零点。

---

## 4. 实时性设计

### 4.1 20 kHz DDS 脉冲（`stepper.c`）

- GPTimer 1 MHz 计数、50us 报警周期，ISR 中用 32 位相位累加器同时产生 5 路 STEP。
- STEP 高电平固定 **50us（≥3us）**，相邻脉冲之间至少 50us 低电平；ISR 每 tick
  最多发一个脉冲，所以**脉冲率硬上限 10 kHz**（1/16 细分下约 0.64 m/s 轮速）。
- 相位累加每拍无条件执行，保证步频精度；速度 → 相位增量在 1 kHz 任务中换算。
- **S 形软加减速**（降低噪声、发热与抖动）：
  - 起步速度 **960 微步/秒**（整步 60 步/秒的等价物理速度）；1/16 细分后低速
    共振明显减小，不再沿用整步时代的高速起步硬跳；
  - 速度接近 0 或接近目标时自动降低加速度（`accel_scale` 钟形系数），减小冲击；
  - 默认加速度 **19200 微步/秒²**（= 整步 1200），可 Web 滑块或 `stepper_set_accel()` 调整；
  - 开环定步运动起步速度 640 微步/秒（升降同理），并按剩余脉冲数限制速度；
  - **换向必须先减速到 0**，再切换 DIR，避免带速换向丢步；
  - 默认底盘航速 **2400 微步/秒**（整步 150），滑块 800~4800 微步/秒
    （整步 50~300）；升降默认 **6400 微步/秒**（整步 400），滑块 1600~10000。
- 定步运动的位置计数在 ISR 内累加，最后一个脉冲由 ISR 自己封口，不再补发。

### 4.2 ADS1115 乒乓流水（`ads1115.c`）

- 新 I2C 主驱动，所有事务 **20ms 超时**，总线级互斥锁防止通道错切。
- 860 SPS、±6.144V（PGA=2/3）、单次转换；单次转换 1.163ms，等待 1.25ms。
- 两块 ADS **并行转换**：触发 A 板 → 触发 B 板 → 读 A → 触发 A 下一路 → 读 B → …
- 8 路完整刷新约 **6~8ms**，刷新周期 12ms（约 83Hz），彻底消除秒级滞后。
- 单次总线错误沿用上一拍缓存值，不会冻结整帧数据。

### 4.3 任务/核心分配

| 核心 | 任务 | 周期/优先级 |
|---|---|---|
| Core 0 | GPTimer 脉冲 ISR | 50us / 中断 |
| Core 0 | 步进速度斜坡 `step_ramp` | 1kHz / prio 12 |
| Core 0 | K230 链路 `k230_link` | 20ms / prio 4 |
| Core 0 | Wi-Fi、HTTP 服务 | 事件驱动 |
| Core 1 | ADS1115 采样 `ads_sample` | 12ms / prio 4 |
| Core 1 | 底盘控制环 `chassis` | 10ms / prio 6 |
| Core 1 | 比赛状态机 `competition` | 10ms / prio 3 |
| Core 1 | 舵机点动计时 `srv_pulse` | 5ms / prio 3 |

采样任务优先级低于控制环，等待转换的自旋不会阻塞 100Hz 控制。
比赛状态机与舵机点动优先级较低，不会挤占底盘与采样实时性。

### 4.4 安全机制

- Web 遥控指令 **400ms 看门狗**：浏览器断开或松键后自动停车。
- 升降点动 **500ms 看门狗**；定步升降运动不受该看门狗影响，由剩余步数结束。
- EN 关闭时底盘/升降目标强制清零，并取消进行中的定步运动。
- 升降软限位 `[0, z_max]`：点动与定步移动都会检查，防止开环越界。
- 手动循迹丢线：**不停车**，保持上次转向继续走；全自动比赛时丢线超过 **3s** 会安全中止。
- 自动比赛：传感器帧停滞 500ms、K230 离线、舵机故障、丢线 3s、动作超时都会中止。
- 连续舵机点动由后台任务计时，到点写停止角：CH8 夹爪/CH4 盘固定写 **90°** 停止，
  成功后追加 3 次确认写入；写失败会标记故障并持续重试，直到停止成功。
- 停车为梯形减速，急停可调用 `stepper_abort_all()`。

---

## 5. 机械参数（必须按实车修改）

在 `main/app_config.h` 中：

```c
#define WHEEL_DIAMETER_MM       80.0f    /* 轮径（实测） */
#define WHEEL_MICROSTEP         16       /* DRV8825 1/16 细分 */
#define WHEEL_STEPS_PER_REV     (200 * WHEEL_MICROSTEP)   /* 3200 微步/圈 */
#define WHEELBASE_MM            167.0f   /* 前后轴心距（实测） */
#define TRACK_MM                220.0f   /* 左右轮中心面距（实测） */
```

当前参数下：`k = (167 + 220) / 2 = 193.5 mm`，
每米约 `3200 / (π × 0.08) ≈ 12732` 微步。

逆运动学（O 型，`vy` 前进、`vx` 右移、`ω` 逆时针为正）：

```
RR = vy + vx + k·ω      RF = vy - vx + k·ω
LF = vy + vx - k·ω      LR = vy - vx - k·ω      k = (轴距 + 轮距) / 2
```

自动比赛的机械/视觉标定不在 `app_config.h`，而在网页比赛卡片或
`/api/competition/config` 中设置；默认值只是占位，必须按实车实测。
开环升降约 **1 微步 ≈ 0.064mm**（3200 微步/圈，丝杆导程需按实车确认；
整步标定的 Z 值 ×16 即为微步值）。

---

## 6. 编译与烧录

```bash
# 1. 进入 ESP-IDF 环境（Windows PowerShell）
#    或使用 VS Code ESP-IDF 插件终端

# 2. 目标芯片（本工程已配置为 esp32s3，可跳过）
idf.py set-target esp32s3

# 3. 编译
idf.py build

# 4. 烧录 + 串口监视（按实际端口修改）
idf.py -p COM18 flash monitor
```

退出监视：`Ctrl + ]`。

> **注意事项**
> - 本工程使用 `driver/i2c_master.h`（新 I2C 驱动），要求 **IDF ≥ 5.2**；
>   IDF 6.x 已移除旧 `driver/i2c.h`，本工程无需修改。
> - `sdkconfig` 已修改为 **FreeRTOS 1000Hz**、**1500K app 分区**（2MB Flash），
>   对应的 `sdkconfig.defaults` 也已提供，删除 sdkconfig 重新生成时不会丢失。
> - 控制台已切到 **USB Serial/JTAG**（UART0 排针留给 K230 链路），
>   若 `idf.py monitor` 看不到日志，请确认使用的是开发板原生 USB 口。
> - 若 Flash 实际是 4/8MB，可在 `idf.py menuconfig → Serial flasher config` 调大。

---

## 7. Web 调试台使用

1. 上电后串口会打印 Wi-Fi 连接过程与 IP，例如：
   `I (xxxx) wifi: connected! open http://192.168.1.23/`
2. 手机/电脑连同一个路由器，浏览器打开该 IP。
3. **连接失败时**：串口会打印断开原因（如 `NO_AP_FOUND`、`4WAY_HANDSHAKE_TIMEOUT`），
   连续失败 5 次会自动扫描并列出周围 2.4GHz AP（SSID/RSSI/信道），便于判断是
   SSID 写错、路由器只开 5GHz 还是信号太弱。
4. **兜底热点**：固件始终开启 AP `mecanum-car`（密码 `mecanum123`）。
   即使路由器连不上，也可以手机直连该热点，打开 **http://192.168.4.1/** 调试。
5. 页面每 **50ms** 轮询 `/api/status`，实时显示：
   - 8 路电压条、阈值线、黑白判定
   - 底盘/升降 EN 状态、四种轮速、采样序号、升降位置 `zpos`
   - 舵机当前角度、Wi-Fi 状态
6. 比赛卡片每 **200ms** 轮询 `/api/competition`，显示状态机、已装/已放、
   Z 与盘位零点有效性、K230 在线/模式、视觉坐标与置信度。
7. 识别到物料时，比赛卡片会用文字提示**该往哪边移动多少**，例如
   `物料：红色 · 需向右 1.2 cm、向前 0.6 cm · 偏差 40,-20 px`；
   进入容差后显示 `已对准，可以抓取`。像素→毫米比例由
   `app_config.h` 的 `VISION_MM_PER_PX`（默认 0.30 mm/px）标定，
   方向正负由比赛配置里的 `vision_x_sign/vision_y_sign` 决定。

操作说明：

- **方向键**：按住持续运动，松开立刻停止；中间 ■ 为急停。
  方向键只决定方向，实际轮速由 **底盘航速** 滑块（默认 2400 微步/秒 = 150 整步/秒）决定。
- **底盘航速 / 升降速度**：两个实时滑块，底盘 800~4800、升降 1600~10000 微步/秒，
  手动遥控、循迹基础航速、M5 升降同时生效。界面同时显示微步与整步换算值。
- **手动 / PID 循迹**：一键切换模式。
- **底盘 EN / 升降 EN**：独立开关，关闭即释放力矩；自动比赛要求两路都打开。
- **升降点动**：按住上升/下降，松开停止；只取符号，幅值由升降速度决定。
- **舵机**：拖动滑块时按 30ms 节流实时下发；电脑端鼠标滚轮可按 1° 微调；
  两侧 `−1° / +1°` 微调，右侧"中位"一键回中。
- **CH0 底座（MG996R 180° 位置舵机）**：直接写绝对角度 **0~180°** 并保持；
  `bstow/bpick/btray/bplace` 就是真实角度，`bspeed/bsweep` 已不再生效。
  `bms` 是到位后的额外稳定等待（建议 300~800ms）。
- **CH8 夹爪（连续舵机）**：不能写角度保持，用“速度角 + 通电时间”开合：
  `gopen/gclose` 是张开/夹紧方向的速度角，`gopenms/gclosems` 是通电毫秒数；
  到点自动回中位停止。建议夹紧时间顶到机械限位后再多 150~250ms。
- **步进控制**：设步进角（默认 15°），用 `CH0 −步进/＋步进` 一次走一个绝对角度。
- **CH4 物料盘（MG90S 连续）**：3 槽，每格 **120°**；`tms` 填每格通电毫秒数，
  不能直接写角度。
- **360° 点动**（仅 CH4）：设速度角（<90 正转，>90 反转）与持续时间，
  到点自动停止；CH4 停止中位可校准到 85~95。
- **阈值 4.00~5.00V（步进 0.01V）/ 极性 / PID / 加速度**：实时生效。
- **参数保存**：PID、阈值、极性、探头顺序、加速度、底盘/升降速度、舵机协议与
  中位校准修改后约 **2 秒自动写入 NVS**；也可点 PID 卡片里的“立即保存到 Flash”。
  **机械臂/舵机标定**（底座各动作角、夹爪开/合、每一步 Z 高度、速度角/行程、
  横线参数）在“保存并确认本次标定”时整体写入 Flash，上电自动恢复。
- **示教保存**：手动把底座/夹爪/升降摆到目标位置后，在比赛卡片选择“示教步骤”
  并点 **“保存当前姿态到该步骤”**，程序读取当前角度/Z 步数写入对应动作并立即
  存 Flash；配合“执行抓取/放置”可逐步调整每一段动作。

### 7.1 比赛卡片使用顺序

1. **机械低速点动**：确认升降、底座、夹爪方向正确，物料盘能正转分度。
2. **标定**：展开"动作、视觉、分度与横线标定"，填写实测 Z 步数、底座角度、
   夹爪速度角/通电时间、视觉基准与容差、盘位速度角/时间、横线检测参数，
   点 **“保存并确认本次标定”**（等价 `/api/competition/config?confirm=1`）。
   底座是 **MG996R 180° 位置舵机**：`bstow/bpick/btray/bplace` 填 **0~180° 的真实角度**，
   用网页滑块或示教保存；`bspeed/bsweep` 字段保留但不再生效。
   夹爪是 **连续舵机**：`gopen/gclose` 是开/合方向速度角（例如 60/120），
   `gopenms/gclosems` 是通电时间（50~3000ms），按实车行程标定。
   物料盘 3 槽：`tms` 填 **每格 120°** 的脉宽时长，3 次分度应回到 slot0；
   停止中位若不在 90°，用 `/servo_center?ch=4&a=xx` 校准。
3. **Z 零点**：把升降停在运输安全位，点 **“设当前 Z 为运输零点”**。
4. **盘位零点**：把物料盘 slot0 对准装卸点并清空，点 **“设当前盘位为 slot0”**。
5. **底座基准**：人工把机械臂底座摆到运输角（`bstow`），启动时程序会把它声明
   为底座逻辑基准。
6. **打开 EN**：底盘与升降 EN 都打开。
7. **启动**：点 **“▶ 启动三取三放”**，状态从 `MANUAL` 进入 `ARM_STOW`。
8. **急停**：任何时候点 **“■ 急停（全部停止）”**（`/api/estop`）——
   立即清空步进脉冲、停止底盘/升降、取消舵机点动、中止比赛并锁存；
   此时所有运动接口返回 423。确认安全后点 **“✔ 解除急停”**（`/api/estop/clear`）
   恢复控制。急停会作废 Z/盘位零点，自动流程需重新执行第 3~5 步。
   “■ 全局中止”仍是平缓停车 + 中止比赛，不锁存。

> 自动比赛运行期间，手动运动/舵机/调速/标定接口会返回 **409 Conflict**；
> `start / abort / z_zero / confirm` 不受此限制。

---

## 8. HTTP API（全部 GET，成功返回 `{"ok":1}`）

| 接口 | 参数 | 说明 |
|---|---|---|
| `/api/status` | — | 完整状态 JSON |
| `/api/move` | `vx` `vy` `w` | 手动方向指令（只决定方向/比例，峰值归一化到底盘航速），非零时自动切手动模式 |
| `/api/mode` | `m=0\|1` | 0 手动，1 循迹 |
| `/api/en` | `chassis=0\|1` `lift=0\|1` | 1 = 锁轴使能 |
| `/api/lift` | `v` | 升降点动，只取符号（正=上升/负=下降），速度由 `lift_speed` 决定 |
| `/api/servo` | `ch` `a` | 位置舵机：CH0 逻辑 0~360°，CH8 0~180°（受标定限位）；CH4 连续舵机请用点动/盘位命令 |
| `/api/line` | `th` `inv` `rev` `kp` `ki` `kd` `om` `accel` | 阈值/极性/探头顺序/PID/转向上限/加速度 |
| `/thresh` | `th`（或 `v`） | 黑白阈值 0.0~5.0V（网页滑块只开放 4.00~5.00V，步进 0.01V） |
| `/speed`（别名 `/api/speed`） | `chassis` `lift` | 底盘 800~4800、升降 1600~10000 微步/秒，立即生效 |
| `/servo_proto` | `mode=0\|1\|2` | 0 单字节角度，1/2 双字节脉宽（小端/大端） |
| `/servo_pulse` | `ch` `speed_angle` `duration_ms` `stop?` | 360° 舵机定时点动，到点回中位 |
| `/servo_center` | `ch` `a` | 校准连续通道停止值（CH0 固定 90°，CH4 可调 85~95） |
| `/servo_stop` | `ch` | 停止连续舵机（取消点动并回中位）；位置舵机无动作 |
| `/api/stop` | — | 平滑停车 + 取消舵机点动 + 中止自动比赛（不锁存） |
| `/api/estop` | — | **急停锁存**：立即停脉冲/底盘/升降/舵机并中止比赛；运动接口返回 423 |
| `/api/estop/clear` | — | 解除急停锁存，恢复手动控制 |
| `/settings` | `save=1` | 立即把 PID/阈值/速度/舵机校准写入 NVS（平时约 2s 自动保存） |
| `/api/competition` | — | 比赛状态 JSON（状态机、零点、K230、视觉、标定） |
| `/api/competition/cmd` | `cmd=start\|abort\|resume\|skip\|execute\|z_zero\|confirm\|tray_zero\|tray_next\|tray_to`（也接受 `action=` 别名）；`tray_to` 另需 `slot=0..5` | `execute`/`resume` 仅在 `DEBUG_PAUSE` 有效 |
| `/api/competition/config` | `zpick ztray zplace zmax bstow bpick btray bplace gopen gclose bms gms lsps mcx mcy rcx rcy tol minconf maxiter xsign ysign nudge asps tspeed tms sblack sconfirm sclear srearm sinterval`，可选 `confirm=1` | 保存标定并可选确认（自动写 Flash） |
| `/api/competition/teach` | `step=bstow\|bpick\|btray\|bplace\|gopen\|gclose\|zpick\|ztray\|zplace` | 把当前底座角度/夹爪角度/升降位置记录到该步骤并写 Flash |

示例：

```bash
curl "http://192.168.1.23/api/move?vy=0.4&vx=0&w=0"
curl "http://192.168.1.23/thresh?th=4.00"
curl "http://192.168.1.23/speed?chassis=450&lift=400"
curl "http://192.168.1.23/api/line?kp=1.2&kd=0.05"
curl "http://192.168.1.23/api/servo?ch=4&a=135"
curl "http://192.168.1.23/servo_pulse?ch=4&speed_angle=60&duration_ms=500"
curl "http://192.168.1.23/api/competition/config?confirm=1&nudge=3&tol=8"
curl "http://192.168.1.23/api/competition/cmd?cmd=z_zero"
curl "http://192.168.1.23/api/competition/cmd?cmd=start"
curl "http://192.168.1.23/api/stop"
```

---

## 9. 调试建议

1. **先确认方向**：抬空车，在手动模式逐个方向测试；若某个轮子反向，
   修改 `app_config.h` 的 `dir_invert` 或调用 `stepper_set_dir_invert()`。
2. **确认传感器极性**：观察 8 路电压条，黑线压在探头上时电压升高则勾选
   “黑线为高电平”，否则保持默认。
3. **确认探头顺序**：若实际安装是 8 号在最左，勾选“探头安装顺序反转”，
   否则循迹会朝反方向纠偏。
4. **阈值**：滑块覆盖 **4.00~5.00V**（步进 0.01V，默认 4.00V），放在黑白电压中点。
   若默认 4.00V 下 8 路几乎全判为黑线，说明探头白底电压低于 4V，
   请勾选"黑线为高电平"或换用合适的探头供电。ADS1115 始终是 ±6.144V 量程。
5. **循迹调参**：默认 `Kp=0.60, Kd=0.15, ω≤1.5 rad/s`，且转向有 6 rad/s² 斜率限制。
   若仍过冲，先降 `Kp` 或 `ω` 上限；若纠偏太慢，再小幅加 `Kp`/`Kd`。
   底盘响应不够时提高加速度（默认 19200 微步/秒²）。
6. **工位横线检测**：先用网页观察 8 路黑白判定，确认整条横线能让至少
   `sblack` 路（默认 6）同时变黑，且左右半区都有黑探头；再调 `sconfirm`、
   `srearm`、`sinterval`，避免普通弯道误触发。
7. **视觉对位**：先让 K230 屏幕显示稳定颜色与坐标，再调基准 `mcx/mcy`、`rcx/rcy`；
   `align_nudge_mm` 不要小于 1mm（否则四舍五入成 0 步直接判失败），
   `xsign/ysign` 用于修正装车方向。
8. **比赛前**：确认 K230 `DEBUG_UART_LOG=False`、两路 EN 打开、所有执行器静止、
   Z 与盘位零点有效、标定已确认；启动后不要再用手机发手动指令。
9. **发热**：不运动时关闭两路 EN，DRV8825 会释放力矩。
