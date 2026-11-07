# TrafficMonitor 任务栏卡死修复版

> 本仓库是 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) **1.8.6** 的个人修复分支。
> 用于解决在笔记本上开启「显示任务栏窗口」后，切换电源模式（省电 / 低功耗）或运行安卓模拟器时，
> **任务栏卡死、TrafficMonitor 窗口消失**的问题。
>
> 原始项目版权归原作者 [zhongyang219](https://github.com/zhongyang219) 所有，遵循原项目许可（Anti-996 License）。
> 本仓库仅记录修复内容，方便自己留存与回溯。

---

## 一、问题现象

在以下场景中复现：

| 条件 | 说明 |
| --- | --- |
| 硬件 | 双显卡笔记本（Intel 核显 + NVIDIA 独显），外接副屏 |
| 软件 | TrafficMonitor 1.8.6 标准版，开启「显示任务栏窗口」 |
| 触发 | 使用神舟 Control Center 3.0 切换到「省电 / 低功耗」模式 |
| 伴随 | MuMu 模拟器运行游戏（存在虚拟显示适配器） |

**表现：**

1. 任务栏上的 TrafficMonitor 窗口先消失或变黑
2. 随后整个 Windows 任务栏无响应、点不动
3. 切回高性能模式后可恢复

---

## 二、根因分析

这个问题实际上是**两个独立缺陷**叠加导致的。

### 缺陷 1：与 explorer 的跨进程死锁

TrafficMonitor 的任务栏窗口通过 `SetParent` 嵌入到 explorer 的 `Shell_TrayWnd` 下。
但它在调整位置时，对 explorer 的子窗口（如 `MSTaskSwWClass`）使用了**同步** `MoveWindow` / `SetWindowPos`。

同步调用本质上是 `SendMessage`，会阻塞等待 explorer 处理。而 explorer 同时又在等待本进程的任务栏子窗口，两边互等形成死锁 —— 表现就是整个任务栏卡死。

> 在系统繁忙时（例如切换电源模式导致 GPU 降频），这个死锁被触发的概率大幅上升。

### 缺陷 2：D2D 设备重建失败后无法降级

任务栏窗口默认使用 Direct2D 渲染（透明背景）。当省电模式导致 GPU 降频或显卡切换时，D3D10 设备会被移除，此时程序本应自动重建。

但源码中存在两个问题：

**① 重建失败没有兜底**

只有 `DXGI_ERROR_DEVICE_REMOVED`、`D2DERR_RECREATE_TARGET` 等特定错误码才会触发重建逻辑。
而省电降频时更常见的是 `E_OUTOFMEMORY` 一类的错误，会被当作普通错误计数，需累计 77 次才禁用 D2D。在这期间每一次重绘都会失败，窗口表现为「消失」。

**② 运行期回退不生效**

即使程序自动把 `disable_d2d` 置为 `true`，也只有**在设置界面点击「应用」**时才会重建任务栏窗口。运行期自动回退不会触发重建，窗口会永远停留在无法绘制的状态。

**③ 适配器选择不当**

重建时直接取 `GetDeviceList().front()`，即枚举序第一个适配器。而多显卡笔记本上该顺序并不等于「主显示器所在显卡」，尤其存在 MuMu、Oray 等虚拟适配器时，可能选中无法用于渲染的设备。

---

## 三、修复内容

共修改 7 个文件，`+193 / -14` 行。

### 1. 消除跨进程死锁

| 文件 | 修改 |
| --- | --- |
| `TrafficMonitor/TaskBarDlg.cpp` | 自身窗口移动改用 `SWP_ASYNCWINDOWPOS` 异步投递 |
| `TrafficMonitor/ClassicalTaskbarDlg.cpp` | 新增 `MoveExplorerWindowAsync()`，操作 explorer 窗口时改为异步，并加入尺寸比对与 500ms 节流，避免与 StartAllBack 等工具每 100ms 反复对打 |

关键点：`SWP_ASYNCWINDOWPOS` 让 `SetWindowPos` 不再同步等待 explorer，从根本上消除互等。

### 2. D2D 渲染失败后安全降级

| 文件 | 修改 |
| --- | --- |
| `TrafficMonitor/TaskBarDlgDrawCommon.cpp` | 新增 `TryRecreateAllAfterDeviceLost()`：重建失败**一次**即永久回退 GDI 渲染，不再每帧重试 |
| `TrafficMonitor/TaskBarDlgDrawCommon.h` | 声明上述方法 |
| `TrafficMonitor/TaskBarDlg.cpp` | `UpdateLayeredWindowIndirect` 失败路径同步刷新渲染缓存，并加一次性标志防止重复重建 |
| `TrafficMonitor/TrafficMonitorDlg.cpp` | 新增 `OnD2dDisabledTip`，把阻塞式弹框改为消息循环中延迟弹出 |
| `TrafficMonitor/TrafficMonitorDlg.h` | 声明上述消息处理 |
| `TrafficMonitor/stdafx.h` | 新增 `WM_D2D_DISABLED_TIP` 消息 |

### 3. 适配器选择修正

`InternalRecreateD3D10Device1()` 改为：

1. 遍历适配器，跳过 `DXGI_ADAPTER_FLAG_SOFTWARE` 虚拟设备
2. 枚举各适配器的输出，优先选择**主显示器**所在的适配器
3. 若均未匹配，回退到第一个硬件适配器（保持原有行为）
4. 适配器列表为空时抛出明确异常，避免对空容器调用 `.front()` 的未定义行为

---

## 四、验证结果

修复后在本机（i7-12700 + Intel UHD 770 + RTX 3070 Ti，双屏 2560×1440 / 2560×1600）验证：

- ✅ 进程运行稳定，无常驻内存增长
- ✅ 任务栏窗口正常嵌入 `Shell_TrayWnd`（实测窗口矩形 `1843,1409 - 2003,1441`）
- ✅ 切换到省电模式后，日志正确记录回退信息，任务栏不再卡死
- ✅ 编译无新增警告

回退发生时的日志形如：

```
Recreate D3D devices failed after device lost, fall back to GDI render.
```

---

## 五、如何编译

### 环境要求

- Visual Studio 2022 或更新版本（需包含 **MSVC v143** 工具集与 **MFC** 组件）
- Windows SDK
- .NET Framework 4.7.2 开发人员工具包（`OpenHardwareMonitorApi` 工程需要）

### 编译步骤

```bat
msbuild TrafficMonitor.sln /p:Configuration=Release /p:Platform=x64
```

**若提示 `MSB3644: 找不到 .NETFramework,Version=v4.7.2 的引用程序集`**，说明缺少 4.7.2 目标包。两个解决办法：

**方法一（推荐）** 安装 [.NET SDK 开发人员工具包](https://dotnet.microsoft.com/download/dotnet-framework)。

**方法二** 用已有的 .NET Framework 引用程序集替代（仅编译期引用，不影响运行时行为）：

```bat
msbuild TrafficMonitor.sln /p:Configuration=Release /p:Platform=x64 ^
  /p:FrameworkPathOverride="C:\Program Files (x86)\Reference Assemblies\Microsoft\Framework\.NETFramework\v4.8.1"
```

产物位于 `Bin\x64\Release\TrafficMonitor.exe`。

### 可用的构建配置

`Release | x64`、`Release | Win32`、`Release | ARM64EC`

---

## 六、使用须知

1. **升级会覆盖修复。** 若日后更新为官方版本，本修复会丢失，需重新应用补丁。
2. **建议开启日志。** 在 `config.ini` 的 `[other]` 段设 `debug_log = true`，便于排查回退问题。
3. **兼容性说明。** 本修复针对 Windows 11（实测 Build 26300）。Windows 10 经典任务栏路径同样适用，但未做完整回归测试。

---

## 七、原项目信息

TrafficMonitor 是一款 Windows 平台的网速监控悬浮窗软件，可显示网速、CPU 及内存利用率，支持嵌入任务栏、更换皮肤、历史流量统计等功能。

- 原项目：[zhongyang219/TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor)
- Gitee 镜像：[zhongyang219/TrafficMonitor](https://gitee.com/zhongyang219/TrafficMonitor)
- 常见问题：[Help.md](./Help.md)

### 版本说明

原项目提供标准版与 Lite 版：

| 功能 | 标准版 | Lite 版 |
| --- | --- | --- |
| 网速监控 | ✔ | ✔ |
| CPU、内存利用率 | ✔ | ✔ |
| CPU、显卡、硬盘、主板温度监控 | ✔ | ❌ |
| 显卡 / 硬盘利用率监控 | ✔ | ✔ |
| 插件系统 | ✔ | ✔ |
| 需要管理员权限 | 是 | 否 |

> 从 1.86 版本起，温度监控功能不再维护，已迁移至[硬件监控插件](https://github.com/zhongyang219/TrafficMonitorPlugins/blob/main/download/plugin_download.md)。

### 运行库依赖

TrafficMonitor 依赖 Microsoft Visual C++ 运行库。若启动提示「找不到 MSVC*.dll」，请安装 [Visual C++ 可再发行程序包](https://learn.microsoft.com/zh-CN/cpp/windows/latest-supported-vc-redist)。

---

## 八、许可

本仓库代码基于原项目修改，遵循原项目的 [Anti-996 License](./LICENSE)。
