# 工创赛麦克纳姆轮小车 · ESP32-S3 底层驱动 + Web 调试台

基于 **ESP-IDF v5.2+ / v6.x** 与 FreeRTOS 的高实时性底盘与机械臂控制系统。

> 本工程已在本机 **ESP-IDF v6.1.0** 上完成编译验证：**0 warning / 0 error**，
> 固件 0xDB3C0 ≈ 877 KiB，2MB Flash 的 1500K app 分区剩余 42%。

---

## 1. 目录结构

```
helloworld/
├── CMakeLists.txt              # 顶层工程
├── sdkconfig.defaults          # FreeRTOS 1000Hz / 1.5MB app 分区等默认配置
├── README.md
├── docs/DRIVER_API.md          # ★ 驱动接口手册（二次开发 / 其他 agent 调用）
└── main/
    ├── CMakeLists.txt          # 组件源文件与依赖
    ├── main.c                  # 启动流程、任务初始化、健康检查
    ├── app_config.h            # ★ 全部引脚 / I2C 地址 / 机械参数
    ├── stepper.h / .c          # 5 轴 DDS 脉冲发生器（20kHz GPTimer ISR + 1kHz 梯形斜坡）
    ├── mecanum.h / .c          # O 型麦轮逆运动学
    ├── i2c_bus.h / .c          # I2C 主总线 + TCA9548A 复位/选通道/互斥锁
    ├── ads1115.h / .c          # 双 ADS1115 乒乓流水采样（Core 1）
    ├── lu9685.h / .c           # LU9685 单字节协议舵机驱动
    ├── line_follow.h / .c      # 8 路模拟量加权 PID
    ├── chassis.h / .c          # 手动 / 循迹双模式、100Hz 控制环、看门狗
    ├── wifi_sta.h / .c         # Wi-Fi STA + 断线重连 + 关闭省电
    ├── web_server.h / .c       # HTTP API
    └── web_page.h              # 内嵌单页 HTML/JS 控制台
```

---

## 2. 硬件接线（与需求逐项一致）

### 2.1 步进电机（5 × DRV8825，整步）

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

### 2.2 I2C 与传感器

| 器件 | 连接 |
|---|---|
| I2C 主总线 | SDA = GPIO 8，SCL = GPIO 9，400 kHz |
| TCA9548A | 地址 0x70，RESET = GPIO 40（初始化拉低 20ms 再拉高） |
| CH0 | ADS1115 @0x48，A0~A3 → 探头 **5/6/7/8** |
| CH1 | ADS1115 @0x49，A0~A3 → 探头 **1/2/3/4** |
| CH2 | LU9685 @0x40 或 0x00（自动探测） |

| 舵机 | LU9685 通道 |
|---|---|
| 底座旋转（20kg） | CH0 |
| 物料盘（SG90） | CH4 |
| 夹爪（20kg） | CH8 |

**LU9685 协议**：默认单字节角度 `[ch, 0~180]`。若你的模块固件使用脉宽寄存器，
可运行时切到双字节 `[ch, pulse_lo, pulse_hi]`（500~2500us）或大端版本，
在网页"协议模式"下拉框或 `/servo_proto?mode=0|1|2` 切换。

---

## 3. 实时性设计

### 3.1 20 kHz DDS 脉冲（`stepper.c`）

- GPTimer 1 MHz 计数、50us 报警周期，ISR 中用 32 位相位累加器同时产生 5 路 STEP。
- STEP 高电平固定 **50us（≥3us）**，相邻脉冲之间至少 50us 低电平，单轴最高 8 kHz。
- 相位累加每拍无条件执行，保证步频精度；速度 → 相位增量在 1 kHz 任务中换算。
- 梯形加减速：
  - 目标非零且当前速度为 0 时，**直接跳到 200 步/秒**，跨越低速共振区；
  - 默认加速度 **3000 步/秒²**，可运行时调整（Web 滑块或 `stepper_set_accel()`）；
  - **换向必须先减速到 0**，再切换 DIR，避免带速换向丢步。

### 3.2 ADS1115 乒乓流水（`ads1115.c`）

- 新 I2C 主驱动，所有事务 **20ms 超时**，总线级互斥锁防止通道错切。
- 860 SPS、±6.144V（PGA=2/3）、单次转换；单次转换 1.163ms，等待 1.25ms。
- 两块 ADS **并行转换**：触发 A 板 → 触发 B 板 → 读 A → 触发 A 下一路 → 读 B → …
- 8 路完整刷新约 **6~8ms**，刷新周期 12ms（约 83Hz），彻底消除秒级滞后。
- 单次总线错误沿用上一拍缓存值，不会冻结整帧数据。

### 3.3 任务/核心分配

| 核心 | 任务 | 周期/优先级 |
|---|---|---|
| Core 0 | GPTimer 脉冲 ISR | 50us / 中断 |
| Core 0 | 步进速度斜坡 | 1kHz / prio 12 |
| Core 0 | Wi-Fi、HTTP 服务 | 事件驱动 |
| Core 1 | ADS1115 采样 | 12ms / prio 4 |
| Core 1 | 底盘控制环 | 10ms / prio 6 |

采样任务优先级低于控制环，等待转换的自旋不会阻塞 100Hz 控制。

### 3.4 安全机制

- Web 遥控指令 **400ms 看门狗**：浏览器断开或松键后自动停车。
- 升降点动 **500ms 看门狗**。
- EN 关闭时底盘/升降目标强制清零。
- 循迹丢线：保持 150ms 上次转向，超时立即停车。
- 360° 舵机点动由后台任务计时，到点自动回中位；浏览器关闭也不会持续转。
- 停车为梯形减速，急停可调用 `stepper_abort_all()`。

---

## 4. 机械参数（必须按实车修改）

在 `main/app_config.h` 中：

```c
#define WHEEL_DIAMETER_MM       65.0f    /* 轮径 */
#define WHEEL_STEPS_PER_REV     200      /* 1.8° 电机整步 */
#define WHEELBASE_MM            160.0f   /* 前后轴距 */
#define TRACK_MM                190.0f   /* 左右轮距 */
```

逆运动学（O 型，`vy` 前进、`vx` 右移、`ω` 逆时针为正）：

```
RR = vy + vx + k·ω      RF = vy - vx + k·ω
LF = vy + vx - k·ω      LR = vy - vx - k·ω      k = (轴距 + 轮距) / 2
```

---

## 5. 编译与烧录

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
> - 若 Flash 实际是 4/8MB，可在 `idf.py menuconfig → Serial flasher config` 调大。

---

## 6. Web 调试台使用

1. 上电后串口会打印 Wi-Fi 连接过程与 IP，例如：
   `I (xxxx) wifi: connected! open http://192.168.1.23/`
2. 手机/电脑连同一个路由器，浏览器打开该 IP。
3. 页面每 **50ms** 轮询 `/api/status`，实时显示：
   - 8 路电压条、阈值线、黑白判定
   - 底盘/升降 EN 状态、四种轮速、采样序号
   - 舵机当前角度、Wi-Fi 状态

操作说明：

- **方向键**：按住持续运动，松开立刻停止；中间 ■ 为急停。
  方向键只决定方向，实际轮速由 **底盘航速** 滑块（默认 450 步/秒）决定。
- **底盘航速 / 升降速度**：两个实时滑块，100~1500 / 100~1200 步/秒，
  手动遥控、循迹基础航速、M5 升降同时生效。
- **手动 / PID 循迹**：一键切换模式。
- **底盘 EN / 升降 EN**：独立开关，关闭即释放力矩。
- **升降点动**：按住上升/下降，松开停止。
- **舵机**：拖动滑块时按 30ms 节流实时下发；电脑端鼠标滚轮可按 1° 微调；
  两侧 `−1° / +1°` 微调，右侧"中位"一键回中。物料盘另有 5 个工位快捷按钮。
- **360° 连续舵机**：选择通道 → 设速度角（<90 正转，>90 反转）→ 设持续时间，
  点"点动"后到点自动停止；中位停止值可校准到 85~95。
- **阈值 0~5.0V / 极性 / PID / 加速度**：实时生效。

---

## 7. HTTP API（全部 GET，返回 `{"ok":1}`）

| 接口 | 参数 | 说明 |
|---|---|---|
| `/api/status` | — | 完整状态 JSON |
| `/api/move` | `vx` `vy` `w` | 手动速度（m/s, m/s, rad/s），非零时自动切手动模式 |
| `/api/mode` | `m=0\|1` | 0 手动，1 循迹 |
| `/api/en` | `chassis=0\|1` `lift=0\|1` | 1 = 锁轴使能 |
| `/api/lift` | `v` | 升降点动，只取符号（正=上升/负=下降），速度由 `lift_speed` 决定 |
| `/api/servo` | `ch` `a` | LU9685 通道与角度 0~180（按当前协议） |
| `/api/line` | `th` `inv` `rev` `kp` `ki` `kd` `om` `accel` | 阈值/极性/探头顺序/PID/转向上限/加速度 |
| `/thresh` | `th`（或 `v`） | 黑白阈值 0.0~5.0V |
| `/speed` | `chassis` `lift` | 基础转速 100~1500 / 100~1200 步/秒，立即生效 |
| `/servo_proto` | `mode=0\|1\|2` | 0 单字节角度，1/2 双字节脉宽（小端/大端） |
| `/servo_pulse` | `ch` `speed_angle` `duration_ms` `stop?` | 360° 舵机定时点动，到点回中位 |
| `/servo_center` | `ch` `a` | 校准该通道中位停止值（建议 85~95） |
| `/api/stop` | — | 平滑停车 |

示例：

```bash
curl "http://192.168.1.23/api/move?vy=0.4&vx=0&w=0"
curl "http://192.168.1.23/thresh?th=4.00"
curl "http://192.168.1.23/speed?chassis=450&lift=400"
curl "http://192.168.1.23/api/line?kp=1.2&kd=0.05"
curl "http://192.168.1.23/api/servo?ch=4&a=135"
curl "http://192.168.1.23/servo_pulse?ch=4&speed_angle=60&duration_ms=500"
```

---

## 8. 调试建议

1. **先确认方向**：抬空车，在手动模式逐个方向测试；若某个轮子反向，
   修改 `app_config.h` 的 `dir_invert` 或调用 `stepper_set_dir_invert()`。
2. **确认传感器极性**：观察 8 路电压条，黑线压在探头上时电压升高则勾选
   “黑线为高电平”，否则保持默认。
3. **确认探头顺序**：若实际安装是 8 号在最左，勾选“探头安装顺序反转”，
   否则循迹会朝反方向纠偏。
4. **阈值**：滑块覆盖 **0.0~5.0V**（默认 4.00V），放在黑白电压中点即可。
   若默认 4.00V 下 8 路几乎全判为黑线，说明你的探头白底电压低于 4V，
   请降低阈值或勾选"黑线为高电平"。ADS1115 始终是 ±6.144V 量程，无 3.3V 截断。
5. **循迹调参**：先 `Kd=0, Ki=0`，从小到大加 `Kp` 直到轻微摆动，再加 `Kd` 抑制；
   底盘响应不够时提高加速度（默认 3000 步/秒²）。
6. **发热**：不运动时关闭两路 EN，DRV8825 会释放力矩。
