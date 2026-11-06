# VM Manager v6.0

**Windows 内存 / 进程 / GPU 监控台 — 前后端分离重构版**

[![Platform](https://img.shields.io/badge/platform-Windows%2010%2B-blue)]()
[![Runtime](https://img.shields.io/badge/runtime-Node.js%2020.11%2B-green)]()
[![Stack](https://img.shields.io/badge/stack-Fastify%20%2B%20React%20%2B%20SQLite-orange)]()

v6 把原来的「C 核心 + C++ 手写 HTTP + 内嵌 HTML」单体程序，重构为**独立的 Node/TypeScript 后端 + React 前端**，并加入 4 组新功能。旧的 C/C++ 代码、可执行文件与加密数据文件原样保留为 legacy，不参与 v6 运行。

---

## 架构

```
┌──────────────────────────── Web 前端 (web/) ─────────────────────────────┐
│ React 18 + Vite + TypeScript + Recharts                                  │
│ 概览 / 进程 / 历史分析 / 告警中心 / 设置中心 · 三语 i18n · 深浅主题         │
│ SSE 实时订阅 + 断线轮询兜底                                               │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │ REST /api/*  +  SSE /api/events
┌───────────────────────────────▼─────────────────────────────────────────┐
│                         后端服务 (server/)                                │
│ Fastify 5 + TypeScript + better-sqlite3 (WAL)                            │
│ 采集器 ─ 告警规则引擎 ─ 聚合/导出 ─ 进程控制 ─ 设置中心 ─ 事件总线          │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │ PowerShell CIM / PDH / nvidia-smi
                    Windows 10+ 系统指标
```

- **采集**：单次 PowerShell 调用取得内存、页面文件、进程、GPU、空闲时间；Node 侧做 CPU 时间差分、内存增长追踪与白名单匹配
- **GPU**：`nvidia-smi` 为准（利用率 / 已用显存 / 总显存），非 N 卡降级到 PDH 计数器，并对异常读数做范围校验
- **存储**：`vm_manager.sqlite`（SQLite WAL），快照 + 进程样本 + 告警 + 规则 + 白名单 + 操作日志 + 设置
- **实时**：SSE 广播 `snapshot / alert / settings / action`，前端按通知设置弹 Toast、播放提示音

---

## 快速开始

```bash
# 1. 安装依赖（Node >= 20.11，建议 22；npm workspaces）
npm install

# 2. 构建后端 + 前端
npm run build

# 3. 启动（同时托管前端静态产物）
npm start
# 浏览器打开 http://127.0.0.1:18080
```

开发模式（前后端热更新，Vite 代理 `/api` 到后端）：

```bash
npm run dev          # 前端 http://127.0.0.1:5173，后端 18080
```

其他脚本：

| 命令 | 说明 |
|------|------|
| `npm run typecheck` | 前后端 TypeScript 类型检查 |
| `npm run dev:server` / `npm run dev:web` | 单独启动后端 / 前端 |
| `npm start` | 运行编译后的 `server/dist/index.js` |

### 环境变量

| 变量 | 默认 | 说明 |
|------|------|------|
| `VM_PORT` | 设置中心的 `port`（18080） | 覆盖监听端口；被占用时自动顺延最多 5 个端口 |
| `VM_HOST` | `127.0.0.1` | 仅本地绑定，勿暴露到公网 |
| `VM_DB_PATH` | 项目根 `vm_manager.sqlite` | 覆盖数据库路径 |
| `VM_DEMO` | 关闭 | `1` 启用模拟数据，用于无 Windows 采集环境/演示，界面会显著标注「演示模式」 |
| `VM_API_TARGET` | `http://127.0.0.1:18080` | Vite 开发代理目标（端口改过时需要同步） |

---

## 功能

### 监控与展示
- 页面文件 / 物理内存 / CPU 总负载 / GPU 使用率四张实时卡片，24h 资源趋势、TOP 10 进程、系统信息、最新告警
- 进程表：搜索、多字段排序、白名单标记、可疑增长标记、详情抽屉（含历史趋势）
- 历史分析：`1h / 24h / 7d / 30d / 1y` 范围切换、多指标曲线、聚合柱状图、统计摘要
- SSE 实时刷新；实时连接断开时自动降级为 10s 轮询

### 新功能（v6）
1. **告警规则与通知** — 可增删改的阈值规则（指标 / 大于小于 / 连续次数 / 严重级别 / 冷却时间 / 启停），连续命中才触发、冷却去重，支持网页 Toast、提示音、Windows 桌面通知，告警可确认/批量确认/清空
2. **进程操作与白名单** — 结束 / 挂起 / 恢复进程，通配符白名单，白名单进程与系统关键进程、服务自身禁止操作；危险操作需 `confirm: true` 且前端二次确认，全程记入操作日志
3. **历史分析与导出** — 原始/下采样趋势、按小时/天/月聚合、CSV / JSON 导出（快照、告警、进程样本、操作日志，最多 5 万行）
4. **设置中心** — 采样间隔、端口、语言、主题、历史保留天数、通知开关与最低级别、自动清理、可疑增长阈值、开机自启；采样间隔与阈值热生效，端口变更提示重启

### 兼容与保护
- 保留 v5 只读端点：`/api/status`、`/api/history`、`/api/cpu`、`/api/gpu`、`/api/actions`、`/api/anomalies`、`/api/suspicious`、`/api/aggregated`；`/api/stopserver`、`/api/shutdown` 返回 410
- 所有 SQL 使用参数绑定与表白名单；CSV 值做转义；进程 PID 经数字校验；导出文件名白名单化
- 开机自启仅在用户主动开启时写入 `HKCU\...\Run`（调用项目根 `start.cmd`），关闭时只删除自建键值

---

## API

| 方法 | 端点 | 说明 |
|------|------|------|
| GET | `/api/health` | 健康状态、演示模式、SSE 连接数 |
| GET | `/api/events` | SSE 事件流 |
| GET | `/api/status` | 当前快照 + 全部进程 |
| GET | `/api/processes?search=&sort=&limit=` | 进程列表 |
| GET | `/api/processes/:pid` | 进程详情 + 历史样本 |
| POST | `/api/processes/:pid/terminate` | 结束进程（需 `confirm`） |
| POST | `/api/processes/:pid/suspend` / `resume` | 挂起 / 恢复 |
| GET/POST/DELETE | `/api/whitelist` | 白名单 CRUD |
| GET | `/api/history?range=24h` | 下采样趋势点 |
| GET | `/api/aggregated?range=week` | 聚合视图 |
| GET | `/api/export?kind=&format=&range=` | CSV / JSON 导出 |
| GET/POST/DELETE | `/api/alerts` | 告警列表 / 确认 / 清空 |
| GET/POST/DELETE | `/api/rules` | 告警规则 CRUD |
| GET/PUT | `/api/settings` | 读取 / 更新设置 |
| POST | `/api/collect` | 立即触发一次采集 |
| POST | `/api/settings/prune` | 清理过期数据 |
| DELETE | `/api/data?confirm=true` | 清空历史快照与进程样本 |
| GET | `/api/actions`、`/api/stats`、`/api/suspicious`、`/api/anomalies` | 日志 / 统计 / 可疑进程 / 兼容端点 |

---

## 目录

```
vm_manager_web/
├── package.json                 # npm workspaces + 统一脚本
├── server/
│   ├── src/
│   │   ├── index.ts            # Fastify 启动、静态托管、采样调度
│   │   ├── collector.ts        # PowerShell 采集 + CPU/增长差分 + 演示数据
│   │   ├── alerts.ts           # 规则引擎、连续命中与冷却
│   │   ├── db.ts               # SQLite schema 与全部 DAO
│   │   ├── config.ts           # 默认设置与 zod 校验
│   │   ├── events.ts / notify.ts / context.ts
│   │   └── routes/             # status / history / processes / alertRoutes / settings / legacy
│   └── dist/                   # tsc 编译产物
├── web/
│   ├── src/
│   │   ├── App.tsx             # 布局、Context、SSE、Toast、确认框
│   │   ├── pages/              # 5 个页面
│   │   ├── api.ts / i18n.ts / types.ts / ui.tsx / styles.css
│   └── dist/                   # Vite 构建产物（由后端托管）
├── start.cmd                   # 开机自启入口
├── vm_manager.sqlite           # v6 数据库（自动生成）
├── src/ build.bat vm_manager.exe vm_data.db   # v5 legacy，保持原样
└── tools/                      # v5 配套脚本，保持原样
```

---

## 已知限制

- 修改端口需重启服务；开机自启仅 Windows 生效，且需要 `start.cmd` 与本机 Node 环境
- CPU 差分在首次采样为 0；采样间隔越短，增长速率越容易受噪声影响（已对 <3s 窗口忽略增长判定）
- GPU 指标依赖 `nvidia-smi` 或 PDH 计数器；无 N 卡 / 精简系统上显存可能显示 `--`
- 挂起/恢复进程需要足够权限；`System`、`csrss.exe` 等关键进程被后端硬保护
- Windows 桌面通知可能被系统「专注助手 / 通知设置」抑制，此时仍有网页 Toast
- v5 的 `vm_data.db`（DPAPI 加密）**不会自动迁移**；v6 使用独立的新数据库

## 验证

在 Windows 11 / Node 22 环境下已完成：前后端类型检查与生产构建、真实采集（进程数、CPU 差分、GPU 显存）、全部读写接口冒烟、SSE、危险操作防护、CSV/JSON 导出，以及无头浏览器对 5 个页面的渲染检查（零控制台错误，三语切换与深浅主题均正常）。

## 许可

MIT
