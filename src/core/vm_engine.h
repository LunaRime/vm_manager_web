/**
 * vm_engine.h — C core engine: public API
 *
 * This is the C interface that the C++ wrapper layer consumes.
 * All functions are thread-safe when used with the provided
 * synchronization primitives (CRITICAL_SECTION g_csData).
 */
#ifndef VM_ENGINE_H
#define VM_ENGINE_H

#include "vm_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Lifecycle ---- */
BOOL InitLogFile(void);
void Log(const char *format, ...);

/* ---- Privilege ---- */
BOOL EnableDebugPrivilege(void);

/* ---- Memory monitoring ---- */
DWORD GetIdleTimeMs(void);
DWORD GetPageFileUsagePct(void);
void CollectSnapshot(MemorySnapshot *snap);

/* ---- CPU sampling ---- */
void SampleProcessCpu(void);

/* ---- GPU monitoring ---- */
void InitGpuMonitoring(void);
void QueryGpuInfo(void);
void ShutdownGpuMonitoring(void);

/* ---- Anomaly detection ---- */
void DetectAnomalies(void);
void TrackSuspiciousProcesses(void);

/* ---- Memory trimming ---- */
BOOL TrimProcessWorkingSet(DWORD pid);

/* ---- Aggregation ---- */
void UpdateAggregations(MemorySnapshot *snap);

/* ---- High-level operations ---- */
void UpdateLatestSnapshot(void);
void CheckAndAct(void);

#ifdef __cplusplus
}
#endif

#endif /* VM_ENGINE_H */
