/**
 * vm_db.h — C core: encrypted database public API
 *
 * DPAPI-encrypted persistent storage for snapshots and action logs.
 * Database format: [4-byte length][ciphertext] per record.
 */
#ifndef VM_DB_H
#define VM_DB_H

#include "vm_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Low-level encryption ---- */
BOOL EncryptData(const BYTE *plainData, DWORD plainLen,
                 BYTE **cipherData, DWORD *cipherLen);
BOOL DecryptData(const BYTE *cipherData, DWORD cipherLen,
                 BYTE **plainData, DWORD *plainLen);

/* ---- Database lifecycle ---- */
BOOL InitDatabase(void);
BOOL LoadDatabase(void);

/* ---- Persistence ---- */
BOOL AppendSnapshot(MemorySnapshot *snap);
BOOL AppendActionLog(ActionRecord *action);

#ifdef __cplusplus
}
#endif

#endif /* VM_DB_H */
