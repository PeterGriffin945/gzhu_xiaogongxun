# 麦克纳姆轮小车驱动接口手册（供 Agent / 二次开发调用）

> 适用工程：`E:\xiaogongxun_codes\helloworld`（ESP-IDF ≥ 5.2，已在 6.1.0 验证）
> 目标芯片：ESP32-S3　构建系统：CMake + `idf.py build`
> 本文档描述**已实现的公共 API**，不含内部实现细节。所有函数都带 `extern "C"` 保护。
> 视觉端为 K230D / CanMV（MicroPython），通过 UART1 ↔ PORT2 与 ESP32 通信。

---

## 0. 一句话集成规则

| 想做的事 | 调用 |
|---|---|
| 上电初始化 | 按第 2 节顺序调用各 `*_init()` |
| 让车走 | `chassis_set_velocity(vx, vy, w)`，每 <400ms 刷新一次 |
| 让车按 8 路探头巡线 | `chassis_set_mode(CHASSIS_MODE_LINE)` |
| 调车速 | `chassis_set_speeds(chassis_sps, lift_sps)` |
| 升降点动 | `chassis_lift_jog(+1 / -1 / 0)`（只取符号） |
| 升降走到绝对位置 | `chassis_lift_move_to(target_steps, peak_sps)` |
| 开环短距平移对位 | `chassis_start_translation(dx_mm, dy_mm, peak_sps)` |
| 舵机定位 | `lu9685_set_angle(ch, 0~180)` |
| 360° 舵机转固定角度 | `lu9685_pulse_start(ch, speed_angle, duration_ms, -1)` |
| 读探头电压 | `ads1115_get_latest(float v[8], &seq)` |
| 读视觉结果 | `k230_link_result_fresh(mode)` + `k230_link_get_result(&r)` |
| 跑完整三取三放 | 标定 + `competition_set_z_zero()` + `competition_start()` |
| 不用 Web，纯 C 控制 | 只依赖 `chassis.h` / `mecanum.h` / `stepper.h` |

**硬约束**：不要在 ISR 里调用任何 I2C / UART / FreeRTOS 阻塞函数；步进轴速度上限
**10000 微步/秒**（1/16 细分，脉冲率硬上限 10 kHz）；EN 低电平为锁轴；自动比赛期间
手动执行器接口会被 HTTP 层拒绝（409）。

---

## 1. 硬件拓扑（与 `main/app_config.h` 一致，改硬件只改该文件）

### 1.1 步进电机 5 × DRV8825（1/16 细分）

> **单位约定**：DRV8825 的 MS1/MS2/MS3 跳线已设为 1/16 细分（纯硬件，不占 GPIO）。
> 本文所有“步/秒、步数、加速度”均为**微步**单位，是整步数值的 16 倍。
> `app_config.h` 中 `WHEEL_MICROSTEP` / `LIFT_MICROSTEP` 控制缩放；改回整步设为 1 即可。

| 轴枚举 | 位置 | STEP | DIR | DIR 逻辑默认 |
|---|---|---|---|---|
| `AXIS_RR` (0) | 右后 M1 | GPIO3 | GPIO46 | 正 |
| `AXIS_RF` (1) | 右前 M2 | GPIO17 | GPIO18 | 正 |
| `AXIS_LF` (2) | 左前 M3 | GPIO12 | GPIO11 | **取反** |
| `AXIS_LR` (3) | 左后 M4 | GPIO14 | GPIO13 | **取反** |
| `AXIS_LIFT` (4) | 升降 M5 | GPIO15 | GPIO16 | 正 |

| 使能 | 引脚 | 电平 |
|---|---|---|
| 底盘 M1~M4 | GPIO42 | 低 = 锁轴，高 = 释放（上电默认高） |
| 升降 M5 | GPIO41 | 低 = 锁轴，高 = 释放（上电默认高） |

### 1.2 I2C 总线

| 项目 | 值 |
|---|---|
| SDA / SCL | GPIO8 / GPIO9，400 kHz |
| 事务超时 | 20 ms（`I2C_TIMEOUT_MS`） |
| TCA9548A | 0x70，RESET = GPIO40（初始化拉低 20ms 再拉高） |
| CH0 | ADS1115 @0x48，A0~A3 → 探头 **5/6/7/8** |
| CH1 | ADS1115 @0x49，A0~A3 → 探头 **1/2/3/4** |
| CH2 | LU9685 @0x40（探测失败再试 0x00） |

### 1.3 舵机通道

| 用途 | LU9685 通道 | 备注 |
|---|---|---|
| 底座旋转 20kg | CH0 | 位置舵机 |
| 物料盘 SG90 | CH4 | 也可 360° 点动 |
| 夹爪 20kg | CH8 | 位置舵机 |

### 1.4 机械参数（`app_config.h`，必须按实车改）

```c
WHEEL_DIAMETER_MM    65.0f                          // 轮径
WHEEL_MICROSTEP      16                             // DRV8825 细分
WHEEL_STEPS_PER_REV  (200 * WHEEL_MICROSTEP)        // 3200 微步/圈
WHEELBASE_MM         160.0f                         // 轴距
TRACK_MM             190.0f                         // 轮距
```

### 1.5 K230 视觉串口

| 项目 | 值 |
|---|---|
| ESP32 逻辑串口 | UART1（重映射到 UART0 排针 GPIO43/44） |
| ESP32 TX / RX | GPIO43 / GPIO44，115200 8N1 |
| K230 端口 | PORT2（UART2），115200 8N1 |
| ESP32 控制台 | USB Serial/JTAG（`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`） |
| 链路超时 | ESP32 侧 1500ms；K230 侧 2000ms |
| 结果新鲜度 | 400ms |

---

## 2. 初始化顺序（必须遵守）

`app_main()`（`main/main.c`）中已验证的顺序：

```c
nvs_flash_init();            // Wi-Fi 前置
stepper_init();              // ★ 最先：EN 立刻拉高，避免上电锁死发热
i2c_bus_init();              // TCA9548A 复位 + 400kHz 总线
ads1115_init();
ads1115_start();             // 创建 Core1 采样任务
lu9685_init();               // 失败不致命：返回 != ESP_OK 时舵机不可用
line_follow_init();
chassis_init();              // 创建 100Hz 控制任务
k230_link_init();            // 失败只禁用自动比赛，不影响手动
competition_init();          // 内部初始化机械臂/物料盘并安装升降软限位
wifi_sta_init();
web_server_start();          // 可选
```

依赖关系：

- `ads1115_*` / `lu9685_*` 依赖 `i2c_bus_init()`。
- `chassis_*` 依赖 `stepper_init()` 与 `line_follow_init()`；不依赖 Wi-Fi。
- `competition_*` 依赖 `stepper`、`chassis`、`lu9685`、`ads1115`、`k230_link`。
- `web_server_start()` 依赖 `wifi_sta_init()`（只在日志里用 IP）。

---

## 3. 任务 / 中断布局（不要随意改优先级）

| 核 | 执行体 | 周期 | 优先级 |
|---|---|---|---|
| 中断 | `stepper_timer_isr`（GPTimer 1MHz，50µs 报警） | 50 µs | 硬件 3 |
| Core 0 | `step_ramp` 梯形加减速 | 1 ms | 12 |
| Core 0 | `k230_link` UART 收发 | 20 ms | 4 |
| Core 0 | Wi-Fi / `httpd` | 事件驱动 | 默认 |
| Core 1 | `ads_sample` 乒乓采样 | 12 ms | 4 |
| Core 1 | `chassis` 控制环 | 10 ms | 6 |
| Core 1 | `competition` 比赛状态机 | 10 ms | 3 |
| Core 1 | `srv_pulse` 舵机点动计时 | 5 ms | 3 |

控制环优先级高于采样任务，采样等待可被随时抢占。
比赛状态机与舵机点动优先级较低，不会挤占底盘与采样实时性。

---

## 4. 模块 API

### 4.1 `stepper.h` —— 5 轴 DDS 脉冲与开环定步

```c
typedef enum { AXIS_RR=0, AXIS_RF=1, AXIS_LF=2, AXIS_LR=3, AXIS_LIFT=4, AXIS_COUNT=5 } stepper_axis_t;

typedef enum {
    STEPPER_MOVE_IDLE = 0,
    STEPPER_MOVE_RUNNING,
    STEPPER_MOVE_DONE,
    STEPPER_MOVE_CANCELLED,
} stepper_move_status_t;

esp_err_t stepper_init(void);

void  stepper_set_speed(stepper_axis_t axis, float steps_per_sec);
void  stepper_set_chassis_speeds(const float steps_per_sec[4]);   // 顺序 RR/RF/LF/LR
float stepper_get_speed(stepper_axis_t axis);                     // 当前实际速度（含斜坡）
float stepper_get_target(stepper_axis_t axis);

void  stepper_set_accel(float steps_per_sec2);                    // 默认 19200 微步/秒²，范围 1600~100000
// 加减速为 S 形：接近 0 或目标速度时自动降低加速度（accel_scale 钟形系数）
float stepper_get_accel(void);

void  stepper_set_dir_invert(stepper_axis_t axis, bool invert);   // 运行时翻转 DIR 逻辑
bool  stepper_get_dir_invert(stepper_axis_t axis);

void  stepper_set_chassis_enable(bool enable);   // true=锁轴(EN 低)
void  stepper_set_lift_enable(bool enable);
bool  stepper_chassis_enabled(void);
bool  stepper_lift_enabled(void);

/* 开环定步运动 */
esp_err_t stepper_start_move(stepper_axis_t axis, int32_t delta_steps, float peak_sps);
stepper_move_status_t stepper_get_move_status(stepper_axis_t axis);
bool     stepper_move_active(stepper_axis_t axis);
int32_t  stepper_get_move_remaining(stepper_axis_t axis);
int32_t  stepper_get_position(stepper_axis_t axis);
esp_err_t stepper_set_position(stepper_axis_t axis, int32_t position);

void  stepper_stop(stepper_axis_t axis);
void  stepper_stop_all(void);
void  stepper_abort_all(void);
```

行为约定：

- 单位 **微步/秒**，正负表示方向；内部限幅 ±`STEPPER_MAX_SPEED`(10000)。
- 连续调速：目标从 0 变非 0 时，**直接跳到 960 微步/秒**（升降轴同理）
  跨过低速区，再按加速度爬升。
  **换向必须先减速到 0**，下一拍才切换 DIR；调用方无需处理。
- `stepper_start_move()`：`delta_steps` 的符号决定方向、绝对值是微步数；`peak_sps` 必须
  为正，内部限幅到 10000。返回：
  - `ESP_ERR_INVALID_ARG`：`delta_steps == 0`、`peak_sps` 非有限或 ≤0；
  - `ESP_ERR_INVALID_STATE`：该轴已在定步、当前速度 >1 步/秒，或对应 EN 未使能；
  - `ESP_OK`：立即返回，运动在 ISR + 1kHz 斜坡中完成。
- 定步曲线：起步 40 步/秒；剩余脉冲越少，允许速度按 `sqrt(2·accel·remaining)` 降低，
  形成梯形/三角形曲线；最后一个脉冲由 ISR 自己封口并置 `STEPPER_MOVE_DONE`。
- `stepper_get_move_status()`：`IDLE` 未运动 / `RUNNING` / `DONE` / `CANCELLED`
  （平滑停车完成、急停、EN 关闭或改速度导致取消）。
- `stepper_get_position()`：**ISR 实际发出的 STEP 脉冲累计计数**，开环、无机械反馈；
  连续调速和定步运动都会更新它。上电初值 0，需用 `stepper_set_position()` 建立零点。
- `stepper_set_position()`：仅在该轴空闲且速度 ≤1 时生效，否则 `ESP_ERR_INVALID_STATE`；
  成功时把位置写为给定值并把状态置 `IDLE`。
- `stepper_stop()` 对活动定步是“减速到 0 后置 CANCELLED”；`stepper_abort_all()` 是立即
  清空相位/速度/剩余步数并置 CANCELLED。
- `set_chassis_enable(false)` / `set_lift_enable(false)` 会把对应目标速度清 0、取消定步并释放电机。
- 所有 setter 只写 `volatile` 变量，**任意任务可调用**；不要在 ISR 里调用。

### 4.2 `mecanum.h` —— O 型麦轮逆运动学

```c
typedef struct { float vx; float vy; float omega; } chassis_vel_t;
// vx>0 右移 (m/s), vy>0 前进 (m/s), omega>0 逆时针 (rad/s, 俯视)

void    mecanum_inverse(const chassis_vel_t *cmd, float wheel_mps[4]); // RR/RF/LF/LR
int32_t mecanum_mps_to_steps(float mps);     // 已限幅 ±10000 并四舍五入
float   mecanum_steps_to_mps(int32_t steps);
float   mecanum_max_wheel_mps(void);         // ≈ 0.64 m/s（16 细分 + 10kHz 上限）
```

公式（`k = (轴距 + 轮距) / 2`）：

```
RR = vy + vx + k·ω      RF = vy - vx + k·ω
LF = vy + vx - k·ω      LR = vy - vx - k·ω
```

步频换算（1/16 细分）：`steps = mps × 3200 / (π · 0.065)` ≈ `mps × 15670`；
即 1 微步 ≈ 0.064 mm，1 mm ≈ 15.7 微步。

### 4.3 `i2c_bus.h` —— 总线与 TCA9548A

```c
esp_err_t i2c_bus_init(void);
i2c_master_bus_handle_t i2c_bus_handle(void);
esp_err_t tca_select(uint8_t channel);          // 0~7，一次只开通一路
esp_err_t tca_deselect(void);
bool      tca_available(void);
bool      i2c_bus_lock(uint32_t timeout_ms);
void      i2c_bus_unlock(void);
```

**关键规则**：任何“选通道 → 读/写”序列必须包在 `i2c_bus_lock()` / `i2c_bus_unlock()`
之间，否则两个任务交错选通道会把数据读到别的器件上。现有 `lu9685_*` 已内部加锁。

### 4.4 `ads1115.h` —— 8 路模拟量

```c
esp_err_t ads1115_init(void);
esp_err_t ads1115_start(void);                      // Core 1，12ms 周期
bool      ads1115_get_latest(float v[8], uint32_t *seq);  // v[0]=探头1 … v[7]=探头8
uint32_t  ads1115_get_seq(void);
bool      ads1115_is_ready(void);
float     ads1115_get_voltage(int index);           // 0~7
```

- 量程 ±6.144 V（PGA=2/3），860 SPS；返回值单位 **V**，可正可负。
- 两块 ADS 并行转换，8 路全刷新 6~8 ms，周期 12 ms；单次总线错误沿用上一拍缓存。
- `ads1115_get_latest()` 返回 `false` 表示尚无有效数据，此时 `v[]` 保持最后一次值。
- `seq` 每完成一帧 +1，可用于判断数据是否更新。

### 4.5 `lu9685.h` —— 16 路舵机

```c
typedef enum {
    LU9685_PROTO_ANGLE    = 0,   // [ch, 0~180]           默认
    LU9685_PROTO_PULSE_LO = 1,   // [ch, pulse_lo, pulse_hi]  500~2500us
    LU9685_PROTO_PULSE_HI = 2,   // [ch, pulse_hi, pulse_lo]
} lu9685_proto_t;

esp_err_t lu9685_init(void);
bool      lu9685_available(void);
bool      lu9685_pulse_ready(void);          // 点动后台任务是否可用
uint8_t   lu9685_address(void);

esp_err_t lu9685_set_protocol(uint8_t mode);   // 0/1/2，立即生效
uint8_t   lu9685_get_protocol(void);

esp_err_t lu9685_set_angle(uint8_t channel, uint8_t angle);   // 0~180，超限自动限幅
uint8_t   lu9685_get_angle(uint8_t channel);                  // 缓存值，默认 90

esp_err_t lu9685_set_center(uint8_t channel, uint8_t angle);  // 360° 舵机停止点，建议 85~95
uint8_t   lu9685_get_center(uint8_t channel);

esp_err_t lu9685_pulse_start(uint8_t channel, uint8_t speed_angle,
                             uint32_t duration_ms, int stop_angle);  // stop<0 = 用中位
esp_err_t lu9685_pulse_cancel(uint8_t channel);
bool      lu9685_pulse_active(uint8_t channel);
bool      lu9685_pulse_faulted(uint8_t channel);
void      lu9685_pulse_stop_all(void);

// 360° 连续舵机的“定时定位”：按逻辑角度差换算转动时间，到点自动停
esp_err_t lu9685_move_timed(uint8_t channel, uint8_t target_angle,
                            uint8_t speed_angle, uint32_t ms_per_180,
                            int stop_angle, uint32_t *duration_ms_out);
uint8_t   lu9685_get_target_angle(uint8_t channel);   // 逻辑角度，默认 90
esp_err_t lu9685_set_target_angle(uint8_t channel, uint8_t angle);  // 仅声明姿态，不转动
```

- 板子离线时：`lu9685_available()==false`，`set_angle` 直接返回
  `ESP_ERR_INVALID_STATE`，**不会更新缓存**。
- `pulse_start` 先发 `speed_angle`，**后台任务**在 `duration_ms` 后自动发停止角；
  同一通道再次调用会重新计时；`duration_ms` 内部限幅 60000ms。
- `pulse_cancel` 立即清除计时并写回中位；`pulse_active` 表示计时器仍在等待；
  `pulse_faulted` 表示停止角连续写失败 3 次（粘滞故障，`pulse_start` 才会清除）。
- 360° 连续舵机判定：`90` 附近停转，偏离越远越快。用 `pulse_start(ch, 60, 500, -1)`
  即是“正转约半秒”。
- **`move_timed` 是连续舵机的角度控制入口**：内部维护每通道逻辑角度（默认 90），
  `duration = |target - current| / 180 × ms_per_180`，正转用 `speed_angle`、
  反转用 `180 - speed_angle`，到点自动写回中位；`speed_angle` 必须 1~179。
  底座 CH0 的 `speed_angle` / `ms_per_180` 来自比赛标定（`bspeed` / `bsweep`）。
- 手动点动（`pulse_start`）不更新逻辑角度；对 CH0 使用后逻辑基准会偏移，
  开赛时 `competition_start()` 会用 `set_target_angle(CH0, bstow)` 重新声明基准，
  因此启动前需人工把底座摆到运输角。

### 4.6 `line_follow.h` —— 8 路加权 PID

```c
typedef struct {
    float kp;            // 默认 0.60
    float ki;            // 默认 0.00
    float kd;            // 默认 0.15
    float threshold;     // 黑白阈值 V，默认 4.00；后端限幅 0.0~5.0
                         // 网页滑块仅开放 4.00~5.00（步进 0.01）用于精细标定
    bool  black_high;    // true = 电压高于阈值为黑线（极性反转）
    bool  reverse;       // true = 探头实际安装为 8→1，位置权重取反
    float base_speed;    // 旧字段 m/s；底盘已改用 chassis_base_speed
    float max_omega;     // 修正角速度上限 rad/s，默认 1.5
                         // 输出还受 6 rad/s² 的转向斜率限制，防止过冲
} line_params_t;

void  line_follow_init(void);
void  line_follow_get_params(line_params_t *out);
void  line_follow_set_params(const line_params_t *in);   // threshold 限幅 0.0~5.0
bool  line_follow_is_black(float voltage);
void  line_follow_reset(void);
float line_follow_update(const float v[8], float dt, bool *line_seen, float *error_out);
float line_follow_get_error(void);
```

语义：

- 探头位置 `-3.5 … +3.5`，误差 >0 表示黑线偏右，输出 `omega = -PID(error)`（顺时针修正）。
- 丢线时**不停车**：返回上一次的 ω 并保持基础航速继续前进。
  `line_seen` 与页面 8 路黑白判定使用同一规则：**任一路判黑即为 true**，
  仅作界面提示，不触发停车。
- 权重跨度按 `max(0.5V, threshold/4)` 归一化，避免高阈值（4~5V）时权重被压得
  过小、界面显示黑线但 PID 认为丢线。
- 积分限幅 ±5。`line_follow_update` 由 `chassis` 任务在 100 Hz 中调用，**不要在其他任务重复调用**。
- `line_follow_set_params` 会对非有限值直接忽略，并对阈值/ω 上限限幅。

### 4.7 `chassis.h` —— 底盘控制（推荐的主要入口）

```c
typedef enum {
    CHASSIS_MODE_MANUAL = 0,
    CHASSIS_MODE_LINE = 1,
    CHASSIS_MODE_POSITION = 2,
} chassis_mode_t;

typedef struct {
    chassis_mode_t mode;
    float vx, vy, omega;          // 当前生效指令
    float wheel_mps[4];           // RR/RF/LF/LR 轮速 m/s
    int32_t wheel_steps[4];       // RR/RF/LF/LR 步频
    float line_error;
    bool  line_seen;
    float lift_sps;               // 当前升降速度（带符号）
    uint32_t sensor_seq;
} chassis_status_t;

esp_err_t     chassis_init(void);
void          chassis_set_mode(chassis_mode_t mode);
chassis_mode_t chassis_get_mode(void);

void chassis_set_velocity(float vx, float vy, float omega);  // ← 必须是"方向"语义，见下
void chassis_lift_jog(float steps_per_sec);                  // 只取符号，幅值 = lift_speed

esp_err_t chassis_lift_move_to(int32_t target_position, float peak_sps);
bool      chassis_lift_move_active(void);
bool      chassis_lift_idle(void);
void      chassis_set_lift_limits(int32_t min_position, int32_t max_position);
void      chassis_invalidate_lift_limits(void);

esp_err_t chassis_start_translation(float dx_mm, float dy_mm, float peak_sps);
bool      chassis_translation_active(void);
bool      chassis_translation_done(void);

void chassis_set_speeds(float chassis_sps, float lift_sps);  // 1600~10000 / 1600~10000 微步/秒
void chassis_get_speeds(float *chassis_sps, float *lift_sps);
void chassis_stop(void);
void chassis_get_status(chassis_status_t *out);
```

**`chassis_set_velocity` 的重要语义（当前版本）**：
`vx / vy / omega` 只决定**方向与各轮比例**，四轮步频的峰值会被归一化到
`chassis_base_speed`（`chassis_set_speeds` 的第一个参数，默认 4800 微步/秒）。
因此 `(0, 0.45, 0)` 与 `(0, 1, 0)` 都是“全速前进”，`(0, 0, 0)` 才是停车。
如果希望按 m/s 精确控制，请直接调用 `mecanum_*` + `stepper_set_chassis_speeds()`。

**模式**：

- `CHASSIS_MODE_MANUAL`：使用 `chassis_set_velocity` 的方向指令，400ms 未刷新自动停车。
- `CHASSIS_MODE_LINE`：控制环读 ADS，以 `chassis_base_speed` 为基础前进速度，叠加 PID
  转向；丢线时保持上次转向继续前进，不减速、不停车。
- `CHASSIS_MODE_POSITION`：控制环**不再下发轮速**，四轮由
  `chassis_start_translation()` 启动的开环定步运动接管；此时 `chassis_set_velocity`
  会被忽略。一般不要手工切到该模式，除非你自己调用 `stepper_start_move()`。

**升降**：

- `chassis_lift_jog(v)`：只取符号，幅值用 `lift_speed`；500ms 未刷新自动停止；
  若已安装软限位且已经到达边界，命令会被清 0。
- `chassis_lift_move_to(target, peak_sps)`：按**绝对位置**（步）移动 `AXIS_LIFT`，
  `peak_sps > 0`。若软限位有效且目标越界返回 `ESP_ERR_INVALID_ARG`；目标等于当前位置时，
  空闲则返回 `ESP_OK`，否则 `ESP_ERR_INVALID_STATE`；调用会取消升降点动。
  该函数立即返回，运动由定步机制完成。
- `chassis_lift_move_active()`：升降定步是否仍在运行。
- `chassis_lift_idle()`：无点动命令、无定步、目标速度 0、实际速度 0，四者同时成立。
- `chassis_set_lift_limits(min, max)`：设置升降软限位；`min > max` 视为无效。
  注意它**不会**拒绝当前位置在区间外，只会阻止点动继续越界、拒绝越界的 `move_to`。
- `chassis_invalidate_lift_limits()`：清除软限位。
  自动比赛在应用标定时会安装 `[0, z_max]`。

**短距平移对位**：

- `chassis_start_translation(dx_mm, dy_mm, peak_sps)`：`dx` 向右、`dy` 向前，单位 mm；
  通过麦轮逆运动学换算成四轮步数，按距离比例分配峰值步频（`peak_sps` 对应最大轮）。
- 要求底盘 EN 已使能、当前无平移、四轮实际速度 ≤1 步/秒，否则
  `ESP_ERR_INVALID_STATE`；`dx/dy` 都 <0.01mm 或 `peak_sps<=0` 返回 `ESP_ERR_INVALID_ARG`。
- 成功后模式切到 `CHASSIS_MODE_POSITION`，立即返回。
- `chassis_translation_active()`：仍有轮在定步运动。
- `chassis_translation_done()`：只在该函数启动过一次平移、当前无轮运动、且所有参与轮
  状态均为 `STEPPER_MOVE_DONE` 时返回 true。完成后建议调用 `chassis_stop()` 退出位置模式。
- 平移是**开环**的，没有编码器/IMU 反馈；视觉对位依赖 K230 反复测量。

**安全看门狗**：

- 手动模式：`chassis_set_velocity` 后 **400 ms** 未刷新 → 自动停车。
- 升降点动：`chassis_lift_jog` 后 **500 ms** 未刷新 → 自动停止。
- 对应 EN 关闭时，该侧目标强制为 0，并取消定步运动。

### 4.8 `k230_link.h` —— K230 视觉链路

```c
typedef struct {
    vision_mode_t mode;
    vision_color_t color;
    bool detected;
    bool stable;
    uint16_t cx;
    uint16_t cy;
    uint16_t area;
    uint8_t confidence;
    uint8_t seq;
    int64_t received_ms;
} vision_result_t;

typedef struct {
    vision_mode_t requested_mode;
    vision_mode_t ack_mode;
    uint8_t last_command_seq;
    uint8_t last_ack_command_seq;
    uint8_t last_rx_seq;
    int64_t last_rx_ms;
    uint32_t valid_frames;
    uint32_t crc_errors;
    bool driver_ready;
} k230_link_status_t;

esp_err_t k230_link_init(void);
esp_err_t k230_link_set_mode(vision_mode_t mode);
bool      k230_link_online(void);
bool      k230_link_result_fresh(vision_mode_t mode);
void      k230_link_get_result(vision_result_t *out);
void      k230_link_get_status(k230_link_status_t *out);
```

`vision_mode_t`：`VISION_MODE_IDLE=0, MATERIAL=1, RING=2, TRAY=3`。
`vision_color_t`：`UNKNOWN=0, RED=1, GREEN=2, BLUE=3`。

- `k230_link_init()`：配置 UART1、GPIO43/44、115200 8N1、2KB RX 缓冲，创建 Core0
  20ms 任务，并主动发送一次 `IDLE` 命令。
- `k230_link_set_mode()`：发送 `COMMAND`（1 字节模式）。**ACK 重试**：最多 2 轮，
  每轮等待 12×10ms 看是否收到 `command_seq + current_mode` 匹配的 ACK；两轮都失败时
  仍返回 `ESP_OK`（命令已发出，交给 12s 识别超时处理），只有发送失败才返回错误。
  由于内部有 `vTaskDelay`，最坏阻塞约 240ms，**只应在状态切换时调用，不要放在
  10ms 周期循环里**。
- `k230_link_online()`：驱动就绪、收到过帧、且最近一帧在 **1500ms** 内。
- `k230_link_result_fresh(mode)`：最近结果存在、模式匹配、且年龄 ≤ **400ms**。
- `k230_link_get_result()` / `get_status()`：短暂关中断拷贝一份快照，可在任意任务调用。
- `vision_result_t.received_ms` 为 ESP32 本地毫秒时间戳（`esp_timer`），用于判断结果
  是否晚于某次动作。
- DETECTION 解析时要求 payload 恰好 11 字节；`detected==false` 时会把 `stable` 强制为
  false、`color` 强制为 unknown。`crc_errors` 字段已保留，当前实现只在解析失败时丢弃
  坏帧、不做累计上报。

### 4.9 `station_detector.h` —— 工位横线检测

```c
typedef struct {
    uint8_t min_black;          // 默认 6，范围 2~8
    uint8_t confirm_frames;     // 默认 2，范围 1~10
    uint8_t clear_max_black;    // 默认 3，必须 < min_black
    uint8_t rearm_frames;       // 默认 3，范围 1~20
    uint32_t min_interval_ms;   // 默认 800，≤10000
} station_detector_config_t;

typedef struct {
    station_detector_config_t config;
    uint32_t last_seq;
    uint8_t candidate_count;
    uint8_t clear_count;
    bool armed;
    int64_t last_trigger_ms;
} station_detector_t;

void station_detector_init(station_detector_t *detector);
void station_detector_reset(station_detector_t *detector, int64_t now_ms);
bool station_detector_update(station_detector_t *detector, uint8_t black_mask,
                             uint32_t seq, int64_t now_ms);
```

语义：

- `black_mask` bit0..bit7 对应探头 1..8。
- 每个**新的 ADS 帧**调用一次（`seq` 未变化时直接返回 false）。
- 触发条件：黑探头数 ≥ `min_black`，且**左右半区都有黑探头**
  （`mask & 0x0F` 与 `mask & 0xF0` 均非 0），并连续保持 `confirm_frames` 帧；
  同时距上次触发 ≥ `min_interval_ms`。满足时返回 true 一次（上升沿）。
- 重新武装：未武装时，只有看到“存在但不宽”的正常线（`0 < 黑数 ≤ clear_max_black`）
  连续 `rearm_frames` 帧才重新 armed；完全丢线不算离站。
- `station_detector_reset()` 会把 `last_trigger_ms` 回拨一个 `min_interval_ms`，
  使新一段行程开头没有冷却盲区。

### 4.10 `tray_controller.h` —— 6 工位物料盘

```c
#define TRAY_SLOT_COUNT 6

typedef enum {
    TRAY_MOTION_IDLE = 0,
    TRAY_MOTION_RUNNING,
    TRAY_MOTION_DONE,
    TRAY_MOTION_ERROR,
} tray_motion_status_t;

typedef struct {
    bool zero_valid;
    uint8_t current_slot;
    uint8_t target_slot;
    uint8_t speed_angle;
    uint32_t index_duration_ms;
    tray_motion_status_t motion;
    vision_color_t colors[TRAY_SLOT_COUNT];
    esp_err_t last_error;
} tray_status_t;

void      tray_controller_init(void);
esp_err_t tray_controller_set_zero(void);
void      tray_controller_invalidate(void);
esp_err_t tray_controller_configure(uint8_t speed_angle, uint32_t index_duration_ms);
esp_err_t tray_controller_start_next(void);
esp_err_t tray_controller_start_to(uint8_t target_slot);
void      tray_controller_update(void);
void      tray_controller_abort(void);
bool      tray_controller_busy(void);
void      tray_controller_get_status(tray_status_t *out);
esp_err_t tray_controller_mark_loaded(uint8_t slot, vision_color_t color);
esp_err_t tray_controller_mark_empty(uint8_t slot);
int       tray_controller_find_color(vision_color_t color);
uint8_t   tray_forward_steps(uint8_t current_slot, uint8_t target_slot);
```

语义：

- 物料盘由 CH4 连续舵机驱动，**每个工位固定为一次 `pulse_start(speed_angle, index_duration_ms)`**，
  默认 60°、500ms；一次脉冲到一个工位，不是定位舵机。
- `tray_controller_set_zero()`：要求当前不忙且 CH4 无活动点动；成功后
  `zero_valid=true`、`current_slot=target_slot=0`、清空颜色台账。
- `tray_controller_invalidate()`：作废零点（不清除槽位颜色记录）。
- `tray_controller_configure()`：`speed_angle` 必须在 1~89（0 和 ≥90 拒绝），
  `index_duration_ms` 20~10000；忙时拒绝。
- `start_next()`：`(current+1) % 6`；`start_to(slot)`：0~5，已在目标则立即 DONE。
- `tray_controller_update()`：**必须周期调用**（比赛任务每 10ms）。它检测点动故障
  （置 ERROR 并作废零点）、等待 200ms 机械稳定、然后推进一个槽位；未到目标会自动
  再发一次脉冲。
- `tray_controller_busy()`：`motion == RUNNING`。
- `tray_controller_abort()`：取消 CH4 点动、`motion=IDLE`、**作废零点**。
- `mark_loaded(slot, color)`：需要 `zero_valid` 且颜色 1~3；`mark_empty(slot)` 只需槽位合法。
- `find_color(color)`：返回第一个匹配槽位，未找到或零点无效返回 -1。
- `tray_forward_steps(current, target)`：返回正转需要经过的工位数；越界返回 `0xFF`。
- 内部使用**递归互斥锁**（200ms 超时），比赛任务与 HTTP 手动命令可安全并发。

### 4.11 `manipulator.h` —— 机械臂动作脚本

```c
typedef enum {
    MANIPULATOR_OP_STOW = 0,
    MANIPULATOR_OP_PICK_AND_LOAD,
    MANIPULATOR_OP_UNLOAD_AND_PLACE,
} manipulator_operation_t;

typedef enum {
    MANIPULATOR_IDLE = 0,
    MANIPULATOR_RUNNING,
    MANIPULATOR_DONE,
    MANIPULATOR_ERROR,
} manipulator_status_t;

typedef struct {
    int32_t z_transport;        // 运输位 Z，步；valid_config 要求必须为 0
    int32_t z_ground_pick;      // 地面抓取 Z，步；0..z_max
    int32_t z_tray;             // 盘内取放 Z，步；0..z_max
    int32_t z_ground_place;     // 靶环放置 Z，步；0..z_max
    int32_t z_max;              // Z 最大软限位，微步；1600..1600000
    uint8_t base_stow;          // 底座运输角，0~180（逻辑角，按时间定位）
    uint8_t base_pick;          // 底座地面抓取角，0~180
    uint8_t base_tray;          // 底座盘位角，0~180
    uint8_t base_place;         // 底座靶环角，0~180
    uint8_t gripper_open;       // 夹爪张开角，0~180，且 ≠ closed
    uint8_t gripper_closed;     // 夹爪闭合角，0~180，且 ≠ open
    uint8_t base_speed_angle;   // CH0 连续舵机速度角 10~170（<90 正转）
    uint16_t base_sweep_ms;     // CH0 逻辑 0~180° 行程时间 200~10000ms
    uint16_t base_settle_ms;    // 底座到位稳定，0~5000ms
    uint16_t gripper_settle_ms; // 夹爪到位稳定，0~5000ms
    float lift_speed_sps;       // Z 定步峰值速度，1600~10000 微步/秒
} manipulator_config_t;

void      manipulator_init(void);
void      manipulator_configure(const manipulator_config_t *config);
esp_err_t manipulator_start(manipulator_operation_t operation);
manipulator_status_t manipulator_update(void);
bool      manipulator_busy(void);
esp_err_t manipulator_last_error(void);
void      manipulator_abort(void);
uint8_t   manipulator_current_step(void);
```

语义：

- `manipulator_configure()` 只在非 RUNNING 时接受新配置。
- `manipulator_start()` 要求当前非 RUNNING，否则 `ESP_ERR_INVALID_STATE`；立即返回。
- `manipulator_update()` 是**增量式、非阻塞**的，必须周期调用；每次最多启动一个动作，
  然后检查舵机稳定计时或升降定步状态。返回 `RUNNING / DONE / ERROR`。
- 动作脚本（`STEPPER_MOVE_DONE`/位置一致才算完成）：
  - **STOW**：夹爪张开 → Z 运输位 → 底座运输角。
  - **PICK_AND_LOAD**：夹爪张开 → 底座抓取角 → Z 地面抓取 → 夹爪闭合 →
    Z 运输位 → 底座盘位角 → Z 盘内 → 夹爪张开 → Z 运输位 → 底座运输角。
  - **UNLOAD_AND_PLACE**：底座盘位角 → Z 盘内 → 夹爪闭合 → Z 运输位 →
    底座靶环角 → Z 靶环放置 → 夹爪张开 → Z 运输位 → 底座运输角。
- `manipulator_abort()`：若在 RUNNING 则回到 IDLE，并清除当前步骤/等待标志；
  不会自动把机械臂收回。
- `manipulator_current_step()`：调试用，返回当前脚本步骤索引。

### 4.12 `competition.h` —— 自动比赛状态机

```c
typedef enum {
    COMPETITION_MANUAL = 0,
    COMPETITION_ARM_STOW,
    COMPETITION_SOURCE_TRAVEL,
    COMPETITION_SOURCE_BRAKE,
    COMPETITION_SOURCE_IDENTIFY,
    COMPETITION_SOURCE_ALIGN,
    COMPETITION_SOURCE_PICK_LOAD,
    COMPETITION_SOURCE_INDEX,
    COMPETITION_TARGET_TRAVEL,
    COMPETITION_TARGET_BRAKE,
    COMPETITION_TARGET_IDENTIFY,
    COMPETITION_TARGET_ALIGN,
    COMPETITION_TARGET_SELECT,
    COMPETITION_TARGET_PLACE,
    COMPETITION_FINISHED,
    COMPETITION_ABORTED,
} competition_state_t;

typedef enum {
    COMPETITION_ERROR_NONE = 0,
    COMPETITION_ERROR_CALIBRATION,
    COMPETITION_ERROR_VISION_LINK,
    COMPETITION_ERROR_VISION_TIMEOUT,
    COMPETITION_ERROR_ALIGNMENT,
    COMPETITION_ERROR_TRAY,
    COMPETITION_ERROR_MANIPULATOR,
    COMPETITION_ERROR_CHASSIS,
    COMPETITION_ERROR_SENSOR,
    COMPETITION_ERROR_SERVO,
    COMPETITION_ERROR_TIMEOUT,
    COMPETITION_ERROR_MANUAL_STOP,
} competition_error_t;

typedef struct {
    manipulator_config_t manipulator;
    uint16_t material_target_cx;
    uint16_t material_target_cy;
    uint16_t ring_target_cx;
    uint16_t ring_target_cy;
    uint16_t vision_tolerance_px;
    uint8_t  vision_min_confidence;
    uint8_t  max_align_iterations;
    int8_t   vision_x_sign;
    int8_t   vision_y_sign;
    float    align_nudge_mm;
    float    align_speed_sps;
    uint8_t  tray_speed_angle;
    uint32_t tray_index_ms;
    station_detector_config_t station;
} competition_config_t;

typedef struct {
    competition_state_t state;
    competition_error_t error;
    bool running;
    bool config_confirmed;
    bool z_zero_valid;
    uint8_t picked_count;
    uint8_t placed_count;
    uint8_t align_iterations;
    vision_color_t current_color;
    vision_color_t required_color;
    int32_t z_position;
    int64_t state_elapsed_ms;
    int64_t run_elapsed_ms;
    tray_status_t tray;
    k230_link_status_t link;
    vision_result_t vision;
} competition_status_t;

esp_err_t competition_init(void);
esp_err_t competition_start(void);
void      competition_abort(competition_error_t error);
bool      competition_auto_active(void);
void      competition_get_status(competition_status_t *out);
void      competition_get_config(competition_config_t *out);
esp_err_t competition_set_config(const competition_config_t *config, bool confirm);
esp_err_t competition_set_z_zero(void);
esp_err_t competition_confirm_config(void);
const char *competition_state_name(competition_state_t state);
const char *competition_error_name(competition_error_t error);
```

#### 4.12.1 配置字段、单位与合法范围

`valid_config()` 会逐项校验；任何越界都会让 `set_config()` 返回 `ESP_ERR_INVALID_ARG`。

| 字段 | 单位 | 默认值 | 合法范围 / 说明 |
|---|---|---|---|
| `manipulator.z_transport` | 微步 | 0 | **必须为 0**（运输位即零点） |
| `manipulator.z_ground_pick` | 微步 | 96000 | 0 … `z_max` |
| `manipulator.z_tray` | 微步 | 40000 | 0 … `z_max` |
| `manipulator.z_ground_place` | 微步 | 96000 | 0 … `z_max` |
| `manipulator.z_max` | 微步 | 160000 | 1600 … 1600000 |
| `manipulator.base_stow` | 逻辑° | 90 | 0 … 180 |
| `manipulator.base_pick` | 逻辑° | 0 | 0 … 180 |
| `manipulator.base_tray` | 逻辑° | 180 | 0 … 180 |
| `manipulator.base_place` | 逻辑° | 0 | 0 … 180 |
| `manipulator.gripper_open` | ° | 30 | 0 … 180，且 ≠ `gripper_closed` |
| `manipulator.gripper_closed` | ° | 80 | 0 … 180，且 ≠ `gripper_open` |
| `manipulator.base_speed_angle` | ° | 60 | 10 … 170（<90 正转） |
| `manipulator.base_sweep_ms` | ms | 2400 | 200 … 10000 |
| `manipulator.base_settle_ms` | ms | 450 | 0 … 5000 |
| `manipulator.gripper_settle_ms` | ms | 300 | 0 … 5000 |
| `manipulator.lift_speed_sps` | 步/秒 | 300 | 100 … 1200 |
| `material_target_cx` | px | 160 | 0 … 319（QVGA 宽 320） |
| `material_target_cy` | px | 120 | 0 … 239（QVGA 高 240） |
| `ring_target_cx` | px | 160 | 0 … 319 |
| `ring_target_cy` | px | 120 | 0 … 239 |
| `vision_tolerance_px` | px | 8 | 1 … 100 |
| `vision_min_confidence` | % | 10 | 0 … 100 |
| `max_align_iterations` | 次 | 5 | 1 … 20 |
| `vision_x_sign` | ±1 | 1 | 只能为 +1 或 -1 |
| `vision_y_sign` | ±1 | 1 | 只能为 +1 或 -1 |
| `align_nudge_mm` | mm | 3.0 | 1.0 … 50.0（<1mm 会四舍五入成 0 步） |
| `align_speed_sps` | 步/秒 | 200 | 50 … 1000 |
| `tray_speed_angle` | ° | 60 | 1 … 89（必须 <90） |
| `tray_index_ms` | ms | 500 | 20 … 10000 |
| `station.min_black` | 路 | 6 | 2 … 8 |
| `station.confirm_frames` | 帧 | 2 | 1 … 10 |
| `station.clear_max_black` | 路 | 3 | 必须 < `min_black` |
| `station.rearm_frames` | 帧 | 3 | 1 … 20 |
| `station.min_interval_ms` | ms | 800 | ≤ 10000 |

`apply_config()` 会把 `z_max` 自动安装为升降软限位 `[0, z_max]`，无需手动调用
`chassis_set_lift_limits()`。

#### 4.12.2 启动前置条件

`competition_start()` 在以下条件**全部满足**时返回 `ESP_OK`，否则返回
`ESP_ERR_INVALID_STATE`：

1. `!s_running`；
2. `config_confirmed && z_zero_valid && tray.zero_valid`；
3. `k230_link_online() && lu9685_available() && lu9685_pulse_ready() &&
   !lu9685_pulse_faulted(LU9685_CH_TRAY)`；
4. `stepper_chassis_enabled() && stepper_lift_enabled()`；
5. `ads1115_get_latest()` 已产出有效帧；
6. `!tray_controller_busy() && !manipulator_busy() &&
   !chassis_translation_active() && chassis_lift_idle()`。

成功后先 `chassis_stop()` 平滑停车、重新应用标定、清零计数，进入 `ARM_STOW`。

#### 4.12.3 状态流转

| 当前状态 | 完成/事件 | 下一状态 |
|---|---|---|
| `ARM_STOW` | 收臂脚本完成 | `SOURCE_TRAVEL` |
| `SOURCE_TRAVEL` | 横线事件 | `SOURCE_BRAKE` |
| `SOURCE_BRAKE` | 四轮速度归零 | `SOURCE_IDENTIFY` |
| `SOURCE_IDENTIFY` | 稳定颜色 + 置信度达标 | `SOURCE_ALIGN` |
| `SOURCE_ALIGN` | 误差在容差内 | `SOURCE_PICK_LOAD` |
| `SOURCE_PICK_LOAD` | 抓取装盘脚本完成 | `picked<3 → SOURCE_INDEX`，否则 `TARGET_TRAVEL` |
| `SOURCE_INDEX` | 盘分度完成 | `SOURCE_TRAVEL` |
| `TARGET_TRAVEL` | 横线事件 | `TARGET_BRAKE` |
| `TARGET_BRAKE` | 四轮速度归零 | `TARGET_IDENTIFY` |
| `TARGET_IDENTIFY` | 稳定目标颜色 | `TARGET_ALIGN` |
| `TARGET_ALIGN` | 误差在容差内 | `TARGET_SELECT` |
| `TARGET_SELECT` | 盘分度到目标颜色槽位 | `TARGET_PLACE` |
| `TARGET_PLACE` | 放置脚本完成 | `placed<3 → TARGET_TRAVEL`，否则 `FINISHED` |
| 任意 | 故障 / 超时 / `competition_abort()` | `ABORTED` |

#### 4.12.4 中止与超时

- `competition_abort(error)`：可在任意任务/HTTP 中调用；内部以 `portMAX_DELAY` 获取
  比赛锁，确保状态一定落锁。它会：
  1. `running=false`、`state=ABORTED`、记录错误；
  2. **作废 Z 零点**（`z_zero_valid=false`）；
  3. `manipulator_abort()`、`chassis_stop()`；
  4. `tray_controller_abort()`（同时**作废盘位零点**）；
  5. `lu9685_pulse_stop_all()`、K230 切回 `IDLE`。
- 因此中止后必须重新执行 `competition_set_z_zero()` 与 `tray_controller_set_zero()`，
  并重新确认标定。
- 超时（宏定义在 `competition.c`）：
  - 总运行 **360000ms（6 分钟）** → `TIMEOUT`；
  - 识别 12000ms → `VISION_TIMEOUT`；
  - 对位 24000ms（2×识别） → `ALIGNMENT`；
  - 单次取放脚本 180000ms → `TIMEOUT`；
  - 传感器帧停滞 500ms → `SENSOR`；
  - 丢失循迹线 3000ms → `SENSOR`；
  - 刹车等待 `max(1800ms, 最大轮速/加速度·1000 + 800ms)` → `CHASSIS`。

#### 4.12.5 配置与零点 API

- `competition_set_config(cfg, confirm)`：校验并整体替换配置；运行中或物料盘忙时
  返回 `ESP_ERR_INVALID_STATE`。`confirm=true` 等价于保存同时确认。
- `competition_confirm_config()`：运行中或配置非法时拒绝；否则置 `config_confirmed=true`
  并重新应用配置。
- `competition_set_z_zero()`：要求未运行且 `chassis_lift_idle()`；把 `AXIS_LIFT`
  位置写 0 并置 `z_zero_valid=true`。
- `competition_auto_active()`：等价于 `running`，HTTP 层用它拦截手动执行器命令。
- `competition_get_status()` / `get_config()`：带 50ms 超时的快照拷贝。
- `competition_state_name()` / `competition_error_name()`：返回静态字符串，可直接放进 JSON。

### 4.13 `wifi_sta.h`

```c
esp_err_t   wifi_sta_init(void);      // 连接 app_config.h 中的 WIFI_SSID / WIFI_PASS
bool        wifi_sta_connected(void);
const char *wifi_sta_ip(void);        // 未连接返回 "0.0.0.0"
```

- `WIFI_PS_NONE`：关闭省电，保证 Web 轮询低延迟。
- 断线每 2 s 重连一次，无限重试；断开原因按名称记录（`NO_AP_FOUND`、
  `4WAY_HANDSHAKE_TIMEOUT` 等），连续失败 5 次自动扫描并打印周围 AP
  （SSID / RSSI / 信道 / 加密方式）。
- 目标路由器在 `app_config.h`：`WIFI_SSID "哇哇哇哇哇"` / `WIFI_PASS "13532535322"`。
- **APSTA 兜底**：固件同时开启 AP `mecanum-car` / `mecanum123`，
  即使路由器不可用，也可连该热点访问 `http://192.168.4.1/`。
- Wi-Fi 初始化失败时 `main` 只打印错误、不重启，串口仍可查看原因。

### 4.14 `web_server.h` + HTTP API

```c
esp_err_t web_server_start(void);     // 端口 80，需 Wi-Fi 已启动
```

所有接口均为 **GET**，成功返回 `{"ok":1}`；参数错误返回 400 `{"ok":0,"msg":"..."}`；
LU9685 离线时 `/api/servo` 返回 400；自动比赛运行中，手动执行器接口返回
**409 Conflict**（`automatic mission owns actuators`）。

| 接口 | 参数 | 说明 |
|---|---|---|
| `/` | — | 内嵌控制台页面 |
| `/api/status` | — | 状态 JSON（见 4.14.1） |
| `/api/move` | `vx` `vy` `w` | 手动方向指令，非零自动切手动 |
| `/api/mode` | `m=0\|1` | 0 手动 / 1 循迹 |
| `/api/en` | `chassis=0\|1` `lift=0\|1` | 1 = 锁轴 |
| `/api/lift` | `v` | 只取符号，速度取 `lift_speed` |
| `/api/servo` | `ch` `a` | 角度 0~180（按当前协议下发） |
| `/thresh` | `th`（或 `v`） | 阈值 0.0~5.0 V |
| `/api/line` | `th` `inv` `rev` `kp` `ki` `kd` `om` `accel`（`base` 兼容） | 循迹参数 |
| `/speed`（别名 `/api/speed`） | `chassis` `lift` | 1600~10000 微步/秒 |
| `/servo_proto` | `mode=0\|1\|2` | 切换协议并重发三路角度 |
| `/servo_pulse` | `ch` `speed_angle` `duration_ms` `stop?` | 360° 舵机定时点动 |
| `/servo_center` | `ch` `a` | 校准中位停止值 |
| `/api/stop` | — | 平滑停车 + 取消舵机点动 + 中止自动比赛 |
| `/settings` | `save=1` | 立即保存调参到 NVS（平时约 2s 自动保存） |
| `/api/competition` | — | 比赛状态 JSON（见 4.14.2） |
| `/api/competition/cmd` | `cmd=start\|abort\|z_zero\|confirm\|tray_zero\|tray_next\|tray_to`；`tray_to` 另需 `slot=0..5` | 比赛命令 |
| `/api/competition/config` | 见 4.12.1 的字段名；`confirm=1` 同时确认 | 保存/确认标定 |
| `/favicon.ico` | — | 204 |

说明：

- `start / abort / z_zero / confirm` 不受“自动运行中禁止手动”限制；
  `tray_zero / tray_next / tray_to` 与其他手动接口一样，运行中返回 409。
- `/api/competition/config` 若一个已知字段都没提供，返回 400，避免键名拼错时静默确认旧值。

#### 4.14.1 `/api/status` 字段

```json
{
  "mode":0, "vx":0.00, "vy":0.45, "w":0.00,
  "en":[1,0],
  "th":4.00, "inv":0, "rev":0,
  "kp":0.60, "ki":0.00, "kd":0.15, "base":0.30, "om":1.5, "acc":1200,
  "cs":300, "lsp":300,
  "v":[0.12,0.13,0.11,0.12,0.14,0.12,0.13,0.12],
  "b":[1,1,1,1,1,1,1,1],
  "srv":[90,90,90], "srv_ok":1, "proto":0, "ctr":[90,90,90],
  "lift":0, "err":0.00, "ls":0, "zpos":0, "zmove":0,
  "ws":[450,450,450,450],
  "seq":128, "wifi":1, "ip":"192.168.1.23"
}
```

| 字段 | 含义 |
|---|---|
| `mode` | 0 手动 / 1 循迹 / 2 位置 |
| `vx/vy/w` | 当前生效速度指令（方向语义） |
| `en[2]` | 底盘 / 升降 EN（1 = 锁轴） |
| `th/inv/rev` | 阈值 V / 黑线为高 / 探头顺序反转 |
| `kp/ki/kd/om/acc` | PID、ω 上限、加速度 |
| `base` | 旧字段（m/s），实际不用 |
| `cs/lsp` | 底盘航速 / 升降速度（步/秒） |
| `v[8]` | 8 路电压 V（探头 1→8） |
| `b[8]` | 黑白判定 0/1 |
| `srv[3]` | CH0/CH4/CH8 缓存角度 |
| `srv_ok` | LU9685 是否在线 |
| `proto/ctr[3]` | 舵机协议 / 三路中位校准值 |
| `lift` | 当前升降步频（带符号） |
| `err/ls` | 循迹误差 / 是否看到线 |
| `zpos` | 升降轴开环位置计数（步） |
| `zmove` | 升降定步运动是否进行中（1/0） |
| `ws[4]` | RR/RF/LF/LR 目标步频 |
| `seq` | 采样帧号 |
| `wifi/ip` | 连接状态 / IP |

#### 4.14.2 `/api/competition` 字段

```json
{
  "ok":1, "state":"MANUAL", "error":"NONE", "running":0,
  "cfgok":0, "zok":0, "picked":0, "placed":0, "align":0,
  "color":0, "required":0, "zpos":0, "state_ms":0, "run_ms":0,
  "tray":{"zero":0,"slot":0,"target":0,"motion":0,"speed":60,"ms":500,
          "colors":[0,0,0,0,0,0]},
  "link":{"online":0,"mode":0,"ack":0,"frames":0},
  "vision":{"mode":0,"color":0,"detected":0,"stable":0,
            "cx":0,"cy":0,"area":0,"conf":0,"age":-1},
  "cfg":{"zpick":96000,"ztray":40000,"zplace":96000,"zmax":160000,
         "bstow":90,"bpick":0,"btray":180,"bplace":0,"gopen":30,"gclose":80,
         "bms":450,"gms":300,"bspeed":60,"bsweep":2400,"lsps":6400.0,
         "mcx":160,"mcy":120,"rcx":160,"rcy":120,
         "tol":8,"minconf":10,"maxiter":5,"xsign":1,"ysign":1,"nudge":3.00,"asps":200.0,
         "tspeed":60,"tms":500,"sblack":6,"sconfirm":2,"sclear":3,"srearm":3,"sinterval":800}
}
```

| 字段 | 含义 |
|---|---|
| `state/error` | 状态名 / 错误名（字符串，见 4.12） |
| `running` | 自动流程是否运行中 |
| `cfgok` | 标定是否已确认 |
| `zok` | Z 零点是否有效 |
| `picked/placed` | 已抓取 / 已放置数量（各 3 个结束） |
| `align` | 当前对位已修正次数 |
| `color/required` | 当前识别颜色 / 目标槽位所需颜色（0~3） |
| `zpos` | 升降位置计数（步） |
| `state_ms/run_ms` | 当前状态已持续 / 本次运行已持续（ms） |
| `tray.zero` | 盘位零点有效 |
| `tray.slot/target` | 当前槽位 / 目标槽位 |
| `tray.motion` | 0 IDLE / 1 RUNNING / 2 DONE / 3 ERROR |
| `tray.speed/ms` | CH4 速度角 / 每工位脉冲时长 |
| `tray.colors[6]` | 各槽位颜色（0 空 / 1 红 / 2 绿 / 3 蓝） |
| `link.online` | K230 是否在线 |
| `link.mode/ack` | 请求模式 / 最后 ACK 模式 |
| `link.frames` | 收到的合法帧总数 |
| `vision.mode/color` | 最近检测的模式 / 颜色 |
| `vision.detected/stable` | 是否检测到 / 是否稳定 |
| `vision.cx/cy/area` | 目标中心与面积（像素） |
| `vision.conf` | 置信度 % |
| `vision.age` | 结果年龄 ms；无结果为 -1 |
| `cfg.*` | 当前标定值，字段名与 `/api/competition/config` 一致 |

### 4.15 `settings.h` —— 调参参数持久化

```c
esp_err_t settings_init(void);        // 上电调用：加载 NVS 并应用到各模块
void      settings_request_save(void); // 参数变化后调用（约 2s 防抖后自动保存）
esp_err_t settings_save_now(void);     // 立即保存
bool      settings_last_save_ok(void);
```

持久化内容（NVS 命名空间 `tuning`，键 `blob`，带 magic/version）：

- 循迹：`kp / ki / kd / threshold / max_omega / black_high / reverse`
- 步进：`accel`
- 底盘：`chassis_sps / lift_sps`
- 舵机：协议模式、CH0/CH4/CH8 中位校准

规则：

- `settings_init()` 必须在 `line_follow_init()`、`chassis_init()`、`lu9685_init()`
  之后调用；无保存数据时保持各模块默认值。
- 网页调用 `/thresh`、`/api/line`、`/speed`、`/servo_proto`、`/servo_center`
  会自动标记保存；**自动比赛配置（competition_config）不在本 blob 内**，
  仍按比赛卡片流程每次确认。
- 保存值经过各 setter 的限幅与 `isfinite` 检查，损坏/版本不符的 blob 会被忽略。

---

## 5. K230 串口协议与 Python 端职责

### 5.1 帧格式（两端一致）

```
A5 5A | ver(0x01) | type | seq | len | payload[len] | CRC8
```

| 偏移 | 字节 | 字段 |
|---|---|---|
| 0~1 | 2 | 同步字 `A5 5A` |
| 2 | 1 | 协议版本 `0x01` |
| 3 | 1 | 消息类型 |
| 4 | 1 | 序号 `seq`（0~255 循环） |
| 5 | 1 | payload 长度（0~32） |
| 6… | N | payload |
| 末尾 | 1 | CRC8 |

- CRC8：多项式 **0x07**、初值 0、无反射、xorout 0，覆盖 **版本到 payload 末尾**
  （`frame[2 : 6+len]`），同步字不参与。
- 固定开销 7 字节，最大 payload 32 字节，最大帧 39 字节。
- 流式解析：逐字节找同步字；CRC/版本/长度任一失败只丢弃一个字节后重新同步。

### 5.2 消息类型

| 值 | 名称 | 方向 | payload |
|---|---|---|---|
| `0x01` | COMMAND | ESP32 → K230 | 1 字节模式：0 IDLE / 1 MATERIAL / 2 RING / 3 TRAY |
| `0x02` | HEARTBEAT | 双向 | 空；**500ms** |
| `0x03` | ACK | K230 → ESP32 | `command_seq, current_mode`（2 字节） |
| `0x10` | DETECTION | K230 → ESP32 | 11 字节，见下；**80ms（12.5Hz）** |

DETECTION payload：

| 字节 | 字段 |
|---|---|
| 0 | `mode`（0~3） |
| 1 | `color`（0 未知 / 1 红 / 2 绿 / 3 蓝） |
| 2 | `flags`：bit0 detected，bit1 stable |
| 3~4 | `cx` 小端 16 位 |
| 5~6 | `cy` 小端 16 位 |
| 7~8 | `area` 小端 16 位 |
| 9 | `confidence`（0~100） |
| 10 | reserved（0） |

无目标时仍按周期上报，颜色/坐标/面积/置信度清零；ESP32 收到 `detected=0` 时会把
`stable` 强制为 false、颜色强制为 unknown。

### 5.3 超时与重试

- ESP32：K230 在线判定 **1500ms**；视觉结果新鲜度 **400ms**。
- K230：超过 **2000ms** 未收到 ESP32 心跳/命令则在屏幕显示通信异常。
- ESP32 `k230_link_set_mode()`：2 轮命令，每轮最多等 12×10ms 的匹配 ACK；
  仍无 ACK 也返回成功，由比赛状态机的 12s 识别超时兜底。
- 序号在 ESP32 发送锁内递增，心跳与比赛命令并发时不会撞号。

### 5.4 Python 端职责（`k230/`）

| 文件 | 职责 |
|---|---|
| `config.py` | UART2/PORT2、115200 8N1；心跳 500ms；检测 80ms；ESP 链路超时 2000ms；QVGA 320×240；LAB 阈值；MATERIAL/RING 的 ROI 与面积阈值；`DEBUG_UART_LOG=False` |
| `protocol.py` | 帧编码/解码、CRC8、流式 `FrameParser`、`TxSequence`、`build_heartbeat/ack/detection`；不依赖硬件 |
| `vision.py` | 按颜色分别 `find_blobs`，在 ROI 内选最大有效 blob；连续 3 帧同色才 `stable`；置信度由像素密度与面积计算 |
| `main.py` | 打开 UART(2)，解析帧；每个合法 COMMAND 清空投票并立即 ACK；每 500ms 发心跳；每 80ms 发 DETECTION；在板载屏显示待机/识别/通信状态 |

约定：

- **MODE_TRAY（3）当前只在协议中预留**，K230 首版不做视觉识别。
- 正常比赛必须保持 `DEBUG_UART_LOG=False`，否则调试文本会污染二进制帧。
- K230 不做运动控制；所有底盘、升降、舵机决策都在 ESP32 侧。

---

## 6. 线程安全与调用约束

### 6.1 锁清单与顺序

| 锁 | 类型 | 保护对象 | 超时 |
|---|---|---|---|
| `competition.s_lock` | 互斥量 | 比赛状态、配置、零点、计数 | 50/100ms；`abort` 用 `portMAX_DELAY` |
| `chassis.s_control_mux` | 临界区自旋锁 | 模式、速度指令、升降命令、软限位、平移掩码 | 无（短临界区） |
| `stepper.s_move_mux` | 临界区自旋锁 | 目标/速度/相位/位置/定步状态 | 无（短临界区） |
| `chassis.s_status_mutex` | 互斥量 | `chassis_status_t` 快照 | 20ms |
| `tray.s_mux` | **递归**互斥量 | 盘位状态、颜色台账、分度流程 | 200ms |
| `i2c_bus` 锁 | 互斥量 | TCA 选通道 + I2C 事务 | 20ms |
| `lu9685.s_pulse_mux` | 临界区自旋锁 | 每通道点动计时状态 | 无 |
| `k230.s_tx_mutex` | 互斥量 | UART 写与发送序号 | 20ms |
| `k230.s_state_mux` | 临界区自旋锁 | 视觉结果/链路状态快照 | 无 |

**全局锁顺序**：

```
competition 锁 → chassis control 锁 → stepper move 锁
competition 锁 → tray 递归锁 → i2c_bus 锁
competition 锁 → manipulator（无自有锁）→ chassis / lu9685 / i2c_bus
```

- 任何路径都**不得反向获取**：不能持 stepper move 锁去拿 chassis control 锁，
  也不能持 i2c 锁去拿 competition 锁。
- `chassis.c` 内部严格按 `control → move`：例如 `cancel_translation_locked()` 在持
  `s_control_mux` 时调用 `stepper_stop()`。
- 比赛任务在持 `s_lock` 时调用 chassis/tray/manipulator API，因此上述顺序成立。
- `stepper` 的 1kHz 斜坡任务只拿 `s_move_mux`，ISR 只拿 `s_move_mux` 的 ISR 版本。

### 6.2 非阻塞要求

- `competition_update()`、`manipulator_update()`、`tray_controller_update()` 都是
  **增量式**函数，由周期任务反复调用；内部不得加入 `vTaskDelay`，否则会拖慢 10ms 状态机。
- `chassis_start_translation()` / `chassis_lift_move_to()` / `stepper_start_move()`
  只启动运动并立即返回；调用方轮询 `*_active()` / `*_done()` / `get_move_status()`。
- `k230_link_set_mode()` 因 ACK 等待最多阻塞约 240ms，只用于状态切换，不要放进周期循环。
- HTTP 处理函数在 `httpd` 任务中执行，可调用上述 API；手动接口在自动运行中会被 409 拒绝。
- ISR 中禁止调用 I2C、UART、`vTaskDelay`、互斥量；ISR 仅操作步进相位与位置计数。

### 6.3 其他约束

- `lu9685_*` 内部已对 I2C 总线加锁，可直接调用；失败返回 `esp_err_t`。
- `ads1115_get_latest` 内部加锁，10~20ms 超时；不要在 ISR 中调用。
- `line_follow_update` 只能由 `chassis` 任务调用；其他任务只读参数/误差。
- `tray_controller_update` 必须与比赛状态机同频调用（当前 10ms），否则分度不会推进。
- `manipulator_update` 必须持续调用，否则动作脚本会停在当前步骤。
- `competition_abort` 可安全用于 HTTP 急停；它保证拿到锁并完成清理。

---

## 7. 典型调用示例

### 7.1 纯 C 手动控制（不用 Wi-Fi）

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "chassis.h"
#include "stepper.h"

stepper_init();                 // 必须先于 chassis_init
chassis_init();                 // 循迹模式还需要 i2c_bus_init + ads1115_init/start
chassis_set_speeds(450.0f, 400.0f);
stepper_set_chassis_enable(true);

/* 前进 1 秒：每 100ms 刷新一次，避免 400ms 看门狗 */
for (int i = 0; i < 10; ++i) {
    chassis_set_velocity(0.0f, 0.45f, 0.0f);
    vTaskDelay(pdMS_TO_TICKS(100));
}
chassis_stop();
```

### 7.2 循迹

```c
line_params_t p;
line_follow_get_params(&p);
p.threshold  = 2.50f;
p.black_high = false;
p.kp = 1.20f;
p.kd = 0.05f;
line_follow_set_params(&p);

chassis_set_speeds(450.0f, 400.0f);
stepper_set_chassis_enable(true);
chassis_set_mode(CHASSIS_MODE_LINE);
```

### 7.3 舵机定位与 360° 点动

```c
lu9685_set_angle(LU9685_CH_BASE, 135);                  // 位置舵机
lu9685_pulse_start(LU9685_CH_TRAY, 60, 500, -1);         // 360° 舵机正转 0.5s 后回中位
```

### 7.4 开环定步升降与短距对位

```c
stepper_set_lift_enable(true);
chassis_set_lift_limits(0, 160000);         // 自动比赛会按 z_max 自动安装
chassis_lift_move_to(40000, 6400.0f);       // 走到 40000 微步
while (chassis_lift_move_active()) {
    vTaskDelay(pdMS_TO_TICKS(20));
}

stepper_set_chassis_enable(true);
chassis_start_translation(3.0f, 0.0f, 200.0f);   // 右移 3mm
while (chassis_translation_active()) {
    vTaskDelay(pdMS_TO_TICKS(20));
}
if (chassis_translation_done()) {
    chassis_stop();                          // 退出 POSITION 模式
}
```

### 7.5 读取探头并自行判定

```c
float v[8];
uint32_t seq;
if (ads1115_get_latest(v, &seq)) {
    bool black = line_follow_is_black(v[0]);   // 使用当前阈值/极性
}
```

### 7.6 启动自动比赛

```c
competition_config_t cfg;
competition_get_config(&cfg);          // 取默认值/当前值
cfg.manipulator.z_max = 10000;
cfg.manipulator.z_ground_pick = 6200;
cfg.vision_tolerance_px = 8;
cfg.align_nudge_mm = 3.0f;
ESP_ERROR_CHECK(competition_set_config(&cfg, true));   // 保存并确认
ESP_ERROR_CHECK(competition_set_z_zero());             // 当前升降位置设为 0
ESP_ERROR_CHECK(tray_controller_set_zero());           // 当前盘位设为 slot0

stepper_set_chassis_enable(true);
stepper_set_lift_enable(true);
/* 确保机械臂、物料盘、平移、升降都空闲后 */
ESP_ERROR_CHECK(competition_start());
```

---

## 8. 常见陷阱

1. **手动速度是“方向”不是 m/s**：`chassis_set_velocity` 会归一化到 `chassis_base_speed`。
   需要精确 m/s 时用 `mecanum_inverse` + `stepper_set_chassis_speeds`。
2. **默认阈值 4.00 V + 默认极性**：8 路可能全判为黑线。现场必须按实际白底/黑线电压校准。
3. **比赛启动要求执行器全静止**：物料盘忙、机械臂忙、有平移、升降未空闲时
   `competition_start()` 都会返回 `ESP_ERR_INVALID_STATE`；手动循迹的残留轮速也会被
   判定为不满足条件。
4. **丢线不会停车/中止**：循迹阶段看不到线时保持上次转向继续走；若车辆因此跑偏，
   属于标定或赛道问题，可用网页 `ls` 观察是否长时间为 0。
5. **`align_nudge_mm` 必须 ≥1mm**：1 微步约 0.064mm，1mm≈15.7 微步；更小的修正会
   被四舍五入成 0 步并直接判失败；合法范围 1.0~50.0mm。
6. **`lift_speed_sps` 必须 ≥1600，但过低速度可能触发超时**：合法范围 1600~10000；
   1600 微步/秒走 96000 微步约需 60 秒，取放脚本含多次升降，极端低速可能超过 180 秒动作超时。
7. **中止会作废两个零点**：`competition_abort()` / `/api/stop` 会把
   `z_zero_valid` 与 `tray.zero_valid` 都清零；必须重新设 Z 零点和盘位零点。
8. **初始化顺序**：`stepper_init()` 必须最先，否则 EN 可能在上电瞬间保持低电平使电机发热。
9. **I2C 不加锁**：自己写 I2C 代码时漏掉 `i2c_bus_lock` 会在高频采样下偶发读错通道。
10. **LU9685 协议**：若定位无效且持续转动，先用 `/servo_proto?mode=1` 或 `2` 试；
    仍持续转动说明是 360° 连续舵机，改用 `lu9685_pulse_start`。
11. **看门狗**：HTTP 松键后前端会发 0；自写客户端必须每 <400ms 刷新移动指令。
12. **EN 关闭**：会把目标速度清 0 并取消定步，重新使能后需要重新下发速度指令。
13. **方向反了**：优先调 `stepper_set_dir_invert(axis, true)`，不要改接线或公式。
14. **步频上限**：单轴 10000 微步/秒（1/16 细分，脉冲率 10kHz 硬上限，约 0.64 m/s）；
    底盘航速上限同为 10000，留足加速余量。
15. **左前/左后 DIR 默认取反**：这是对置安装的机械补偿，若整车前进方向反了，
    应同时翻转四个轮子，而不是单独翻转一侧。
16. **K230 调试文本**：`DEBUG_UART_LOG` 必须保持 `False`，否则文本会污染协议帧。
17. **Z 位置是开环计数**：`stepper_get_position(AXIS_LIFT)` 只统计发出的脉冲，
    丢步/堵转不会体现；必须靠软限位和机械限位兜底。

---

## 9. 单位与换算速查

| 物理量 | 单位 | 换算 |
|---|---|---|
| 轮速指令 | m/s | `steps = mps × 200 / (π × 0.065)` ≈ ×979.4 |
| 步频 | 步/秒 | 1 圈 = 200 步 = π×65mm ≈ 204.2 mm |
| 角速度 | rad/s | 轮速差 = `2 × k × ω`，`k = 0.175 m` |
| 升降位置 | 步 | 开环计数；约 **1 步 ≈ 1.02mm**（按 200 步/圈与实测导程） |
| 舵机角度 | 0~180 | 脉宽模式：`500 + angle × 2000/180` µs |
| 视觉坐标/面积 | px | QVGA 320×240，小端 16 位 |
| 采样电压 | V | `raw × 6.144 / 32768` |
| 阈值 | V | 后端 0.0~5.0；网页滑块 4.00~5.00，步进 0.01 |
| 对位微调 | mm | 1.0~50.0，默认 3.0 |
| 定时/超时 | ms | 心跳 500，检测 80，链路 1500/2000，结果 400 |

---

## 10. 源码索引

| 文件 | 内容 |
|---|---|
| `main/app_config.h` | 全部引脚、地址、机械、速度、K230 UART 参数 |
| `main/stepper.c` | GPTimer ISR + DDS + 梯形斜坡 + 开环定步/位置计数 |
| `main/mecanum.c` | 逆运动学与单位换算 |
| `main/i2c_bus.c` | 总线、TCA9548A、互斥锁 |
| `main/ads1115.c` | 双 ADS 乒乓采样任务 |
| `main/lu9685.c` | 三协议舵机 + 点动定时任务 |
| `main/line_follow.c` | 加权 PID |
| `main/chassis.c` | 100Hz 控制环、模式仲裁、看门狗、定步平移、升降软限位 |
| `main/vision_protocol.c` | 视觉帧常量、CRC8、解析/编码 |
| `main/k230_link.c` | UART1 链路、心跳、ACK、视觉结果缓存 |
| `main/station_detector.c` | 横线/工位上升沿检测 |
| `main/tray_controller.c` | 6 工位分度与颜色台账 |
| `main/manipulator.c` | 升降/底座/夹爪动作脚本 |
| `main/competition.c` | 三取三放状态机、配置校验、超时与中止 |
| `main/wifi_sta.c` | STA 连接与重连 |
| `main/web_server.c` | HTTP 路由与 JSON |
| `main/web_page.h` | 内嵌控制台（含比赛卡片） |
| `k230/config.py` | K230 UART/视觉参数 |
| `k230/protocol.py` | K230 帧协议实现 |
| `k230/vision.py` | blob 识别与颜色投票 |
| `k230/main.py` | K230 主循环与屏幕状态 |
| `README.md` | 接线、编译烧录、比赛流程、Web 使用 |
| `docs/DRIVER_API.md` | 本接口手册 |

---

*最后更新：2026-11-15　固件 0xE93C0（955328 字节，约 933 KiB），ESP-IDF v6.1.0 编译 0 warning。*
