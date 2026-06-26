# VM Manager v5.0

**Windows 虚拟内存监控 + 可疑进程检测**

[![Platform](https://img.shields.io/badge/platform-Windows%207%2B-blue)]()
[![Language](https://img.shields.io/badge/language-C%20%2B%20C%2B%2B-orange)]()
[![Encryption](https://img.shields.io/badge/encryption-DPAPI%20(user--bound)-purple)]()

三层架构的轻量级 Windows 系统工具: 监控虚拟内存、追踪 CPU/GPU 使用、检测可疑进程，提供 Web 仪表盘。

## 架构

```
┌─────────────────────────────────────────────────┐
│                 Web 仪表盘 (Layer 3)              │
│  内嵌 HTML SPA → REST API → 127.0.0.1:18080     │
│  src/web/dashboard_html.h                       │
├─────────────────────────────────────────────────┤
│              C++ 包装层 (Layer 2)                 │
│  RAII 生命周期 | VMApp | VMHttpServer            │
│  src/cpp/vm_app.cpp | vm_http_server.cpp         │
├─────────────────────────────────────────────────┤
│               C 核心后端 (Layer 1)                │
│  内存监控 | CPU/GPU采样 | DPAPI加密 | 异常检测    │
│  src/core/vm_engine.c | vm_db.c | vm_locale.c    │
└─────────────────────────────────────────────────┘
```

## 功能

### 核心监控
- **内存**: 页面文件使用率、物理内存负载、进程 Commit & Working Set
- **CPU**: 基于 `GetProcessTimes` 的两次差分采样计算进程 CPU 占用率
- **GPU**: 通过动态加载 PDH 计数器查询 GPU 使用率 & VRAM
- **历史**: 2880 条快照循环缓冲 + 小时/天/月聚合

### 可疑进程检测
通过追踪 Commit 大小变化率检测快速增长进程:
- 标记增长速率 > 10 MB/s 的进程
- 追踪峰值增长率
- 连续 3 次检测后生成异常警报
- 最小 100 MB Commit 阈值过滤噪音

### 异常检测引擎
| 类型 | 条件 |
|------|------|
| CPU 异常 | 单进程 > 50% CPU, 连续 3+ 次采样 |
| 内存异常 | 单进程 Commit > 4 GB |
| GPU 异常 | 使用率 > 90% |
| 可疑进程 | 内存增长 > 10 MB/s |

### 自动清理
当系统空闲 (5+ 分钟) 且页面文件使用率达 85%:
- 通过 `EmptyWorkingSet()` 裁剪所有后台进程工作集
- 跳过前台应用和自身
- 冷却时间: 10 分钟

### 安全
- **DPAPI 加密**: 历史数据加密存储在 `vm_data.db`，密钥绑定到当前用户
- **本地绑定**: HTTP 服务器仅绑定 127.0.0.1
- **零外部依赖**: 纯 Win32 API + 系统 DLL

## 构建

### 前提条件
- Windows 7+
- MinGW GCC (项目自带 `../MinGW/bin/`)

### 编译
```batch
cd vm_manager_web
build.bat
```

或手动:
```bash
# C 核心层
gcc -c -I src src/core/vm_engine.c -o build/vm_engine.o -O2 -Wall
gcc -c -I src src/core/vm_db.c    -o build/vm_db.o    -O2 -Wall
gcc -c -I src src/core/vm_locale.c -o build/vm_locale.o -O2 -Wall

# C++ 包装层
g++ -std=c++14 -c -I src src/cpp/vm_app.cpp -o build/vm_app.o -O2 -Wall
g++ -std=c++14 -c -I src src/cpp/vm_http_server.cpp -o build/vm_http_server.o -O2 -Wall

# 入口点
g++ -std=c++14 -c -I src src/main.cpp -o build/main.o -O2 -Wall

# 链接
g++ build/*.o -o vm_manager.exe -mwindows \
    -lpsapi -lws2_32 -lcrypt32 -lcomctl32 -lgdi32 -O2 -s
```

### 链接库
| 库 | 用途 |
|----|------|
| `-lpsapi` | `GetProcessMemoryInfo`, `EmptyWorkingSet` |
| `-lws2_32` | Winsock2 (HTTP 服务器) |
| `-lcrypt32` | DPAPI 加密 |
| `-lcomctl32` | Common Controls (桌面 GUI 模式) |
| `-lgdi32` | GDI 图表绘制 (桌面 GUI 模式) |

## 使用

```batch
# 后台禁默 + Web 仪表盘 (默认, 推荐)
vm_manager.exe

# 控制台调试模式
vm_manager.exe /console

# 桌面 GUI 模式 (需编译时启用 WITH_DESKTOP_GUI)
vm_manager.exe /desktop
```

浏览器打开 **http://127.0.0.1:18080** 查看仪表盘。

## 项目结构

```
vm_manager_web/
├── src/
│   ├── core/                # C 底层核心 (Layer 1)
│   │   ├── vm_common.h      #   共享类型、常量、全局变量
│   │   ├── vm_engine.h      #   引擎 API 声明
│   │   ├── vm_engine.c      #   内存监控、CPU/GPU 采样、异常检测、清理
│   │   ├── vm_db.h          #   数据库 API 声明
│   │   ├── vm_db.c          #   DPAPI 加密数据库
│   │   ├── vm_locale.h      #   国际化 API
│   │   └── vm_locale.c      #   中/英/繁 三语支持
│   ├── cpp/                 # C++ 包装层 (Layer 2)
│   │   ├── vm_bridge.hpp    #   C/C++ 桥接 (extern "C")
│   │   ├── vm_app.hpp       #   应用主类声明
│   │   ├── vm_app.cpp       #   生命周期管理、模式分发
│   │   ├── vm_http_server.hpp  # HTTP 服务器类声明
│   │   └── vm_http_server.cpp  # REST API + 内嵌仪表盘
│   ├── web/                 # Web 前端 (Layer 3)
│   │   └── dashboard_html.h #   内嵌仪表盘 SPA (Canvas 图表)
│   ├── main.cpp             # C++ 入口点 (WinMain)
│   └── vm_desktop.c         # 可选: 桌面 GUI (需 WITH_DESKTOP_GUI)
├── build/                   # 编译中间产物 (.o)
├── vm_manager.exe           # 编译输出
├── vm_manager.log           # 运行时日志
├── vm_data.db               # 加密历史数据库
├── build.bat                # 一键构建脚本
└── README.md
```

## API 端点

| 端点 | 描述 |
|------|------|
| `GET /` | 仪表盘 SPA |
| `GET /api/status` | 当前内存快照 + 进程 TOP 20 |
| `GET /api/history` | 24h 历史快照 |
| `GET /api/actions` | 清理操作日志 |
| `GET /api/cpu` | 进程 CPU 占用率 |
| `GET /api/gpu` | GPU 使用率 & VRAM |
| `GET /api/anomalies` | 异常警报 |
| `GET /api/suspicious` | 可疑进程追踪 |
| `GET /api/aggregated?range=week` | 聚合历史 (day/week/month/year) |

## 可配置参数

编辑 `src/core/vm_common.h`:

```c
#define CHECK_INTERVAL_SEC       30    // 快照间隔
#define IDLE_THRESHOLD_SEC       300   // 清理触发的空闲阈值
#define PAGE_FILE_THRESHOLD_PCT  85    // 页面文件 % 触发阈值
#define CPU_HOG_THRESHOLD_PCT    50    // CPU 异常阈值
#define MEM_HOG_THRESHOLD_MB     4096  // 内存异常阈值
#define SUSPICIOUS_GROWTH_MB_PER_SEC 10 // 可疑进程增长阈值
```

## 许可

MIT
