# Chest

基于 ESP32-S3 的智能宝盒原型项目。

当前目标是先完成"关键词识别并自动开盒、闭盒"的稳定闭环；后续逐步扩展声纹验证、局域网管理和物品收纳统计等功能。

## 当前阶段

第一阶段为功能验证阶段，验证以下完整交互链路：

```text
ES8311 持续采集音频
  -> 复用 SonKey 当前 KWS 业务
  -> 命中关键词
  -> SG90 执行开盒或闭盒动作
  -> GC9A01 与 LED 更新业务状态
```

## 第一阶段功能

系统持续采集 ES8311 音频，并复用 SonKey 当前 KWS 业务识别关键词。

| 关键词 | 动作 |
| --- | --- |
| `你好盘宝` | 控制 SG90 自动开盒 |
| `你好鹰老师` | 控制 SG90 自动闭盒 |

当盒子已经处于目标状态时，不重复驱动 SG90，只刷新当前业务状态。

## 屏幕与灯光状态

GC9A01 和 LED 用于显示当前业务状态。

| 状态 | 含义 |
| --- | --- |
| `BOOTING` | 系统启动中 |
| `CONNECTING` | 连接语音服务 |
| `LISTENING` | 等待关键词 |
| `OPENING` / `OPENED` | 开盒中 / 已开盒 |
| `CLOSING` / `CLOSED` | 闭盒中 / 已闭盒 |
| `ERROR` | 网络、音频或执行器异常 |

## 已确认硬件

| 模块 | 方案 | 当前职责 |
| --- | --- | --- |
| 主控 | ESP32-S3 | 系统控制与网络通信 |
| 音频编解码器 | ES8311 | 音频采集与播放基础能力 |
| 执行器 | SG90 舵机 | 宝盒开合功能验证 |
| 显示屏 | GC9A01 | 显示业务状态 |
| 状态提示 | 3 x LED（红、绿、黄） | 显示开盒/闭盒/解锁结果 |
| 语音业务 | SonKey 当前 KWS 服务 | 识别关键词 |

### 硬件接线约定

| 设备 | 信号线 | GPIO | 另一端 |
|------|--------|------|--------|
| 舵机 MG90S | 信号线 | **GPIO6** | — |
| 红色 LED | 正极（阳极） | **GPIO3** | GND |
| 绿色 LED | 正极（阳极） | **GPIO16** | GND |
| 黄色 LED | 正极（阳极） | **GPIO18** | GND |

- LED 为**高电平点亮**（GPIO 输出 1 → LED 亮）。
- 所有 LED 共地，建议外接 100 Ω 限流电阻。
- SG90 必须使用独立 5 V 电源，且电源负极必须与 ESP32-S3 GND 共地。
- 未完成实物验证的引脚，不写入本 README。
- 完成接线、构建和烧录验证后，应在同一提交中更新 README。

## LED 灯光模式

三色 LED 用于直观反馈宝盒状态。底层由 `components/led_control/` 驱动，提供以下灯光模式：

| 模式 | 枚举值 | 灯光效果 | 使用场景 |
|------|--------|----------|----------|
| 全部关闭 | `LED_PATTERN_OFF` | 全部熄灭 | 系统待机/休眠 |
| 待机 | `LED_PATTERN_IDLE` | 绿灯常亮 | 系统就绪，等待指令 |
| 解锁成功 | `LED_PATTERN_UNLOCK_SUCCESS` | 绿灯闪烁 3 次后常亮 | 声纹/关键词验证通过 |
| 解锁失败 | `LED_PATTERN_UNLOCK_FAIL` | 红灯快速连续闪烁 | 声纹/关键词验证失败 |
| 开盒过程 | `LED_PATTERN_OPENING` | 多色循环：红→绿→黄→红+绿→绿+黄→红+黄→全亮→全灭，循环步进 200 ms | 舵机开盒或闭盒过程中 |
| 闭盒完毕 | `LED_PATTERN_CLOSED` | 绿+黄同时闪烁（400 ms 间隔） | 盒子完全闭合后 |

### 与舵机的联动规则

| 业务事件 | 调用顺序 | 说明 |
|----------|----------|------|
| 开盒 | `set_pattern(OPENING)` → `servo_open(20)` | 先开启多色循环，再启动舵机旋转 |
| 闭盒 | `set_pattern(OPENING)` → `servo_close(20)` | 开盒和闭盒过程中复用同一多色循环 |
| 闭盒完毕 | `set_pattern(CLOSED)` | 舵机到位后切换 |
| 解锁成功 | `set_pattern(UNLOCK_SUCCESS)` | 可在开盒前驱动，也可在开盒后驱动 |
| 解锁失败 | `set_pattern(UNLOCK_FAIL)` | 拒绝动作后保持 |
| 空闲待机 | `set_pattern(IDLE)` | 无事件时默认状态 |

### 函数 API 参考

由 `components/led_control/` 提供，`main.c` 或其他业务模块调用：

```c
#include "led_control.h"

/* 初始化（在 app_main 中调用一次） */
led_control_init();

/* 切换灯光模式 — 在业务事件发生时调用 */
led_control_set_pattern(LED_PATTERN_IDLE);              // 待机
led_control_set_pattern(LED_PATTERN_OPENING);            // 开盒/闭盒中
led_control_set_pattern(LED_PATTERN_UNLOCK_SUCCESS);     // 解锁成功
led_control_set_pattern(LED_PATTERN_UNLOCK_FAIL);        // 解锁失败
led_control_set_pattern(LED_PATTERN_CLOSED);             // 闭盒完毕
led_control_set_pattern(LED_PATTERN_OFF);                // 全部关闭

/* 获取当前模式（可选） */
led_pattern_t cur = led_control_get_pattern();
```

> 模式切换即时生效。内部有独立的 FreeRTOS 任务持续运行当前模式的闪烁/循环逻辑，切换后自动更新。

### 后续集成指引

当接入声纹验证、按键触发或语音关键词识别后，在对应的事件处理函数中调用 `led_control_set_pattern() ` 即可，无需关心 LED 硬件细节。

```c
// 示例：关键词"盘宝"触发开盒
void on_keyword_open(void)
{
    led_control_set_pattern(LED_PATTERN_OPENING);   // 开盒灯光
    servo_open(20);                                  // 舵机开盒
    // 开盒完成后可根据结果切换
}

// 示例：声纹验证失败
void on_verify_failed(void)
{
    led_control_set_pattern(LED_PATTERN_UNLOCK_FAIL);
}
```

> ✅ 该部分已完成上板验证（三色 LED + 舵机联动）。

## 暂不包含的功能

- 按键触发开盒或闭盒。
- 声纹注册与声纹验证解锁。
- 触摸输入、用户管理页面和局域网网页后台。
- 物品语音录入与数量统计。
- 清洁模式、娱乐模式和自动休眠。

## 后续功能

- 声纹注册、声纹验证和用户信息管理。
- 局域网后台管理。
- 物品收纳信息的语音录入与统计。
- 清洁模式、娱乐模式和自动休眠。
- 开合限位、卡滞检测和更完善的安全保护。

## 复用来源

音频采集、ES8311 配置、SG90 控制基础和 KWS 业务参考：

`E:\workapp\esp32\twork\SonKey`

迁移原则：只复用已验证的底层驱动和 KWS 通信链路，不复制 SonKey 的声纹注册、显示、用户管理等业务逻辑。

## ES8311 音频组件

`audio_i2s` 只负责 I2S0 的 TX/RX 通道与原始立体声时隙收发；`es8311` 负责 I2C、Codec 配置和单声道 PCM API。

Codec 基于 ESP Component Registry 的 `espressif/esp_codec_dev` `1.5.11`，版本由 `dependencies.lock` 锁定，组件源码随 `managed_components` 一并追踪。

### 正常使用

应用启动时只需初始化 Codec：

```c
ESP_ERROR_CHECK(es8311_init());
```

采集业务调用 `es8311_read_pcm()` 获取固定 20 ms 的 PCM 帧。每帧为 `640 B`，格式为 `16 kHz / 16-bit / mono / little-endian`。如需播放同格式 PCM，调用 `es8311_write_pcm()`。

### 板端自检方法

自检默认关闭，正常固件不会录音或回放。需要单独验证 ES8311 时：

1. 将 `components/es8311/es8311_config.h` 中的 `ES8311_SELF_TEST_ENABLE` 临时改为 `1`。
2. 在 `app_main()` 的 `es8311_init()` 后临时加入：

   ```c
   ESP_ERROR_CHECK(es8311_run_self_test());
   ```

3. 编译、烧录并监视串口。应循环出现 `self-test recording 2 seconds` 与 `self-test playing 2 seconds`，同时可听到录音回放。
4. 验证结束后移除该临时调用，并将宏恢复为 `0`，再提交正常业务代码。

## 构建

### 环境要求

- ESP-IDF v5.5.4（位于 `D:\Esp32\esp-idf-v5.5.4`）
- 目标芯片：ESP32-S3
- 使用项目根目录下的 `D:\Esp32\esp32-build.ps1` 加载环境

### 编译

```powershell
cd D:\Esp32\test\ESP32-CHEST-PRJ
. D:\Esp32\esp32-build.ps1
```

### 烧录

```powershell
idf.py -p COM? flash
```

将 `COM?` 替换为实际串口号（设备管理器查看）。

### 查看串口日志

```powershell
idf.py -p COM? monitor
```

按 `Ctrl + ]` 退出。

### 一条龙

```powershell
cd D:\Esp32\test\ESP32-CHEST-PRJ
. D:\Esp32\esp32-build.ps1
idf.py -p COM? flash monitor
```

## 协作约定

- 提交前确保项目可以构建（`idf.py build` 通过）。
- 一次提交只做一件事，提交前执行 `git diff` 检查。
- 提交信息格式：`<type>(<scope>): <动词开头的说明>`，例如 `feat(led): add unlock fail blink pattern`。
- 不提交 `build/`、`*.bin`、`*.elf`、`*.log`、`sdkconfig` 和本地日志。
- 已验证的硬件接线、供电或业务协议发生变化时，同步更新本 README。
- 未验证或未实现的功能必须明确标记为后续功能。
- 每次实际交付使用 Git Tag 标记：`git tag -a v1.0.0 -m "Release v1.0.0"`。
