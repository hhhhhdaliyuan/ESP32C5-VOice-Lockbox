# ESP32 Chest

基于 ESP32-C5-WROOM-1 MCN16R8 的智能宝盒固件。当前已启用 ES8311
实时音频采集、云端关键词识别、按键声纹注册，以及关键词与声纹双重验证。
关键词和声纹同时通过后，SG90 才会执行开盒动作；显示屏和 LED 将继续分阶段迁移。

## 当前状态

已完成并通过实机验证：

- ES8311 左声道实时采集，格式为 `16 kHz / mono / pcm_s16le`。
- KWS WebSocket 持续监听 `你好盘宝` 和 `你好鹰老师`。
- GPIO24 按住录音、松开上传的声纹注册流程。
- KWS 命中后，使用最近 3 秒 PCM 进行声纹验证。
- 只有关键词与声纹同时通过，才向业务层发布最终唤醒事件。
- 最终唤醒事件触发 SG90 从 0° 慢速旋转到 150°，完成开盒；没有最终唤醒事件不会开盒。
- 声纹注册状态写入 NVS，设备重启后自动恢复双重验证监听。
- 网络异常、WebSocket 中断和音频上传异常自动重建监听会话。

`chest_controller` 业务任务负责消费最终唤醒事件并驱动 SG90 开盒，`app_main()` 只负责启动控制器。GC9A01 和三色 LED 的底层组件已经存在，但尚未绑定监听、验证及开盒状态。

## 双重认证流程

```text
ES8311 持续采集左声道 PCM
  -> KWS WebSocket 识别关键词
  -> KWS 命中后提取最近 3 秒 PCM
  -> 上传声纹验证
  -> 声纹匹配已注册用户
  -> 发布最终唤醒事件
  -> chest_controller 业务任务分发动作
  -> SG90 慢速旋转到 150°开盒
```

仅出现 KWS 候选不代表唤醒成功。最终成功日志包含：

```text
=== VOICEPRINT VERIFIED: ... ===
=== KWS + VOICEPRINT WAKEUP: ... ===
chest_controller: dual-auth wakeup: ...
```

声纹不匹配或后端请求失败时，日志会输出 `WAKEUP REJECTED`，业务层不会收到唤醒事件。

## 声纹注册

注册按键连接在 GPIO24 与 GND 之间，固件启用内部上拉，低电平有效。

1. 按住 GPIO24 注册按键。
2. 对着麦克风说任意内容，不要求包含关键词。
3. 持续至少 3 秒，建议录制 5 到 8 秒。
4. 松开按键，固件上传按住期间的 PCM。
5. 注册成功后自动进入 KWS 正常监听。

单次录音最长 15 秒。再次执行相同操作会使用配置的 `speaker_id` 继续注册该用户。

关键日志：

```text
=== VOICEPRINT ENROLL START ... ===
=== VOICEPRINT ENROLL UPLOADING ... ===
=== VOICEPRINT ENROLL SUCCESS ... ===
```

## 音频与网络

- 采样率：`16000 Hz`
- 位宽：`16-bit signed little-endian`
- 声道：单声道，取 ES8311 左 I2S slot
- 采集帧：`20 ms / 640 B`
- 音频队列：100 帧，约 2 秒
- KWS 上传批次：`200 ms / 6400 B`
- 声纹验证窗口：最近 3 秒，`96000 B`
- 声纹注册窗口：3 至 15 秒

后端协议：

- KWS：WebSocket `/v1/kws/listen`
- 声纹注册：`POST /v1/voiceprint/enroll`
- 声纹验证：`POST /v1/voiceprint/verify`
- 声纹列表：`GET /v1/voiceprint/speakers`

服务器地址、设备 ID、产品 ID、声纹 ID 和鉴权信息均通过项目配置提供，不应写入业务代码或 README。

## 主要组件

| 组件 | 职责 |
| --- | --- |
| `components/es8311/` | I2C、I2S 和 ES8311 Codec 初始化，输出 20 ms PCM 帧 |
| `components/kws_wakeup/` | Wi-Fi、KWS WebSocket、音频队列、注册按键和双重验证状态机 |
| `components/voiceprint_auth/` | 声纹注册/验证 HTTP 客户端和 NVS 注册状态 |
| `components/chest_controller/` | 业务任务、关键词动作分发和宝盒外设编排 |
| `components/board_config/` | 板级 GPIO 和外设资源分配 |
| `components/gc9a01/` | GC9A01 显示驱动 |
| `components/led_control/` | 三色 LED 状态模式 |
| `components/servo/` | 舵机控制 |

## GPIO 分配

| 功能 | GPIO |
| --- | ---: |
| 声纹注册按键 | GPIO24 |
| SG90 PWM | GPIO10 |
| ES8311 I2C SDA / SCL | GPIO7 / GPIO6 |
| ES8311 I2S MCLK / BCLK / WS | GPIO0 / GPIO4 / GPIO5 |
| C5 I2S TX -> ES8311 DIN | GPIO2 |
| ES8311 DOUT -> C5 I2S RX | GPIO3 |
| 串口日志（CH340） | GPIO11 / GPIO12 |
| 显示屏 / LED | 暂未分配 |

ES8311、按键和其他外设必须共地。SG90 信号线接 GPIO10；红线接独立稳压 5 V，
黑/棕线接外部电源 GND，且外部电源 GND 必须与 C5 GND 共地。不要使用 C5 的
3.3 V 给舵机供电。

## 项目配置

在 ESP-IDF 项目配置中设置：

```text
KWS wakeup configuration
Voiceprint authentication configuration
```

需要配置的项目包括：

- Wi-Fi SSID 和密码。
- KWS WebSocket 地址、设备 ID、产品 ID 和可选鉴权 token。
- 声纹 HTTP 服务地址、`speaker_id` 和显示名称。
- WebSocket 重连间隔。

不要提交包含个人网络凭据的 `sdkconfig`。

## 构建与烧录

要求：

- ESP-IDF 5.5.x
- 目标芯片 ESP32-C5-WROOM-1 MCN16R8
- 16 MB Flash 和 8 MB PSRAM

```bash
idf.py set-target esp32c5
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

`<PORT>` 使用实际连接的串口设备名称。

## 待完成

- 接线后验证 SG90 开盒角度与机械限位，必要时校准脉宽和目标角度。
- 增加闭盒业务动作和触发方式。
- 接入 GC9A01 和 LED 的监听、验证、成功及失败状态。
- 增加本地用户管理和声纹删除流程。
- 增加开合限位、卡滞检测和执行器安全保护。
