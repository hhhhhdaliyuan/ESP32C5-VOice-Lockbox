# 贡献指南

感谢你为 Chest 项目贡献代码和文档。本项目基于 ESP-IDF，目标芯片为 ESP32-C5。

## 贡献原则

- 未完成实物验证的硬件接线、功能或性能结论，不得作为已完成内容提交。
- 涉及已验证硬件接线、供电方式或业务协议的变更时，必须同步更新 `README.md`。
- 保持修改聚焦：只改与当前需求直接相关的代码，不做无关重构或格式化。

## 开发环境与构建

在已加载 ESP-IDF 环境的终端中，于项目根目录执行：

```powershell
idf.py set-target esp32c5
idf.py build
```

需要烧录并查看串口日志时执行：

```powershell
idf.py -p COMx flash monitor
```

将 `COMx` 替换为实际串口号。首次修改目标芯片、分区表或底层配置后，应重新完成构建和烧录验证。

## 代码与组件约定

- 应用入口放在 `main/`，可复用驱动或业务模块放在 `components/<component_name>/`。
- 每个组件必须提供 `CMakeLists.txt`；对外 API 放在 `components/<component_name>/include/`。
- 新增组件依赖时，在组件的 `CMakeLists.txt` 中通过 `REQUIRES` 或 `PRIV_REQUIRES` 明确声明。
- 公共 API 应在头文件中说明参数、返回值和调用前提；硬件相关参数必须标明单位。
- 日志仅保留有助于定位状态变化或错误的内容，避免提交高频调试日志。

## 硬件与安全验证

- 新增或修改 GPIO 分配前，确认其未与 ESP32-C5 的启动、PSRAM/MSPI 或现有外设冲突。
- 记录实际验证过的 GPIO、供电和接线；未验证的信息不得写入 `README.md`。
- SG90 必须使用独立 5 V 电源，且该电源负极必须与 ESP32-C5 GND 共地。
- 涉及舵机、音频、显示或 LED 的修改，除构建通过外，还应完成对应硬件功能的烧录验证。

## 提交信息

提交信息必须遵循 Conventional Commits 格式：

```text
<type>(<scope>): <summary>
```

常用类型：

- `feat`：新增功能，例如 `feat(servo): add slow close`
- `fix`：修复缺陷，例如 `fix(servo): clamp invalid angle`
- `docs`：仅文档变更，例如 `docs: update wiring notes`
- `chore`：构建、配置或维护性修改，例如 `chore: update idf settings`

`scope` 应使用受影响的组件名；如果改动不属于某个组件，可以省略。

## 提交前检查

提交或创建 PR 前，请确认：

- [ ] 已执行 `idf.py build`，且构建成功。
- [ ] 涉及硬件行为时，已完成烧录和实物验证。
- [ ] 已更新受影响的 `README.md` 硬件接线、供电或业务说明。
- [ ] 未提交 `build/`、`sdkconfig`、`sdkconfig.old`、本地日志或 IDE 配置文件。
- [ ] 提交信息符合 Conventional Commits 格式。

## Pull Request 说明

PR 描述至少应包含：

1. 本次变更解决的问题和修改范围。
2. 已执行的验证命令及结果，例如 `idf.py build`。
3. 硬件影响、接线变更或尚未完成的实物验证项（如适用）。

