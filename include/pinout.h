/**
 * @file pinout.h
 * @brief M5Stack CoreS3 (ESP32-S3) 引脚定义
 *
 * 数据来源:
 *   - M5Stack 官方 CoreS3 文档 PinMap (docs.m5stack.com/en/core/CoreS3)
 *   - M5CoreS3 / M5Unified 库
 *
 * 注意:
 *   - 背光(LCD_BL)、LCD_RST、摄像头 RST 等引脚不直接连到 ESP32-S3 GPIO,
 *     而是通过 AXP2101 PMU / AW9523B IO 扩展器控制, 见文末注释。
 *   - 本项目为相机应用, GPIO47/48 已被摄像头 D7/D6 占用, 不可再作 USB 引脚。
 */
#pragma once

#include <Arduino.h>

/* -------------------------------------------------------------------------- *
 * 基础信息
 * -------------------------------------------------------------------------- */
#define CORES3_SOC            "ESP32-S3"
#define CORES3_FLASH_BYTES    (16 * 1024 * 1024UL)
#define CORES3_PSRAM_BYTES    (8 * 1024 * 1024UL)
#define CORES3_FREQ_HZ        240000000UL

/* -------------------------------------------------------------------------- *
 * I2C 总线
 * -------------------------------------------------------------------------- */
/* 板载内部 I2C (所有板载外设共用) */
#define I2C_SYS_SDA           GPIO_NUM_12
#define I2C_SYS_SCL           GPIO_NUM_11

/* 外部 I2C (M5 Bus PORT.A 引出) */
#define I2C_EXT_SDA           GPIO_NUM_2
#define I2C_EXT_SCL           GPIO_NUM_1

/* -------------------------------------------------------------------------- *
 * I2C 设备地址
 * -------------------------------------------------------------------------- */
#define AXP2101_ADDR          0x34   /* PMU 电源管理 */
#define AW9523B_ADDR          0x58   /* IO 扩展器 */
#define BM8563_ADDR           0x51   /* RTC */
#define BMI270_ADDR           0x69   /* 6 轴 IMU */
#define BMM150_ADDR           0x10   /* 3 轴磁力计 (经 BMI270 辅助 I2C) */
#define ES7210_ADDR           0x40   /* 双麦克风 ADC */
#define AW88298_ADDR          0x36   /* I2S 功放 */
#define FT6336U_ADDR          0x38   /* 电容触摸 */
#define GC0308_ADDR           0x21   /* 摄像头 */
#define LTR553_ADDR           0x23   /* 接近光感 */

/* -------------------------------------------------------------------------- *
 * LCD 显示屏 (ILI9342C, 320x240, SPI)
 * -------------------------------------------------------------------------- */
#define LCD_MOSI              GPIO_NUM_37
#define LCD_SCLK              GPIO_NUM_36
#define LCD_MISO              GPIO_NUM_35
#define LCD_CS                GPIO_NUM_3
#define LCD_DC                GPIO_NUM_35
/* LCD_RST 无直接 GPIO: AW9523B P1_1 控制 */
/* LCD_BL  无直接 GPIO: AXP2101 DLDO1 控制 */
#define LCD_WIDTH             320
#define LCD_HEIGHT            240
#define LCD_BACKLIGHT_ON      (1)    /* 经 AXP2101 DLDO1 使能背光 */

/* -------------------------------------------------------------------------- *
 * microSD 卡槽 (SPI)
 * -------------------------------------------------------------------------- */
#define SD_MISO               GPIO_NUM_35
#define SD_MOSI               GPIO_NUM_37
#define SD_SCLK               GPIO_NUM_36
#define SD_CS                 GPIO_NUM_4

/* -------------------------------------------------------------------------- *
 * 摄像头 GC0308 (0.3MP, DVP 并行接口)
 * -------------------------------------------------------------------------- */
#define CAM_SCCB_SDA          I2C_SYS_SDA
#define CAM_SCCB_SCL          I2C_SYS_SCL
#define CAM_PCLK              GPIO_NUM_45
#define CAM_VSYNC             GPIO_NUM_46
#define CAM_HREF              GPIO_NUM_38
#define CAM_D0                GPIO_NUM_39
#define CAM_D1                GPIO_NUM_40
#define CAM_D2                GPIO_NUM_41
#define CAM_D3                GPIO_NUM_42
#define CAM_D4                GPIO_NUM_15
#define CAM_D5                GPIO_NUM_16
#define CAM_D6                GPIO_NUM_48
#define CAM_D7                GPIO_NUM_47
/* CAM_XCLK / CAM_RESET / CAM_PWDN 未直连 GPIO:
 *   - XCLK  未连接 (-1)
 *   - RESET 由 AW9523B P1_0 控制
 *   - PWDN  未连接 (-1) */

/* -------------------------------------------------------------------------- *
 * 触摸屏 FT6336U (电容触摸, I2C)
 * -------------------------------------------------------------------------- */
#define TOUCH_SDA             I2C_SYS_SDA
#define TOUCH_SCL             I2C_SYS_SCL
/* TOUCH_RST 无直接 GPIO: AW9523B P0_0 控制 */
/* TOUCH_INT 无直接 GPIO: AW9523B P1_2 控制 */

/* -------------------------------------------------------------------------- *
 * 音频 (I2S): ES7210 麦克风 ADC + AW88298 功放
 * -------------------------------------------------------------------------- */
#define I2S_BCK               GPIO_NUM_34
#define I2S_WS                GPIO_NUM_33
#define I2S_MIC_DAT           GPIO_NUM_13   /* ES7210 输入 */
#define I2S_MCLK              GPIO_NUM_14   /* ES7210 主时钟 */
#define I2S_SPK_DAT           GPIO_NUM_0    /* AW88298 输出 */
/* AUDIO_RST 无直接 GPIO: AW9523B P0_2 控制 */
/* AUDIO_INT 无直接 GPIO: AW9523B P1_3 控制 */

/* -------------------------------------------------------------------------- *
 * M5 Bus / 外部端口
 * -------------------------------------------------------------------------- */
/* PORT.A (HY2.0-4P: GND / 5V / G2 / G1) */
#define PORT_A_SDA            GPIO_NUM_2
#define PORT_A_SCL            GPIO_NUM_1

/* PORT.B (HY2.0-4P: GND / 5V / G9 / G8) */
#define PORT_B_PIN            GPIO_NUM_9
#define PORT_B_ADC            GPIO_NUM_8

/* PORT.C (HY2.0-4P: GND / 5V / G17 / G18) */
#define PORT_C_TX             GPIO_NUM_17
#define PORT_C_RX             GPIO_NUM_18

/* 通用 GPIO */
#define GPIO_ADC              GPIO_NUM_10   /* M5 Bus ADC 输入 */
#define GPIO_PB_IN            GPIO_NUM_8    /* M5 Bus PB_IN */
#define GPIO_PB_OUT           GPIO_NUM_9    /* M5 Bus PB_OUT */
#define GPIO_FREE_5           GPIO_NUM_5
#define GPIO_FREE_6           GPIO_NUM_6
#define GPIO_FREE_7           GPIO_NUM_7
#define UART0_RXD             GPIO_NUM_44
#define UART0_TXD             GPIO_NUM_43
#define PC_TX                 GPIO_NUM_17
#define PC_RX                 GPIO_NUM_18

/* -------------------------------------------------------------------------- *
 * 按键
 * -------------------------------------------------------------------------- *
 * 注意: CoreS3 的按键不是普通 GPIO 直连可读的输入。
 * 请使用 M5Unified / M5CoreS3 库 API:
 *   - 电源键:  CoreS3.BtnPWR (M5.getButton(4))
 *   - 复位键:  硬件 RST, 长按 3s 进入下载模式
 * -------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------- *
 * AW9523B IO 扩展器 (I2C 0x58) 引脚映射参考
 * -------------------------------------------------------------------------- *
 *   P0_0  TOUCH_RST      P1_0  CAM_RST
 *   P0_1  (—)            P1_1  LCD_RST
 *   P0_2  AUDIO_RST      P1_2  TOUCH_INT
 *   P0_3  (—)            P1_3  AUDIO_INT
 *   P0_4  (—)            P1_4  (—)
 *   ...                  P1_5  (—)
 * -------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------- *
 * esp_camera 用配置示例 (DVP 并行接口)
 * 见 M5CoreS3 库 examples/Basic/camera
 * -------------------------------------------------------------------------- *
 *   pixformat      = PIXFORMAT_JPEG
 *   fb_location    = CAMERA_FB_IN_PSRAM
 *   xclk_freq_hz   = 20000000
 *   frame_size     = FRAMESIZE_VGA (640x480) 或更高
 *   pixel_format   = YUV422 / RGB565
 * -------------------------------------------------------------------------- */
