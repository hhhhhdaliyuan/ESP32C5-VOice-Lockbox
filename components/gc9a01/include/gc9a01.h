/**
 * @file gc9a01.h
 * @brief GC9A01 圆形 LCD 的控制、绘图、文字与图片显示接口。
 *
 * 坐标以左上角为原点，范围为 0..239；窗口坐标均为包含端点。
 * 所有像素颜色均采用 RGB565 格式。
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "board_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief LCD 可见区域宽度，单位为像素。 */
#define GC9A01_WIDTH  240U
/** @brief LCD 可见区域高度，单位为像素。 */
#define GC9A01_HEIGHT 240U

/** @name GC9A01 SPI 设备配置 */
/** @{ */
#define GC9A01_SPI_HOST               BOARD_GC9A01_SPI_HOST
#define GC9A01_SPI_SCLK_GPIO          BOARD_GC9A01_SPI_SCLK_GPIO
#define GC9A01_SPI_MOSI_GPIO          BOARD_GC9A01_SPI_MOSI_GPIO
#define GC9A01_SPI_MISO_GPIO          BOARD_GC9A01_SPI_MISO_GPIO
#define GC9A01_DC_GPIO                BOARD_GC9A01_DC_GPIO
#define GC9A01_SPI_CS_GPIO            BOARD_GC9A01_SPI_CS_GPIO
#define GC9A01_RST_GPIO               BOARD_GC9A01_RST_GPIO
#define GC9A01_SPI_CLOCK_HZ           60000000
#define GC9A01_SPI_MODE               0U
#define GC9A01_SPI_MAX_TRANSFER_BYTES (GC9A01_WIDTH * GC9A01_HEIGHT * 2U)
/** @} */

/**
 * @brief 公开显示 API 的并发约束。
 *
 * 所有 gc9a01_*() 公开接口会串行化执行，确保 DC 电平、GRAM 窗口和像素数据属于同一次
 * 操作，调用方可从多个任务使用这些接口；接口会等待互斥锁，因此不可从 ISR 调用。
 */

/** @name RGB565 颜色常量 */
/** @{ */
#define GC9A01_COLOR_BLACK     0x0000U
#define GC9A01_COLOR_WHITE     0xFFFFU
#define GC9A01_COLOR_RED       0xF800U
#define GC9A01_COLOR_GREEN     0x07E0U
#define GC9A01_COLOR_BLUE      0x001FU
#define GC9A01_COLOR_MAGENTA   0xF81FU
#define GC9A01_COLOR_YELLOW    0xFFE0U
#define GC9A01_COLOR_CYAN      0x07FFU
#define GC9A01_COLOR_BROWN     0xBC40U
#define GC9A01_COLOR_BRRED     0xFC07U
#define GC9A01_COLOR_GRAY      0x8430U
#define GC9A01_COLOR_DARKBLUE  0x01CFU
#define GC9A01_COLOR_LIGHTBLUE 0x7D7CU
#define GC9A01_COLOR_GRAYBLUE  0x5458U
#define GC9A01_COLOR_LIGHTGREEN 0x841FU
#define GC9A01_COLOR_LGRAY     0xC618U
#define GC9A01_COLOR_ORANGE    0xFD20U
/** @} */

/** @name GC9A01 前缀的简写颜色常量 */
/** @{ */
#define GC9A01_BLACK GC9A01_COLOR_BLACK
#define GC9A01_WHITE GC9A01_COLOR_WHITE
#define GC9A01_RED   GC9A01_COLOR_RED
#define GC9A01_GREEN GC9A01_COLOR_GREEN
#define GC9A01_BLUE  GC9A01_COLOR_BLUE
/** @} */

/**
 * @name 与参考工程兼容的 RGB565 颜色宏
 * @brief 保留无前缀名称，便于迁移已有显示业务代码。
 * @{
 */
#define WHITE      GC9A01_COLOR_WHITE
#define BLACK      GC9A01_COLOR_BLACK
#define RED        GC9A01_COLOR_RED
#define GREEN      GC9A01_COLOR_GREEN
#define BLUE       GC9A01_COLOR_BLUE
#define MAGENTA    GC9A01_COLOR_MAGENTA
#define YELLOW     GC9A01_COLOR_YELLOW
#define CYAN       GC9A01_COLOR_CYAN
#define BROWN      GC9A01_COLOR_BROWN
#define BRRED      GC9A01_COLOR_BRRED
#define GRAY       GC9A01_COLOR_GRAY
#define DARKBLUE   GC9A01_COLOR_DARKBLUE
#define LIGHTBLUE  GC9A01_COLOR_LIGHTBLUE
#define GRAYBLUE   GC9A01_COLOR_GRAYBLUE
#define LIGHTGREEN GC9A01_COLOR_LIGHTGREEN
#define LGRAY      GC9A01_COLOR_LGRAY
#define ORANGE     GC9A01_COLOR_ORANGE
/** @} */

/**
 * @brief 初始化 SPI 传输层和 GC9A01 控制器。
 *
 * 配置 DC、RST 引脚，执行硬件复位，并发送控制器初始化命令表。
 *
 * @return ESP_OK 初始化成功；其他值为底层 SPI 或 GPIO 驱动返回的错误码。
 */
esp_err_t gc9a01_init(void);

/**
 * @brief 设置后续 GRAM 写入的矩形窗口。
 *
 * 成功后控制器处于 RAM 写入状态，紧随其后的像素数据将写入此窗口。
 * 坐标包含 x1、y1 两个边界。
 *
 * @param x0 窗口左边界，范围 0..239。
 * @param y0 窗口上边界，范围 0..239。
 * @param x1 窗口右边界，范围 0..239，且不得小于 x0。
 * @param y1 窗口下边界，范围 0..239，且不得小于 y0。
 * @return ESP_OK 设置成功；ESP_ERR_INVALID_ARG 表示窗口越界或边界顺序无效；
 *         其他值为底层 SPI/GPIO 错误码。
 */
esp_err_t gc9a01_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/**
 * @brief 用指定颜色填充整块屏幕。
 *
 * @param color RGB565 颜色值。
 * @return ESP_OK 填充成功；其他值为底层错误码。
 */
esp_err_t gc9a01_fill(uint16_t color);

/**
 * @brief 用指定颜色填充一个矩形区域。
 *
 * 坐标包含 x1、y1 两个边界。
 *
 * @param x0 矩形左边界，范围 0..239。
 * @param y0 矩形上边界，范围 0..239。
 * @param x1 矩形右边界，范围 0..239，且不得小于 x0。
 * @param y1 矩形下边界，范围 0..239，且不得小于 y0。
 * @param color RGB565 填充颜色。
 * @return ESP_OK 填充成功；ESP_ERR_INVALID_ARG 表示矩形无效；其他值为底层错误码。
 */
esp_err_t gc9a01_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);

/**
 * @brief 绘制一个像素点。
 *
 * @param x 像素横坐标，范围 0..239。
 * @param y 像素纵坐标，范围 0..239。
 * @param color RGB565 像素颜色。
 * @return ESP_OK 绘制成功；ESP_ERR_INVALID_ARG 表示坐标越界；其他值为底层错误码。
 */
esp_err_t gc9a01_draw_pixel(uint16_t x, uint16_t y, uint16_t color);

/**
 * @brief 在 8x16 字符格中绘制一个 ASCII 字符。
 *
 * 文本位置采用 1-based 格位：line 为 1..15，column 为 1..30。
 *
 * @param line 字符行号，范围 1..15。
 * @param column 字符列号，范围 1..30。
 * @param c 待显示字符，仅支持 ASCII 0x20..0x7E。
 * @param fg 前景色，RGB565 格式。
 * @param bg 背景色，RGB565 格式。
 * @return ESP_OK 绘制成功；ESP_ERR_INVALID_ARG 表示格位或字符无效；其他值为底层错误码。
 */
esp_err_t gc9a01_draw_char(uint8_t line, uint8_t column, char c, uint16_t fg, uint16_t bg);

/**
 * @brief 在 8x16 字符格中绘制一个 ASCII 字符串。
 *
 * 函数会先验证整个字符串和所有目标格位，参数无效时不会产生部分显示。
 *
 * @param line 起始行号，范围 1..15。
 * @param column 起始列号，范围 1..30。
 * @param text 以 NUL 结尾的非空 ASCII 字符串。
 * @param fg 前景色，RGB565 格式。
 * @param bg 背景色，RGB565 格式。
 * @return ESP_OK 绘制成功；ESP_ERR_INVALID_ARG 表示指针、字符或显示区域无效；
 *         其他值为底层错误码。
 */
esp_err_t gc9a01_draw_string(uint8_t line, uint8_t column, const char *text, uint16_t fg, uint16_t bg);

/**
 * @brief 以十进制定宽、前导零形式显示无符号整数。
 *
 * 例如 num=42、len=4 时显示 "0042"。
 *
 * @param line 起始行号，范围 1..15。
 * @param column 起始列号，范围 1..30。
 * @param num 待显示数值；其十进制位数不得超过 len。
 * @param len 显示宽度，范围 1..10。
 * @param fg 前景色，RGB565 格式。
 * @param bg 背景色，RGB565 格式。
 * @return ESP_OK 显示成功；ESP_ERR_INVALID_ARG 表示字段宽度、数值或显示区域无效；
 *         其他值为底层错误码。
 */
esp_err_t gc9a01_show_num(uint8_t line, uint8_t column, uint32_t num, uint8_t len, uint16_t fg, uint16_t bg);

/**
 * @brief 以十六进制定宽、前导零形式显示无符号整数。
 *
 * 十六进制字母使用大写 A..F。
 *
 * @param line 起始行号，范围 1..15。
 * @param column 起始列号，范围 1..30。
 * @param num 待显示数值；其十六进制位数不得超过 len。
 * @param len 显示宽度，范围 1..8。
 * @param fg 前景色，RGB565 格式。
 * @param bg 背景色，RGB565 格式。
 * @return ESP_OK 显示成功；ESP_ERR_INVALID_ARG 表示字段宽度、数值或显示区域无效；
 *         其他值为底层错误码。
 */
esp_err_t gc9a01_show_hexnum(uint8_t line, uint8_t column, uint32_t num, uint8_t len, uint16_t fg, uint16_t bg);

/**
 * @brief 以固定整数位和小数位显示非负浮点数。
 *
 * 小数部分会四舍五入；四舍五入产生进位时，进位会累加到整数部分。
 *
 * @param line 起始行号，范围 1..15。
 * @param column 起始列号，范围 1..30。
 * @param num 待显示的有限非负浮点数，必须小于 4294967296.0f，避免转换 uint32_t 溢出。
 * @param int_len 整数部分宽度，范围 1..10。
 * @param dec_len 小数部分宽度，范围 1..9。
 * @param fg 前景色，RGB565 格式。
 * @param bg 背景色，RGB565 格式。
 * @return ESP_OK 显示成功；ESP_ERR_INVALID_ARG 表示数值、字段宽度或显示区域无效；
 *         其他值为底层错误码。
 */
esp_err_t gc9a01_show_float(uint8_t line, uint8_t column, float num, uint8_t int_len, uint8_t dec_len,
                             uint16_t fg, uint16_t bg);

/**
 * @brief 显示一张完整的 240x240 RGB565 图片。
 *
 * 图片数据按每像素高字节在前的顺序排列，调用方必须提供至少 115200 字节。
 *
 * @param image 指向完整 RGB565 大端字节流的非空指针。
 * @return ESP_OK 显示成功；ESP_ERR_INVALID_ARG 表示 image 为空；其他值为底层错误码。
 */
esp_err_t gc9a01_show_picture(const uint8_t *image);

#ifdef __cplusplus
}
#endif
