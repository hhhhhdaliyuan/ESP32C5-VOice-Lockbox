#include "gc9a01.h"
#include "font8x16.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "common_spi.h"

/* 设备句柄绑定 GC9A01 的 CS；其他 SPI 设备可在同一总线上使用各自的句柄。 */
static common_spi_device_t s_spi_device;
/* 保护 DC 状态、GRAM 窗口和三个静态像素缓冲，确保一次公开绘制不会被其他任务插入。 */
static StaticSemaphore_t s_api_mutex_storage;
static SemaphoreHandle_t s_api_mutex;
static portMUX_TYPE s_api_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;

/* 字模库的单字尺寸决定了可显示 30 列、15 行文本。 */
#define GC9A01_CHAR_WIDTH  8U
#define GC9A01_CHAR_HEIGHT 16U
#define GC9A01_TEXT_COLUMNS (GC9A01_WIDTH / GC9A01_CHAR_WIDTH)
#define GC9A01_TEXT_LINES   (GC9A01_HEIGHT / GC9A01_CHAR_HEIGHT)

typedef struct {
    /* 一条初始化记录：先发 command，再按 length 发送 data。 */
    uint8_t command;
    uint8_t length;
    uint8_t data[16];
} gc9a01_init_command_t;

/* 所有 *_unlocked() 仅供已获取 s_api_mutex 的公开 API 包装函数调用。 */
static esp_err_t gc9a01_set_window_unlocked(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

static esp_err_t gc9a01_api_lock(void)
{
    /* 静态互斥锁无需动态堆内存；临界区保证多任务首次调用时只初始化一次。 */
    taskENTER_CRITICAL(&s_api_mutex_init_lock);
    if (!s_api_mutex) {
        s_api_mutex = xSemaphoreCreateMutexStatic(&s_api_mutex_storage);
    }
    SemaphoreHandle_t mutex = s_api_mutex;
    taskEXIT_CRITICAL(&s_api_mutex_init_lock);

    if (!mutex) {
        return ESP_ERR_NO_MEM;
    }
    return xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_FAIL;
}

static void gc9a01_api_unlock(void)
{
    /* 每个公开 API 只在成功获取锁后调用此函数，释放后下一次绘制才可切换 DC 或 GRAM 窗口。 */
    xSemaphoreGive(s_api_mutex);
}

/*
 * GC9A01 推荐初始化寄存器配置，来自参考工程。
 * Sleep Out（0x11）和 Display On（0x29）带有明确时序要求，因此在 init() 中单独发送。
 */
static const gc9a01_init_command_t s_init_commands[] = {
    {0xFE, 0, {0}}, {0xEF, 0, {0}}, {0xEB, 1, {0x14}}, {0x84, 1, {0x40}},
    {0x85, 1, {0xF1}}, {0x86, 1, {0x98}}, {0x87, 1, {0x28}}, {0x88, 1, {0x0A}},
    {0x89, 1, {0x21}}, {0x8A, 1, {0x00}}, {0x8B, 1, {0x80}}, {0x8C, 1, {0x01}},
    {0x8D, 1, {0x00}}, {0x8E, 1, {0xDF}}, {0x8F, 1, {0x52}}, {0xB6, 1, {0x20}},
    {0x36, 1, {0x48}}, {0x3A, 1, {0x05}}, {0x90, 4, {0x08, 0x08, 0x08, 0x08}},
    {0xBD, 1, {0x06}}, {0xBF, 1, {0x1C}}, {0xA7, 1, {0x45}}, {0xA9, 1, {0xBB}},
    {0xB8, 1, {0x63}}, {0xBC, 1, {0x00}}, {0xFF, 3, {0x60, 0x01, 0x04}},
    {0xC3, 1, {0x17}}, {0xC4, 1, {0x17}}, {0xC9, 1, {0x25}}, {0xBE, 1, {0x11}},
    {0xE1, 2, {0x10, 0x0E}}, {0xDF, 3, {0x21, 0x10, 0x02}},
    {0xF0, 6, {0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}},
    {0xF1, 6, {0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}},
    {0xF2, 6, {0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}},
    {0xF3, 6, {0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}},
    {0xED, 2, {0x1B, 0x0B}}, {0xAC, 1, {0x47}}, {0xAE, 1, {0x77}},
    {0xCB, 1, {0x02}}, {0xCD, 1, {0x63}},
    {0x70, 9, {0x07, 0x09, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03}},
    {0xE8, 1, {0x34}}, {0x62, 12, {0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70}},
    {0x63, 12, {0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70}},
    {0x64, 7, {0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07}},
    {0x66, 10, {0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00}},
    {0x67, 10, {0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98}},
    {0x74, 7, {0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00}}, {0x35, 0, {0}}, {0x21, 0, {0}},
};

static esp_err_t gc9a01_write_command(uint8_t command)
{
    /* DC=0 表示本次 SPI 字节是控制器命令。GPIO 设置失败时不继续传输。 */
    esp_err_t ret = gpio_set_level(BOARD_GC9A01_DC_GPIO, 0);
    if (ret != ESP_OK) {
        return ret;
    }
    return common_spi_write(&s_spi_device, &command, 1);
}

static esp_err_t gc9a01_write_data(const void *data, size_t length)
{
    /* DC=1 表示本次 SPI 字节是命令参数或 GRAM 像素数据。 */
    esp_err_t ret = gpio_set_level(BOARD_GC9A01_DC_GPIO, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    return common_spi_write(&s_spi_device, data, length);
}

static bool gc9a01_window_valid(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    /* 窗口端点均为包含端点，因此只需保证右下角在屏幕内。 */
    return x0 <= x1 && y0 <= y1 && x1 < GC9A01_WIDTH && y1 < GC9A01_HEIGHT;
}

static bool gc9a01_text_region_valid(uint8_t line, uint8_t column, uint8_t width)
{
    /* 文本接口使用 1-based 格位，width 个字符必须全部落在一行内。 */
    return line >= 1 && line <= GC9A01_TEXT_LINES && column >= 1 && width > 0 &&
           width <= GC9A01_TEXT_COLUMNS && column <= GC9A01_TEXT_COLUMNS - width + 1;
}

static bool gc9a01_ascii_valid(char c)
{
    /* font8x16 仅包含可打印 ASCII，不能用 char 的负值索引字模。 */
    return c >= 0x20 && c <= 0x7E;
}

static bool gc9a01_pow_valid(uint8_t base, uint8_t exponent, uint32_t *result)
{
    uint32_t value = 1;
    for (uint8_t i = 0; i < exponent; i++) {
        /* 先判断乘法是否溢出，避免数值字段校验得到错误结果。 */
        if (value > UINT32_MAX / base) {
            return false;
        }
        value *= base;
    }
    *result = value;
    return true;
}

static esp_err_t gc9a01_draw_char_unchecked(uint8_t line, uint8_t column, char c, uint16_t fg, uint16_t bg)
{
    /* 静态缓冲区复用以避免每次绘制字符都占用任务栈；该函数不支持并发调用。 */
    static uint8_t glyph[GC9A01_CHAR_WIDTH * GC9A01_CHAR_HEIGHT * 2];
    const uint8_t *source = font8x16[(uint8_t)c - 0x20];
    for (uint8_t row = 0; row < GC9A01_CHAR_HEIGHT; row++) {
        for (uint8_t col = 0; col < GC9A01_CHAR_WIDTH; col++) {
            /* 字模每行 bit7 对应最左像素，SPI 像素按 RGB565 高字节在前发送。 */
            uint16_t color = (source[row] & (0x80U >> col)) ? fg : bg;
            size_t offset = ((size_t)row * GC9A01_CHAR_WIDTH + col) * 2;
            glyph[offset] = (uint8_t)(color >> 8);
            glyph[offset + 1] = (uint8_t)color;
        }
    }

    /* 将 1-based 文本格位转换为 0-based 像素窗口。 */
    uint16_t x = (uint16_t)(column - 1) * GC9A01_CHAR_WIDTH;
    uint16_t y = (uint16_t)(line - 1) * GC9A01_CHAR_HEIGHT;
    esp_err_t ret = gc9a01_set_window_unlocked(x, y, x + GC9A01_CHAR_WIDTH - 1,
                                                y + GC9A01_CHAR_HEIGHT - 1);
    if (ret != ESP_OK) {
        return ret;
    }
    return gc9a01_write_data(glyph, sizeof(glyph));
}

static esp_err_t gc9a01_init_unlocked(void)
{
    /* 先初始化共享总线；其他屏幕可使用相同 SCLK/MOSI/MISO 配置重复调用。 */
    const common_spi_bus_config_t bus_config = {
        .host = BOARD_GC9A01_SPI_HOST,
        .mosi_io_num = BOARD_GC9A01_SPI_MOSI_GPIO,
        .miso_io_num = BOARD_GC9A01_SPI_MISO_GPIO,
        .sclk_io_num = BOARD_GC9A01_SPI_SCLK_GPIO,
        .max_transfer_bytes = GC9A01_SPI_MAX_TRANSFER_BYTES,
    };
    esp_err_t ret = common_spi_bus_init(&bus_config);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!s_spi_device.handle) {
        /* GC9A01 使用独立 CS=GPIO12，因此不会接收其他 SPI 设备的事务。 */
        const common_spi_device_config_t device_config = {
            .cs_io_num = BOARD_GC9A01_SPI_CS_GPIO,
            .clock_speed_hz = GC9A01_SPI_CLOCK_HZ,
            .mode = GC9A01_SPI_MODE,
            .max_transfer_bytes = GC9A01_SPI_MAX_TRANSFER_BYTES,
        };
        ret = common_spi_device_add(BOARD_GC9A01_SPI_HOST, &device_config, &s_spi_device);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    /* DC 与 RST 只由控制器层管理，SPI 传输层不配置这两个 GPIO。 */
    gpio_config_t control_gpio_config = {
        .pin_bit_mask = (1ULL << BOARD_GC9A01_DC_GPIO) | (1ULL << BOARD_GC9A01_RST_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&control_gpio_config);
    if (ret != ESP_OK) {
        return ret;
    }

    /* 按参考时序执行高-低-高硬件复位，随后等待控制器完成内部启动。 */
    ret = gpio_set_level(BOARD_GC9A01_RST_GPIO, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    ret = gpio_set_level(BOARD_GC9A01_RST_GPIO, 0);
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    ret = gpio_set_level(BOARD_GC9A01_RST_GPIO, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(120));

    /* 每条记录严格按“命令 -> 可选参数”的顺序发送，任一传输失败立即返回。 */
    for (size_t i = 0; i < sizeof(s_init_commands) / sizeof(s_init_commands[0]); i++) {
        ret = gc9a01_write_command(s_init_commands[i].command);
        if (ret != ESP_OK) {
            return ret;
        }
        if (s_init_commands[i].length != 0) {
            ret = gc9a01_write_data(s_init_commands[i].data, s_init_commands[i].length);
            if (ret != ESP_OK) {
                return ret;
            }
        }
    }

    /* Sleep Out 后必须等待控制器退出休眠，再开启显示并进入 GRAM 写入状态。 */
    vTaskDelay(pdMS_TO_TICKS(120));
    ret = gc9a01_write_command(0x11);
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(120));
    ret = gc9a01_write_command(0x29);
    if (ret != ESP_OK) {
        return ret;
    }
    return gc9a01_write_command(0x2C);
}

static esp_err_t gc9a01_set_window_unlocked(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    /* 先完整校验，防止无效窗口已发送部分列/行地址命令。 */
    if (!gc9a01_window_valid(x0, y0, x1, y1)) {
        return ESP_ERR_INVALID_ARG;
    }
    /* 0x2A 设置列地址，控制器使用 16-bit 大端起止坐标。 */
    uint8_t window[4] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
    esp_err_t ret = gc9a01_write_command(0x2A);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gc9a01_write_data(window, sizeof(window));
    if (ret != ESP_OK) {
        return ret;
    }
    /* 0x2B 设置行地址，复用同一缓冲区装载 y 坐标。 */
    window[0] = (uint8_t)(y0 >> 8);
    window[1] = (uint8_t)y0;
    window[2] = (uint8_t)(y1 >> 8);
    window[3] = (uint8_t)y1;
    ret = gc9a01_write_command(0x2B);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gc9a01_write_data(window, sizeof(window));
    if (ret != ESP_OK) {
        return ret;
    }
    /* 0x2C 使后续数据字节直接写入刚设置的 GRAM 窗口。 */
    return gc9a01_write_command(0x2C);
}

static esp_err_t gc9a01_fill_rect_unlocked(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                                            uint16_t color)
{
    if (!gc9a01_window_valid(x0, y0, x1, y1)) {
        return ESP_ERR_INVALID_ARG;
    }
    /* 仅构造一行像素并重复发送，避免为整块矩形分配大缓冲区。 */
    static uint8_t line[GC9A01_WIDTH * 2];
    uint16_t width = x1 - x0 + 1;
    for (uint16_t i = 0; i < width; i++) {
        line[i * 2] = (uint8_t)(color >> 8);
        line[i * 2 + 1] = (uint8_t)color;
    }
    esp_err_t ret = gc9a01_set_window_unlocked(x0, y0, x1, y1);
    if (ret != ESP_OK) {
        return ret;
    }
    /* 该窗口已由 set_window() 激活，每次写入恰好推进一行 GRAM 地址。 */
    for (uint16_t row = y0; row <= y1; row++) {
        ret = gc9a01_write_data(line, width * 2);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    return ESP_OK;
}

static esp_err_t gc9a01_fill_unlocked(uint16_t color)
{
    /* 全屏填充复用矩形填充路径，保证像素字节序和错误处理一致。 */
    return gc9a01_fill_rect_unlocked(0, 0, GC9A01_WIDTH - 1, GC9A01_HEIGHT - 1, color);
}

static esp_err_t gc9a01_draw_pixel_unlocked(uint16_t x, uint16_t y, uint16_t color)
{
    /* set_window() 同时承担单点坐标边界检查。 */
    uint8_t pixel[2] = {(uint8_t)(color >> 8), (uint8_t)color};
    esp_err_t ret = gc9a01_set_window_unlocked(x, y, x, y);
    if (ret != ESP_OK) {
        return ret;
    }
    return gc9a01_write_data(pixel, sizeof(pixel));
}

static esp_err_t gc9a01_draw_char_unlocked(uint8_t line, uint8_t column, char c, uint16_t fg,
                                            uint16_t bg)
{
    /* unchecked 版本直接索引字模，因此必须先验证格位和 ASCII 范围。 */
    if (!gc9a01_text_region_valid(line, column, 1) || !gc9a01_ascii_valid(c)) {
        return ESP_ERR_INVALID_ARG;
    }
    return gc9a01_draw_char_unchecked(line, column, c, fg, bg);
}

static esp_err_t gc9a01_draw_string_unlocked(uint8_t line, uint8_t column, const char *text,
                                              uint16_t fg, uint16_t bg)
{
    if (!text) {
        return ESP_ERR_INVALID_ARG;
    }
    /* 先验证整段字符串与剩余格位，避免某个非法字符造成部分显示。 */
    size_t length = strlen(text);
    if (length == 0 || length > UINT8_MAX || !gc9a01_text_region_valid(line, column, (uint8_t)length)) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < length; i++) {
        if (!gc9a01_ascii_valid(text[i])) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    /* 参数已全部确认有效，逐字符写入各自的 8x16 像素窗口。 */
    for (uint8_t i = 0; i < (uint8_t)length; i++) {
        esp_err_t ret = gc9a01_draw_char_unchecked(line, column + i, text[i], fg, bg);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    return ESP_OK;
}

static esp_err_t gc9a01_show_num_unlocked(uint8_t line, uint8_t column, uint32_t num, uint8_t len,
                                           uint16_t fg, uint16_t bg)
{
    uint32_t limit;
    if (!gc9a01_text_region_valid(line, column, len) || len > 10 ||
        (len < 10 && (!gc9a01_pow_valid(10, len, &limit) || num >= limit))) {
        return ESP_ERR_INVALID_ARG;
    }
    /* 按最高位到最低位取数字，因此不足 len 位时自然显示前导零。 */
    for (uint8_t i = 0; i < len; i++) {
        uint32_t divisor;
        gc9a01_pow_valid(10, len - i - 1, &divisor);
        esp_err_t ret = gc9a01_draw_char_unchecked(line, column + i, (char)('0' + (num / divisor) % 10), fg, bg);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    return ESP_OK;
}

static esp_err_t gc9a01_show_hexnum_unlocked(uint8_t line, uint8_t column, uint32_t num, uint8_t len,
                                              uint16_t fg, uint16_t bg)
{
    uint32_t limit;
    if (!gc9a01_text_region_valid(line, column, len) || len > 8 ||
        (len < 8 && (!gc9a01_pow_valid(16, len, &limit) || num >= limit))) {
        return ESP_ERR_INVALID_ARG;
    }
    /* 与十进制路径相同，从高位开始取值，并将 10..15 转为大写 A..F。 */
    for (uint8_t i = 0; i < len; i++) {
        uint32_t divisor;
        gc9a01_pow_valid(16, len - i - 1, &divisor);
        uint8_t digit = (num / divisor) % 16;
        char c = digit < 10 ? (char)('0' + digit) : (char)('A' + digit - 10);
        esp_err_t ret = gc9a01_draw_char_unchecked(line, column + i, c, fg, bg);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    return ESP_OK;
}

static esp_err_t gc9a01_show_float_unlocked(uint8_t line, uint8_t column, float num, uint8_t int_len,
                                             uint8_t dec_len, uint16_t fg, uint16_t bg)
{
    uint32_t scale;
    uint32_t integer_limit;
    uint16_t width = (uint16_t)int_len + dec_len + 1;
    /* 2^32 是 float 可精确表示的首个超出 uint32_t 范围的值，必须在强制转换前拒绝。 */
    if (!isfinite(num) || num < 0.0f || int_len == 0 || int_len > 10 || dec_len == 0 || dec_len > 9 ||
        width > UINT8_MAX || !gc9a01_text_region_valid(line, column, (uint8_t)width) ||
        !gc9a01_pow_valid(10, dec_len, &scale) || num >= 4294967296.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    /* 所有约束均在首次显示前检查，避免整数、小数仅显示一部分。 */
    uint32_t integer = (uint32_t)num;
    uint32_t fraction = (uint32_t)((num - integer) * scale + 0.5f);
    /* 例如 1.999 显示两位小数时，小数四舍五入为 100，需要向整数部分进位。 */
    if (fraction == scale) {
        integer++;
        fraction = 0;
    }
    if (int_len < 10 && (!gc9a01_pow_valid(10, int_len, &integer_limit) || integer >= integer_limit)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = gc9a01_show_num_unlocked(line, column, integer, int_len, fg, bg);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gc9a01_draw_char_unchecked(line, column + int_len, '.', fg, bg);
    if (ret != ESP_OK) {
        return ret;
    }
    return gc9a01_show_num_unlocked(line, column + int_len + 1, fraction, dec_len, fg, bg);
}

static esp_err_t gc9a01_show_picture_unlocked(const uint8_t *image)
{
    if (!image) {
        return ESP_ERR_INVALID_ARG;
    }
    /* 每次发送 8 行（3840 字节），低于底层 SPI 单次传输上限。 */
    static uint8_t chunk[GC9A01_WIDTH * 2 * 8];
    esp_err_t ret = gc9a01_set_window_unlocked(0, 0, GC9A01_WIDTH - 1, GC9A01_HEIGHT - 1);
    if (ret != ESP_OK) {
        return ret;
    }
    /* 先复制到内部 RAM 缓冲区，兼容图片数据位于只读存储区或外部存储区的场景。 */
    for (size_t offset = 0; offset < GC9A01_WIDTH * GC9A01_HEIGHT * 2; offset += sizeof(chunk)) {
        memcpy(chunk, image + offset, sizeof(chunk));
        ret = gc9a01_write_data(chunk, sizeof(chunk));
        if (ret != ESP_OK) {
            return ret;
        }
    }
    return ESP_OK;
}

esp_err_t gc9a01_init(void)
{
    esp_err_t ret = gc9a01_api_lock();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gc9a01_init_unlocked();
    gc9a01_api_unlock();
    return ret;
}

esp_err_t gc9a01_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    esp_err_t ret = gc9a01_api_lock();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gc9a01_set_window_unlocked(x0, y0, x1, y1);
    gc9a01_api_unlock();
    return ret;
}

esp_err_t gc9a01_fill(uint16_t color)
{
    esp_err_t ret = gc9a01_api_lock();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gc9a01_fill_unlocked(color);
    gc9a01_api_unlock();
    return ret;
}

esp_err_t gc9a01_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    esp_err_t ret = gc9a01_api_lock();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gc9a01_fill_rect_unlocked(x0, y0, x1, y1, color);
    gc9a01_api_unlock();
    return ret;
}

esp_err_t gc9a01_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    esp_err_t ret = gc9a01_api_lock();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gc9a01_draw_pixel_unlocked(x, y, color);
    gc9a01_api_unlock();
    return ret;
}

esp_err_t gc9a01_draw_char(uint8_t line, uint8_t column, char c, uint16_t fg, uint16_t bg)
{
    esp_err_t ret = gc9a01_api_lock();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gc9a01_draw_char_unlocked(line, column, c, fg, bg);
    gc9a01_api_unlock();
    return ret;
}

esp_err_t gc9a01_draw_string(uint8_t line, uint8_t column, const char *text, uint16_t fg, uint16_t bg)
{
    esp_err_t ret = gc9a01_api_lock();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gc9a01_draw_string_unlocked(line, column, text, fg, bg);
    gc9a01_api_unlock();
    return ret;
}

esp_err_t gc9a01_show_num(uint8_t line, uint8_t column, uint32_t num, uint8_t len, uint16_t fg, uint16_t bg)
{
    esp_err_t ret = gc9a01_api_lock();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gc9a01_show_num_unlocked(line, column, num, len, fg, bg);
    gc9a01_api_unlock();
    return ret;
}

esp_err_t gc9a01_show_hexnum(uint8_t line, uint8_t column, uint32_t num, uint8_t len, uint16_t fg,
                              uint16_t bg)
{
    esp_err_t ret = gc9a01_api_lock();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gc9a01_show_hexnum_unlocked(line, column, num, len, fg, bg);
    gc9a01_api_unlock();
    return ret;
}

esp_err_t gc9a01_show_float(uint8_t line, uint8_t column, float num, uint8_t int_len, uint8_t dec_len,
                             uint16_t fg, uint16_t bg)
{
    esp_err_t ret = gc9a01_api_lock();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gc9a01_show_float_unlocked(line, column, num, int_len, dec_len, fg, bg);
    gc9a01_api_unlock();
    return ret;
}

esp_err_t gc9a01_show_picture(const uint8_t *image)
{
    esp_err_t ret = gc9a01_api_lock();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gc9a01_show_picture_unlocked(image);
    gc9a01_api_unlock();
    return ret;
}
