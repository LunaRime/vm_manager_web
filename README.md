# VM Manager v4.0

**Windows Virtual Memory Monitor + Suspicious Process Detector**

[![Platform](https://img.shields.io/badge/platform-Windows%207%2B-blue)]()
[![Language](https://img.shields.io/badge/language-C-orange)]()
[![License](https://img.shields.io/badge/license-MIT-green)]()
[![Encryption](https://img.shields.io/badge/encryption-DPAPI%20(user--bound)-purple)]()

A lightweight, zero-dependency Windows system tool that monitors virtual memory, tracks CPU/GPU usage, detects suspicious processes by memory growth rate, and provides both a desktop GUI and a web dashboard.

## Features

### Core Monitoring
- **Memory**: Page file usage %, physical memory load, per-process Commit & Working Set
- **CPU**: Per-process CPU% via `GetProcessTimes` two-pass differential sampling
- **GPU**: Utilization % and VRAM via dynamic PDH counter loading
- **History**: Rolling buffer of 2880 snapshots, hourly/daily/monthly aggregation

### Suspicious Process Detection
Detects rapidly-growing processes by tracking Commit size change rate:
- Flags processes growing > 10 MB/s
- Tracks peak growth rate over time
- Generates anomaly alerts after 3 consecutive detections
- Filters out noise (minimum 100 MB commit threshold)

### Anomaly Detection Engine
| Type | Criteria |
|------|----------|
| CPU Hog | Single process > 50% CPU for 3+ consecutive samples |
| Memory Hog | Single process Commit > 4 GB |
| GPU Hog | Utilization > 90% |
| Suspicious | Memory growth > 10 MB/s |

### Auto-Cleanup
When system is idle (5+ min) AND page file usage reaches 85%:
- Trims working sets of all background processes via `EmptyWorkingSet()`
- Skips foreground app and self
- Cooldown: 10 minutes between actions

### Interfaces
| Interface | Description |
|-----------|-------------|
| **Desktop GUI** | Win32 native app with tabs, system tray, dark theme |
| **Web Dashboard** | Embedded HTTP server (127.0.0.1:18080-18089) with SPA |

### Desktop GUI — Tab Layout
1. **Overview** — Process list + Action log
2. **Processes** — Full process table with search
3. **Charts** — GDI-rendered memory trend charts (Day/Week/Month/Year)
4. **Anomalies** — All detected anomalies with timestamps
5. **Suspicious** — Tracked suspicious processes with growth data

### Web Dashboard
- Real-time gauges (Page File / Physical Memory)
- 24-hour line chart (Canvas API)
- Process TOP 20 with search
- CPU/GPU/Anomaly panels
- Auto-refreshes every 5 seconds

### Security
- **DPAPI Encryption**: All historical data stored in `vm_data.db` encrypted with Windows DPAPI (bound to current user account)
- **Local Only**: HTTP server binds to 127.0.0.1 only
- **No External Dependencies**: Pure Win32 API + system DLLs

## Screenshots

```
┌─────────────────────────────────────────────────────────┐
│  VM Manager v4.0 — Memory Monitor & Suspicious Detector  │
│  [Overview] [Processes] [Charts] [Anomalies] [Suspicious]│
├─────────────────────────────────────────────────────────┤
│                                                         │
│  ┌───Gauge────┐  ┌───Gauge────┐  ┌──System Info──┐    │
│  │  Page File  │  │  Physical  │  │  Total: 32 GB │    │
│  │    38%      │  │    52%     │  │  Avail: 15 GB │    │
│  └─────────────┘  └────────────┘  └───────────────┘    │
│                                                         │
│  ┌──────────────── Chart Area ────────────────────────┐ │
│  │  Page File ——— Physical Memory -----                │ │
│  │  ▁▂▃▄▅▆▇█▇▆▅▄▃▂▁                                     │ │
│  └────────────────────────────────────────────────────┘ │
│                                                         │
│  ┌─ Process List ─────────────────────────────────────┐ │
│  │ # │ PID  │ Name          │ Commit   │ WS        │ │
│  │ 1 │ 1234 │ chrome.exe    │ 2.5 GB   │ 1.8 GB   │ │
│  └────────────────────────────────────────────────────┘ │
│  Status: Page File: 38% | Physical: 52% | Port: 18080  │
└─────────────────────────────────────────────────────────┘
```

## Build

### Prerequisites
- Windows 7 or later
- MinGW GCC (bundled at `../MinGW/bin/gcc.exe`)

### Compile
```batch
cd vm_manager_web
build.bat
```

Or manually:
```bash
gcc src/main.c src/vm_engine.c src/vm_db.c src/vm_http.c src/vm_desktop.c \
    -o vm_manager.exe -mwindows \
    -lpsapi -lws2_32 -lcrypt32 -lcomctl32 -lgdi32 \
    -O2 -s -Wall
```

### Link Flags
| Flag | Purpose |
|------|---------|
| `-mwindows` | Windows GUI (no console) |
| `-lpsapi` | `GetProcessMemoryInfo`, `EmptyWorkingSet` |
| `-lws2_32` | Winsock2 (HTTP server) |
| `-lcrypt32` | DPAPI (`CryptProtectData`) |
| `-lcomctl32` | Common Controls (listview, tab control) |
| `-lgdi32` | GDI (chart drawing) |

## Usage

```batch
# Desktop GUI (default, recommended)
vm_manager.exe

# Headless mode (background engine + web dashboard only)
vm_manager.exe /headless

# Console debug mode
vm_manager.exe /console
```

Open **http://127.0.0.1:18080** in your browser for the web dashboard.

### System Tray
- Right-click tray icon → Show Window / Manual Cleanup / Exit
- Double-click tray icon → Show window
- Close button → Minimize to tray (doesn't exit)

## Project Structure

```
vm_manager_web/
├── src/
│   ├── vm_common.h        # Shared types, constants, global declarations
│   ├── vm_engine.c        # Core: logging, snapshot, CPU/GPU, anomaly, cleanup
│   ├── vm_db.c            # DPAPI encryption & database layer
│   ├── vm_http.c          # HTTP server & JSON API endpoints
│   ├── vm_desktop.c       # Win32 desktop GUI with GDI charts
│   ├── main.c             # Entry point, mode dispatch
│   └── dashboard_html.h   # Embedded web dashboard SPA
├── vm_manager.exe         # Compiled binary
├── vm_manager.log         # Runtime log
├── vm_data.db             # Encrypted history database (DPAPI)
├── build.bat              # Build script
└── README.md
```

## API Endpoints (Web Dashboard)

| Endpoint | Description |
|----------|-------------|
| `GET /` | Dashboard SPA |
| `GET /api/status` | Current memory snapshot + top processes |
| `GET /api/history` | 24h point-in-time snapshots |
| `GET /api/actions` | Action log (cleanup operations) |
| `GET /api/cpu` | Per-process CPU usage |
| `GET /api/gpu` | GPU utilization & VRAM |
| `GET /api/anomalies` | Anomaly alerts |
| `GET /api/suspicious` | Suspicious process tracker |
| `GET /api/aggregated?range=week` | Aggregated history (day/week/month/year) |

## Architecture

```
┌───────────────────────┐
│       WinMain          │
│  ┌─────────┐ ┌───────┐│
│  │ Desktop  │ │ Head- ││
│  │   GUI    │ │ less  ││
│  └────┬─────┘ └───┬───┘│
│       └─────┬──────┘   │
│  ┌──────────▼────────┐ │
│  │   Engine Thread    │ │
│  │  - Snapshot (30s)  │ │
│  │  - CPU/GPU Sample  │ │
│  │  - Anomaly Detect  │ │
│  │  - Auto Cleanup    │ │
│  └────────┬───────────┘ │
│  ┌────────▼───────────┐ │
│  │    g_csData (CS)    │ │
│  │  - History buffer  │ │
│  │  - Latest snapshot │ │
│  │  - Action log      │ │
│  │  - Anomaly alerts  │ │
│  └──┬──────────┬──────┘ │
│     │          │        │
│  ┌──▼──┐ ┌─────▼──────┐│
│  │ HTTP│ │vm_data.db  ││
│  │ :800│ │(DPAPI enc) ││
│  └─────┘ └────────────┘│
└───────────────────────┘
```

## Configurable Parameters

Edit `src/vm_common.h` to adjust:

```c
#define CHECK_INTERVAL_SEC       30    // Snapshot interval
#define IDLE_THRESHOLD_SEC       300   // Idle threshold for cleanup
#define PAGE_FILE_THRESHOLD_PCT  85    // Page file % to trigger cleanup
#define CPU_HOG_THRESHOLD_PCT    50    // CPU anomaly threshold
#define MEM_HOG_THRESHOLD_MB     4096  // Memory anomaly threshold
#define SUSPICIOUS_GROWTH_MB_PER_SEC 10 // Suspicious growth detection
```

## License

MIT

---

**Note**: Requires running as Administrator for `SeDebugPrivilege` (needed for `EmptyWorkingSet` on some processes). Without admin rights, trimming may fail for protected processes but monitoring still works fully.
