# 麦克纳姆轮小车驱动接口手册（供 Agent / 二次开发调用）

> 适用工程：`E:\xiaogongxun_codes\helloworld`（ESP-IDF ≥ 5.2，已在 6.1.0 验证）
> 目标芯片：ESP32-S3　构建系统：CMake + `idf.py build`
> 本文档描述**已实现的公共 API**，不含内部实现细节。所有函数都带 `extern "C"` 保护。

---

## 0. 一句话集成规则

| 想做的事 | 调用 |
|---|---|
| 上电初始化 | 按第 2 节顺序调用各 `*_init()` |
| 让车走 | `chassis_set_velocity(vx, vy, w)`，每 <400ms 刷新一次 |
| 让车按 8 路探头巡线 | `chassis_set_mode(CHASSIS_MODE_LINE)` |
| 调车速 | `chassis_set_speeds(chassis_sps, lift_sps)` |
| 升降 | `chassis_lift_jog(+1 / -1 / 0)`（只取符号） |
| 舵机定位 | `lu9685_set_angle(ch, 0~180)` |
| 360° 舵机转固定角度 | `lu9685_pulse_start(ch, speed_angle, duration_ms, -1)` |
| 读探头电压 | `ads1115_get_latest(float v[8], &seq)` |
| 不用 Web，纯 C 控制 | 只依赖 `chassis.h` / `mecanum.h` / `stepper.h` |

**硬约束**：不要在 ISR 里调用任何 I2C 函数；步进轴速度上限 8000 步/秒；EN 低电平为锁轴。

---

## 1. 硬件拓扑（与 `main/app_config.h` 一致，改硬件只改该文件）

### 1.1 步进电机 5 × DRV8825（整步）

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
WHEEL_DIAMETER_MM    65.0f    // 轮径
WHEEL_STEPS_PER_REV  200      // 整步 1.8°
WHEELBASE_MM         160.0f   // 轴距
TRACK_MM             190.0f   // 轮距
```

---

## 2. 初始化顺序（必须遵守）

`app_main()`（`main/main.c`）中已验证的顺序：

```c
nvs_flash_init();            // Wi-Fi 前置
stepper_init();              // ★ 最先：EN 立刻拉高，避免上电锁死发热
i2c_bus_init();              // TCA9548A 复位 + 400kHz 总线
ads1115_init();              // 挂载两块 ADS
ads1115_start();             // 创建 Core1 采样任务
lu9685_init();               // 失败不致命：返回 != ESP_OK 时舵机不可用
line_follow_init();          // 载入 PID 默认参数
chassis_init();              // 创建 100Hz 控制任务
wifi_sta_init();             // 可选
web_server_start();          // 可选
```

依赖关系：
- `ads1115_*` / `lu9685_*` 依赖 `i2c_bus_init()`。
- `chassis_*` 依赖 `stepper_init()` 与 `line_follow_init()`；不依赖 Wi-Fi。
- `web_server_start()` 依赖 `wifi_sta_init()`（只在日志里用 IP）。

---

## 3. 任务 / 中断布局（不要随意改优先级）

| 核 | 执行体 | 周期 | 优先级 |
|---|---|---|---|
| 中断 | `stepper_timer_isr`（GPTimer 1MHz，50µs 报警） | 50 µs | 硬件 3 |
| Core 0 | `step_ramp` 梯形加减速 | 1 ms | 12 |
| Core 0 | Wi-Fi / `httpd` | 事件驱动 | 默认 |
| Core 1 | `ads_sample` 乒乓采样 | 12 ms | 4 |
| Core 1 | `chassis` 控制环 | 10 ms | 6 |
| Core 1 | `srv_pulse` 舵机点动计时 | 5 ms | 3 |

控制环优先级高于采样任务，采样等待可被随时抢占。

---

## 4. 模块 API

### 4.1 `stepper.h` —— 5 轴 DDS 脉冲

```c
typedef enum { AXIS_RR=0, AXIS_RF=1, AXIS_LF=2, AXIS_LR=3, AXIS_LIFT=4, AXIS_COUNT=5 } stepper_axis_t;

esp_err_t stepper_init(void);

void  stepper_set_speed(stepper_axis_t axis, float steps_per_sec);
void  stepper_set_chassis_speeds(const float steps_per_sec[4]);   // 顺序 RR/RF/LF/LR
float stepper_get_speed(stepper_axis_t axis);                     // 当前实际速度（含斜坡）
float stepper_get_target(stepper_axis_t axis);

void  stepper_set_accel(float steps_per_sec2);                    // 默认 3000，范围 100~100000
float stepper_get_accel(void);

void  stepper_set_dir_invert(stepper_axis_t axis, bool invert);   // 运行时翻转 DIR 逻辑
bool  stepper_get_dir_invert(stepper_axis_t axis);

void  stepper_set_chassis_enable(bool enable);   // true=锁轴(EN 低)
void  stepper_set_lift_enable(bool enable);
bool  stepper_chassis_enabled(void);
bool  stepper_lift_enabled(void);

void  stepper_stop(stepper_axis_t axis);         // 目标清零，按加速度减速
void  stepper_stop_all(void);
void  stepper_abort_all(void);                   // 立即清空速度（急停）
```

行为约定：
- 单位 **步/秒**，正负表示方向；内部限幅 ±`STEPPER_MAX_SPEED`(8000)。
- 目标从 0 变非 0 时，**直接跳到 200 步/秒**跨过低速共振区，再按加速度爬升。
- **换向必须先减速到 0**，下一拍才切换 DIR；调用方无需处理。
- STEP 高电平固定 50 µs（远大于 DRV8825 的 3 µs 要求）；DDS 相位增量在 1 kHz 任务中换算。
- `set_chassis_enable(false)` / `set_lift_enable(false)` 会把对应目标速度清 0 并释放电机。
- 所有 setter 只写 `volatile` 变量，**任意任务可调用**；不要在 ISR 里调用。

### 4.2 `mecanum.h` —— O 型麦轮逆运动学

```c
typedef struct { float vx; float vy; float omega; } chassis_vel_t;
// vx>0 右移 (m/s), vy>0 前进 (m/s), omega>0 逆时针 (rad/s, 俯视)

void    mecanum_inverse(const chassis_vel_t *cmd, float wheel_mps[4]); // RR/RF/LF/LR
int32_t mecanum_mps_to_steps(float mps);     // 已限幅 ±8000 并四舍五入
float   mecanum_steps_to_mps(int32_t steps);
float   mecanum_max_wheel_mps(void);         // ≈ 8.17 m/s（当前轮径）
```

公式（`k = (轴距 + 轮距) / 2`）：

```
RR = vy + vx + k·ω      RF = vy - vx + k·ω
LF = vy + vx - k·ω      LR = vy - vx - k·ω
```

步频换算：`steps = mps × 200 / (π · 0.065)` ≈ `mps × 979.4`。

### 4.3 `i2c_bus.h` —— 总线与 TCA9548A

```c
esp_err_t i2c_bus_init(void);
i2c_master_bus_handle_t i2c_bus_handle(void);   // 用于 i2c_master_bus_add_device()
esp_err_t tca_select(uint8_t channel);          // 0~7，一次只开通一路
esp_err_t tca_deselect(void);
bool      tca_available(void);
bool      i2c_bus_lock(uint32_t timeout_ms);
void      i2c_bus_unlock(void);
```

**关键规则**：任何"选通道 → 读/写"序列必须包在 `i2c_bus_lock()` / `i2c_bus_unlock()` 之间，
否则两个任务交错选通道会把数据读到别的器件上。现有 `lu9685_*` 已内部加锁。

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
- `ads1115_get_latest()` 返回 `false` 表示尚无有效数据，此时 `v[]` 保持最后一次值（初值 0）。
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
uint8_t   lu9685_address(void);

esp_err_t lu9685_set_protocol(uint8_t mode);   // 0/1/2，立即生效
uint8_t   lu9685_get_protocol(void);

esp_err_t lu9685_set_angle(uint8_t channel, uint8_t angle);   // 0~180，超限自动限幅
uint8_t   lu9685_get_angle(uint8_t channel);                  // 缓存值，默认 90

esp_err_t lu9685_set_center(uint8_t channel, uint8_t angle);  // 360° 舵机停止点，建议 85~95
uint8_t   lu9685_get_center(uint8_t channel);

esp_err_t lu9685_pulse_start(uint8_t channel, uint8_t speed_angle,
                             uint32_t duration_ms, int stop_angle);  // stop<0 = 用中位
void      lu9685_pulse_stop_all(void);
```

- 板子离线时：`lu9685_available()==false`，`set_angle` 仍更新缓存但返回 `ESP_ERR_INVALID_STATE`。
- `pulse_start` 先发 `speed_angle`，**后台任务**在 `duration_ms` 后自动发停止角；
  同一通道再次调用会重新计时；浏览器/调用方断开也不会一直转。
- 360° 连续舵机判定：`90` 附近停转，偏离越远越快。用 `pulse_start(ch, 60, 500, -1)` 即是"正转约半秒"。

### 4.6 `line_follow.h` —— 8 路加权 PID

```c
typedef struct {
    float kp;            // 默认 0.90
    float ki;            // 默认 0.00
    float kd;            // 默认 0.00
    float threshold;     // 黑白阈值 V，默认 4.00，范围 0.0~5.0
    bool  black_high;    // true = 电压高于阈值为黑线（极性反转）
    bool  reverse;       // true = 探头实际安装为 8→1，位置权重取反
    float base_speed;    // 旧字段：底盘已改用 chassis_base_speed，此处仅保留兼容
    float max_omega;     // 修正角速度上限 rad/s，默认 3.0
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
- 丢线 150 ms 内保持上次转向并返回上次 ω；超过 150 ms 返回 0。
- 积分限幅 ±5。`line_follow_update` 由 `chassis` 任务在 100 Hz 中调用，**不要在其他任务重复调用**。

### 4.7 `chassis.h` —— 底盘控制（推荐的主要入口）

```c
typedef enum { CHASSIS_MODE_MANUAL=0, CHASSIS_MODE_LINE=1 } chassis_mode_t;

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
void chassis_stop(void);

void chassis_set_speeds(float chassis_sps, float lift_sps);  // 100~1500 / 100~1200
void chassis_get_speeds(float *chassis_sps, float *lift_sps);

void chassis_get_status(chassis_status_t *out);
```

**`chassis_set_velocity` 的重要语义（当前版本）**：
`vx / vy / omega` 只决定**方向与各轮比例**，四轮步频的峰值会被归一化到
`chassis_base_speed`（`chassis_set_speeds` 的第一个参数，默认 450 步/秒）。
因此 `(0, 0.45, 0)` 与 `(0, 1, 0)` 都是"全速前进"，`(0, 0, 0)` 才是停车。
如果希望按 m/s 精确控制，请直接调用 `mecanum_*` + `stepper_set_chassis_speeds()`。

安全看门狗：
- 手动模式：`chassis_set_velocity` 后 **400 ms** 未刷新 → 自动停车。
- 升降：`chassis_lift_jog` 后 **500 ms** 未刷新 → 自动停止。
- 对应 EN 关闭时，该侧目标强制为 0。

循迹模式：`chassis_set_mode(CHASSIS_MODE_LINE)` 后，控制环自动读 ADS，
以 `chassis_base_speed` 为基础前进速度，叠加 PID 转向；丢线超时停车。

### 4.8 `wifi_sta.h`

```c
esp_err_t   wifi_sta_init(void);      // 连接 app_config.h 中的 WIFI_SSID / WIFI_PASS
bool        wifi_sta_connected(void);
const char *wifi_sta_ip(void);        // 未连接返回 "0.0.0.0"
```

- `WIFI_PS_NONE`：关闭省电，保证 Web 轮询低延迟。
- 断线每 2 s 重连一次，无限重试。
- 目标热点在 `app_config.h`：`WIFI_SSID "哇哇哇哇哇"` / `WIFI_PASS "13532535322"`，
  换场地时只改这两行；连接的是路由器（STA），不是自建 AP。

### 4.9 `web_server.h` + HTTP API

```c
esp_err_t web_server_start(void);     // 端口 80，需 Wi-Fi 已启动
```

所有接口均为 **GET**，成功返回 `{"ok":1}`；参数错误返回 400 `{"ok":0,"msg":"..."}`；
LU9685 离线时舵机接口返回 503。

| 接口 | 参数 | 说明 |
|---|---|---|
| `/` | — | 内嵌控制台页面 |
| `/api/status` | — | 状态 JSON（见 4.9.1） |
| `/api/move` | `vx` `vy` `w` | 手动速度（方向语义），非零自动切手动 |
| `/api/mode` | `m=0\|1` | 0 手动 / 1 循迹 |
| `/api/en` | `chassis=0\|1` `lift=0\|1` | 1 = 锁轴 |
| `/api/lift` | `v` | 只取符号，速度取 `lift_speed` |
| `/api/servo` | `ch` `a` | 角度 0~180（按当前协议下发） |
| `/thresh` | `th`（或 `v`） | 阈值 0.0~5.0 V |
| `/api/line` | `th` `inv` `rev` `kp` `ki` `kd` `om` `accel`（`base` 兼容） | 循迹参数 |
| `/speed`（别名 `/api/speed`） | `chassis` `lift` | 100~1500 / 100~1200 步/秒 |
| `/servo_proto` | `mode=0\|1\|2` | 切换协议并重发三路角度 |
| `/servo_pulse` | `ch` `speed_angle` `duration_ms` `stop?` | 360° 舵机定时点动 |
| `/servo_center` | `ch` `a` | 校准中位停止值 |
| `/api/stop` | — | 平滑停车（底盘 + 升降） |
| `/favicon.ico` | — | 204 |

#### 4.9.1 `/api/status` 字段

```json
{
  "mode":0, "vx":0.00, "vy":0.45, "w":0.00,
  "en":[1,0],
  "th":4.00, "inv":0, "rev":0,
  "kp":0.90, "ki":0.00, "kd":0.00, "base":0.30, "om":3.0, "acc":3000,
  "cs":450, "lsp":400,
  "v":[0.12,0.13,0.11,0.12,0.14,0.12,0.13,0.12],
  "b":[1,1,1,1,1,1,1,1],
  "srv":[90,90,90], "srv_ok":1, "proto":0, "ctr":[90,90,90],
  "lift":0, "err":0.00, "ls":0,
  "ws":[450,450,450,450],
  "seq":128, "wifi":1, "ip":"192.168.1.23"
}
```

| 字段 | 含义 |
|---|---|
| `mode` | 0 手动 / 1 循迹 |
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
| `ws[4]` | RR/RF/LF/LR 目标步频 |
| `seq` | 采样帧号 |
| `wifi/ip` | 连接状态 / IP |

---

## 5. 线程安全与调用约束

| 对象 | 规则 |
|---|---|
| `stepper_*` setter | 只写 volatile，任意任务可调；ISR 中禁止（除读） |
| `i2c_bus_lock` 保护的序列 | 选通道 + 事务必须整体加锁，超时 20 ms |
| `lu9685_*` | 内部已加锁，可直接调用；失败返回 esp_err_t |
| `ads1115_get_latest` | 内部加锁，10~20 ms 超时；不要在 ISR 中调用 |
| `line_follow_set_params` | 内部临界区，可任意任务调用 |
| `chassis_set_*` | 只写 volatile，可任意任务/HTTP 调用 |
| `chassis_get_status` | 内部状态互斥锁，20 ms 超时 |
| Web 处理函数 | 在 httpd 任务中执行；可安全调用上述 API |

---

## 6. 典型调用示例

### 6.1 纯 C 手动控制（不用 Wi-Fi）

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

### 6.2 循迹

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

### 6.3 舵机定位与 360° 点动

```c
lu9685_set_angle(LU9685_CH_BASE, 135);                  // 位置舵机
lu9685_pulse_start(LU9685_CH_TRAY, 60, 500, -1);         // 360° 舵机正转 0.5s 后回中位
```

### 6.4 直接控制单轴（绕开底盘归一化）

```c
stepper_set_chassis_enable(true);
stepper_set_speed(AXIS_RR, 1200.0f);   // 1200 步/秒
stepper_stop(AXIS_RR);
```

### 6.5 读取探头并自行判定

```c
float v[8];
uint32_t seq;
if (ads1115_get_latest(v, &seq)) {
    bool black = line_follow_is_black(v[0]);   // 使用当前阈值/极性
}
```

---

## 7. 常见陷阱

1. **手动速度是"方向"不是 m/s**：`chassis_set_velocity` 会归一化到 `chassis_base_speed`。
   需要精确 m/s 时用 `mecanum_inverse` + `stepper_set_chassis_speeds`。
2. **默认阈值 4.00 V + 默认极性**：8 路可能全判为黑线。现场必须按实际白底/黑线电压校准。
3. **初始化顺序**：`stepper_init()` 必须最先，否则 EN 可能在上电瞬间保持低电平使电机发热。
4. **I2C 不加锁**：自己写 I2C 代码时漏掉 `i2c_bus_lock` 会在高频采样下偶发读错通道。
5. **LU9685 协议**：若定位无效且持续转动，先用 `/servo_proto?mode=1` 或 `2` 试；
   仍持续转动说明是 360° 连续舵机，改用 `lu9685_pulse_start`。
6. **看门狗**：HTTP 松键后前端会发 0；自写客户端必须每 <400 ms 刷新移动指令。
7. **EN 关闭**：会把目标速度清 0，重新使能后需要重新下发速度指令。
8. **方向反了**：优先调 `stepper_set_dir_invert(axis, true)`，不要改接线或公式。
9. **步频上限**：单轴 8000 步/秒；底盘航速上限 1500，留足加速余量。
10. **左前/左后 DIR 默认取反**：这是对置安装的机械补偿，若整车前进方向反了，
    应同时翻转四个轮子，而不是单独翻转一侧。

---

## 8. 单位与换算速查

| 物理量 | 单位 | 换算 |
|---|---|---|
| 轮速指令 | m/s | `steps = mps × 200 / (π × 0.065)` ≈ ×979.4 |
| 步频 | 步/秒 | 1 圈 = 200 步 = π×65mm ≈ 204.2 mm |
| 角速度 | rad/s | 轮速差 = `2 × k × ω`，`k = 0.175 m` |
| 舵机角度 | 0~180 | 脉宽模式：`500 + angle × 2000/180` µs |
| 采样电压 | V | `raw × 6.144 / 32768` |
| 阈值 | V | 0.0~5.0，前端步进 0.05 |

---

## 9. 源码索引

| 文件 | 内容 |
|---|---|
| `main/app_config.h` | 全部引脚、地址、机械与速度参数 |
| `main/stepper.c` | GPTimer ISR + DDS + 梯形斜坡 |
| `main/mecanum.c` | 逆运动学与单位换算 |
| `main/i2c_bus.c` | 总线、TCA9548A、互斥锁 |
| `main/ads1115.c` | 双 ADS 乒乓采样任务 |
| `main/lu9685.c` | 三协议舵机 + 点动定时任务 |
| `main/line_follow.c` | 加权 PID |
| `main/chassis.c` | 100Hz 控制环、模式、看门狗、速度归一化 |
| `main/wifi_sta.c` | STA 连接与重连 |
| `main/web_server.c` | HTTP 路由与 JSON |
| `main/web_page.h` | 内嵌控制台 |
| `README.md` | 接线、编译烧录、调试说明 |

---

*最后更新：2026-11-14　固件 0xDB3C0（877 KiB），ESP-IDF v6.1.0 编译 0 warning。*
