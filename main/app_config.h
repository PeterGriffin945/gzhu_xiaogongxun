/**
 * @file app_config.h
 * @brief 工创赛麦克纳姆轮小车 —— 全局硬件配置与机械参数
 *
 * 所有引脚、I2C 地址、机械尺寸集中在此处，修改硬件只需改这一个文件。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * 一、步进电机 (5 × DRV8825，整步模式)
 * ========================================================================== */
#define PIN_M1_RR_STEP   3      /* 电机1 右后轮 STEP */
#define PIN_M1_RR_DIR    46
#define PIN_M2_RF_STEP   17     /* 电机2 右前轮 STEP */
#define PIN_M2_RF_DIR    18
#define PIN_M3_LF_STEP   12     /* 电机3 左前轮 STEP */
#define PIN_M3_LF_DIR    11
#define PIN_M4_LR_STEP   14     /* 电机4 左后轮 STEP */
#define PIN_M4_LR_DIR    13
#define PIN_M5_LIFT_STEP 15     /* 电机5 机械臂升降 STEP */
#define PIN_M5_LIFT_DIR  16

/* EN: 低电平使能(锁轴)，高电平脱机释放。上电默认拉高。 */
#define PIN_EN_CHASSIS   42     /* 底盘 M1~M4 */
#define PIN_EN_LIFT      41     /* 升降 M5 */

/* ==========================================================================
 * 二、I2C 总线 / TCA9548A / ADS1115 / LU9685
 * ========================================================================== */
#define PIN_I2C_SDA       8
#define PIN_I2C_SCL       9
#define I2C_FREQ_HZ       400000
#define I2C_TIMEOUT_MS    20

#define PIN_TCA_RESET     40
#define TCA9548A_ADDR     0x70

#define TCA_CH_ADS_5678   0    /* 第 1 块 ADS1115：探头 5~8 */
#define TCA_CH_ADS_1234   1    /* 第 2 块 ADS1115：探头 1~4 */
#define TCA_CH_SERVO      2    /* LU9685 舵机板 */

#define ADS_ADDR_PROBE5678 0x48
#define ADS_ADDR_PROBE1234 0x49

#define LU9685_ADDR_A      0x40   /* 常见地址 */
#define LU9685_ADDR_B      0x00   /* 兼容地址，初始化时自动探测 */

#define LU9685_CH_BASE     0      /* 底座旋转舵机 20kg */
#define LU9685_CH_TRAY     4      /* 物料盘旋转舵机 SG90 */
#define LU9685_CH_GRIPPER  8      /* 夹爪开合舵机 20kg */

/* K230D PORT2：UART1 重映射到已确认的 UART0 排针，控制台需迁至原生 USB。 */
#define K230_UART_PORT      1
#define PIN_K230_TX         43
#define PIN_K230_RX         44
#define K230_UART_BAUD      115200
#define K230_LINK_TIMEOUT_MS 1500
#define K230_RESULT_TIMEOUT_MS 400

/* ==========================================================================
 * 三、步进细分与脉冲发生器 (DDS) 参数
 *
 * DRV8825 已通过 MS1/MS2/MS3 跳线配置为 1/16 细分（纯硬件，不占 GPIO）。
 * 注意：所有“步/秒、步数、加速度”从此都按【微步】计，数值是整步的 16 倍。
 * 以后改回整步只需把 MICROSTEP 改为 1，下面的派生值会自动缩放。
 * ========================================================================== */
#define WHEEL_MICROSTEP         16          /* M1~M4 底盘轮：DRV8825 1/16 */
#define LIFT_MICROSTEP          16          /* M5 升降：同样 1/16；整步则改 1 */

#define STEPPER_TICK_HZ         20000       /* 50us 硬件定时器 */
#define STEPPER_TICK_US         50
/* ISR 每 tick 最多发一个脉冲，脉冲率硬上限 10kHz，故限速取 10000。 */
#define STEPPER_MAX_SPEED       10000.0f    /* 单轴限速：微步/秒 */
#define STEPPER_MAX_ACCEL       100000.0f
#define STEPPER_MIN_ACCEL       (100.0f * WHEEL_MICROSTEP)   /* 1600 微步/秒² */
#define STEPPER_RAMP_PERIOD_MS  1           /* 速度斜坡更新周期 */

/* 起步速度：整步基准 60 步/秒（0.3 转/秒）→ 16 细分下 960 微步/秒。
   细分后低速共振大幅减小，不再需要整步时代那种 1 转/秒的起步跳变。 */
#define STEPPER_START_SPEED_BASE    60.0f
#define STEPPER_START_SPEED         (STEPPER_START_SPEED_BASE * WHEEL_MICROSTEP)
#define STEPPER_START_SPEED_LIFT    (STEPPER_START_SPEED_BASE * LIFT_MICROSTEP)

/* 开环定步（Z 轴/平移）的起步速度：整步基准 40 步/秒 */
#define POSITION_START_SPEED_BASE   40.0f
#define POSITION_START_SPEED        (POSITION_START_SPEED_BASE * WHEEL_MICROSTEP)
#define POSITION_START_SPEED_LIFT   (POSITION_START_SPEED_BASE * LIFT_MICROSTEP)

/* 默认加速度：整步 1200 → 16 细分 19200 微步/秒²（物理加速度不变） */
#define STEPPER_DEFAULT_ACCEL   (1200.0f * WHEEL_MICROSTEP)

/* ==========================================================================
 * 四、机械参数 (麦轮逆运动学)
 *   轮径 / 轴距 / 轮距请按实车测量后修改
 * ========================================================================== */
#define WHEEL_DIAMETER_MM       65.0f
#define WHEEL_STEPS_PER_REV     (200 * WHEEL_MICROSTEP)   /* 1.8° 电机 × 1/16 细分 = 3200 */
#define WHEELBASE_MM            160.0f      /* 前后轴距 */
#define TRACK_MM                190.0f      /* 左右轮距 */

#define CHASSIS_MAX_LINEAR_V    2.0f        /* 命令限制 m/s */
#define CHASSIS_MAX_OMEGA       8.0f        /* 命令限制 rad/s */
#define MANUAL_CMD_TIMEOUT_MS   400         /* 遥控指令看门狗 */
#define LIFT_JOG_TIMEOUT_MS     500         /* 升降点动看门狗 */

/* 运行时可调的基础转速（单位：微步/秒，16 细分）
 * 手动遥控 / 循迹基础航速统一读取 chassis 值，升降轴读取 lift 值。
 *
 * 实车工作点主要在整步 150 步/秒以下（16 细分 = 2400 微步/秒）：
 *   底盘默认 150 整步/秒，范围 50~300 整步/秒（800~4800 微步/秒）；
 *   升降默认 400 整步/秒（6400 微步/秒），保证比赛取放动作不超过 6 分钟。 */
#define CHASSIS_BASE_SPEED_DEFAULT  (150.0f * WHEEL_MICROSTEP)   /* 2400 */
#define CHASSIS_BASE_SPEED_MIN      (50.0f * WHEEL_MICROSTEP)    /* 800 */
#define CHASSIS_BASE_SPEED_MAX      (300.0f * WHEEL_MICROSTEP)   /* 4800 */
#define LIFT_SPEED_DEFAULT          (400.0f * LIFT_MICROSTEP)    /* 6400 */
#define LIFT_SPEED_MIN              (100.0f * LIFT_MICROSTEP)    /* 1600 */
#define LIFT_SPEED_MAX              STEPPER_MAX_SPEED            /* 10000 */

/* 底座 CH0 保留的速度角/行程配置，仅用于历史兼容（位置模式不再使用）。 */
#define ARM_BASE_SPEED_ANGLE_DEFAULT 60
#define ARM_BASE_SWEEP_MS_DEFAULT    2400U

/* 舵机类型与行程（逻辑角度，MG996 类位置舵机）
 * CH0 底座 20kg：0~360° 位置控制，最大一圈，超出软限位拒绝；
 * CH8 夹爪 20kg：0~180°，软限位由标定的开/合角度自动收紧；
 * CH4 物料盘 SG90：继续用连续旋转 + 定时分度（托盘逻辑依赖）。
 * 每次步进命令默认 15°，可在网页调整。 */
#define SERVO_BASE_TRAVEL_DEG     360.0f
#define SERVO_BASE_MIN_DEG        0.0f
#define SERVO_BASE_MAX_DEG        360.0f
#define SERVO_GRIPPER_TRAVEL_DEG  180.0f
#define SERVO_STEP_DEG_DEFAULT    15.0f
/* 单次定时点动最长持续时间：无论标定值多大，到点必定停止 */
#define SERVO_MAX_JOG_MS          10000U

/* ==========================================================================
 * 五、传感器采样
 * ========================================================================== */
#define ADS_CONV_TIME_US        1250        /* 860SPS 单次转换 ≈ 1163us，留余量 */
#define ADS_REFRESH_MS          12          /* 8 路全刷新周期（扫描本身约 6~8ms） */
#define LINE_SENSOR_COUNT       8

/* ==========================================================================
 * 六、Wi-Fi
 * ========================================================================== */
#define WIFI_SSID   "哇哇哇哇哇"
#define WIFI_PASS   "13532535322"
#define WIFI_MAX_RETRY 0     /* 0 = 无限重连 */

/* 兜底 AP：路由连不上时，手机可连这个热点访问 http://192.168.4.1/ */
#define WIFI_AP_SSID  "mecanum-car"
#define WIFI_AP_PASS  "mecanum123"

#ifdef __cplusplus
}
#endif
