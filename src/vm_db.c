/**
 * vm_db.c — Encrypted database layer using DPAPI.
 *           Each record is JSON -> DPAPI encrypt -> [4-byte len][ciphertext]
 */
#include "vm_common.h"

BOOL EncryptData(const BYTE *plainData, DWORD plainLen,
                 BYTE **cipherData, DWORD *cipherLen) {
    DATA_BLOB in, out;
    in.pbData = (BYTE *)plainData;
    in.cbData = plainLen;

    if (!CryptProtectData(&in, L"VMManager", NULL, NULL, NULL,
                           CRYPTPROTECT_LOCAL_MACHINE | CRYPTPROTECT_UI_FORBIDDEN,
                           &out))
        return FALSE;

    *cipherData = (BYTE *)HeapAlloc(GetProcessHeap(), 0, out.cbData);
    if (!*cipherData) { LocalFree(out.pbData); return FALSE; }
    memcpy(*cipherData, out.pbData, out.cbData);
    *cipherLen = out.cbData;
    LocalFree(out.pbData);
    return TRUE;
}

BOOL DecryptData(const BYTE *cipherData, DWORD cipherLen,
                 BYTE **plainData, DWORD *plainLen) {
    DATA_BLOB in, out;
    in.pbData = (BYTE *)cipherData;
    in.cbData = cipherLen;

    if (!CryptUnprotectData(&in, NULL, NULL, NULL, NULL,
                            CRYPTPROTECT_UI_FORBIDDEN, &out))
        return FALSE;

    *plainData = (BYTE *)HeapAlloc(GetProcessHeap(), 0, out.cbData + 1);
    if (!*plainData) { LocalFree(out.pbData); return FALSE; }
    memcpy(*plainData, out.pbData, out.cbData);
    (*plainData)[out.cbData] = '\0';
    *plainLen = out.cbData;
    LocalFree(out.pbData);
    return TRUE;
}

BOOL InitDatabase(void) {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, sizeof(path));
    char *lastSlash = strrchr(path, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    strncat(path, DB_FILE_NAME, sizeof(path) - strlen(path) - 1);

    g_hDbFile = CreateFileA(path, FILE_APPEND_DATA | FILE_READ_DATA,
        FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    return (g_hDbFile != INVALID_HANDLE_VALUE);
}

BOOL LoadDatabase(void) {
    if (g_hDbFile == INVALID_HANDLE_VALUE) return FALSE;

    LARGE_INTEGER liSize;
    if (!GetFileSizeEx(g_hDbFile, &liSize)) return TRUE;
    if (liSize.QuadPart == 0) return TRUE;

    SetFilePointer(g_hDbFile, 0, NULL, FILE_BEGIN);

    DWORD totalRead = 0;
    while ((LONGLONG)totalRead < liSize.QuadPart) {
        DWORD blobLen, read;
        if (!ReadFile(g_hDbFile, &blobLen, 4, &read, NULL) || read != 4) break;
        if (blobLen == 0 || blobLen > 1024 * 1024) break;

        BYTE *cipherData = (BYTE *)HeapAlloc(GetProcessHeap(), 0, blobLen);
        if (!cipherData) break;
        if (!ReadFile(g_hDbFile, cipherData, blobLen, &read, NULL) || read != blobLen) {
            HeapFree(GetProcessHeap(), 0, cipherData);
            break;
        }

        BYTE *plainData;
        DWORD plainLen;
        if (DecryptData(cipherData, blobLen, &plainData, &plainLen)) {
            if (g_snapshotCount < MAX_HISTORY_SNAPSHOTS) {
                MemorySnapshot *snap = &g_snapshots[g_snapshotCount];
                memset(snap, 0, sizeof(*snap));
                char *json = (char *)plainData;

                /* Simple JSON field parser (no library dependency) */
                char *p = json;
                while (p && *p) {
                    char *colon = strchr(p, ':');
                    if (!colon) break;
                    char *keyStart = strchr(p, '"');
                    if (!keyStart || keyStart >= colon) break;
                    char *keyEnd = strchr(keyStart + 1, '"');
                    if (!keyEnd || keyEnd >= colon) break;

                    int keyLen = (int)(keyEnd - keyStart - 1);
                    char key[32] = {0};
                    if (keyLen > 30) keyLen = 30;
                    memcpy(key, keyStart + 1, keyLen);

                    char *valStart = colon + 1;
                    while (*valStart == ' ' || *valStart == ':') valStart++;

                    if      (strcmp(key, "t")  == 0) snap->timestamp     = (time_t)atoll(valStart);
                    else if (strcmp(key, "pf") == 0) snap->pageFilePct   = (DWORD)atoi(valStart);
                    else if (strcmp(key, "ph") == 0) snap->physLoad      = (DWORD)atoi(valStart);
                    else if (strcmp(key, "tp") == 0) snap->totalPhys     = _atoi64(valStart);
                    else if (strcmp(key, "ap") == 0) snap->availPhys     = _atoi64(valStart);
                    else if (strcmp(key, "tf") == 0) snap->totalPageFile = _atoi64(valStart);
                    else if (strcmp(key, "af") == 0) snap->availPageFile = _atoi64(valStart);
                    else if (strcmp(key, "id") == 0) snap->idleSeconds   = (DWORD)atoi(valStart);
                    else if (strcmp(key, "np") == 0) snap->numProcesses  = atoi(valStart);
                    else if (strcmp(key, "px") == 0) {
                        char *px = (char *)valStart;
                        if (*px == '"') px++;
                        int pi = 0;
                        char *tok = strtok(px, "|");
                        while (tok && pi < MAX_TOP_PROCESSES) {
                            ProcessInfo *proc = &snap->topProcesses[pi];
                            char *parts[4]; int pc = 0;
                            char *sp = tok;
                            parts[pc++] = sp;
                            while (*sp && pc < 4) {
                                if (*sp == ',') { *sp = '\0'; parts[pc++] = sp + 1; }
                                sp++;
                            }
                            if (pc >= 4) {
                                proc->pid = (DWORD)atoi(parts[0]);
                                strncpy(proc->name, parts[1], sizeof(proc->name) - 1);
                                proc->commitSize = (SIZE_T)_atoi64(parts[2]);
                                proc->workingSet = (SIZE_T)_atoi64(parts[3]);
                            }
                            pi++; tok = strtok(NULL, "|");
                        }
                        snap->numProcesses = pi;
                    }
                    p = strchr(valStart, ',');
                    if (!p) p = strchr(valStart, '}');
                    if (p) p++;
                }
                g_snapshotCount++;
                memcpy(&g_latestSnapshot, snap, sizeof(MemorySnapshot));
            }
            HeapFree(GetProcessHeap(), 0, plainData);
        }
        HeapFree(GetProcessHeap(), 0, cipherData);
        totalRead += 4 + blobLen;
    }

    Log("Loaded %d history records from encrypted database", g_snapshotCount);
    return TRUE;
}

BOOL AppendSnapshot(MemorySnapshot *snap) {
    if (g_hDbFile == INVALID_HANDLE_VALUE) return FALSE;

    char json[16384];
    int off = snprintf(json, sizeof(json),
        "{\"t\":%I64d,\"pf\":%lu,\"ph\":%lu,\"tp\":%I64u,\"ap\":%I64u,"
        "\"tf\":%I64u,\"af\":%I64u,\"id\":%lu,\"np\":%d,\"px\":\"",
        (long long)snap->timestamp, snap->pageFilePct, snap->physLoad,
        (unsigned long long)snap->totalPhys,
        (unsigned long long)snap->availPhys,
        (unsigned long long)snap->totalPageFile,
        (unsigned long long)snap->availPageFile,
        snap->idleSeconds, snap->numProcesses);

    int i;
    for (i = 0; i < snap->numProcesses && off < (int)sizeof(json) - 200; i++) {
        ProcessInfo *proc = &snap->topProcesses[i];
        if (i > 0) off += snprintf(json + off, sizeof(json) - off, "|");
        off += snprintf(json + off, sizeof(json) - off,
            "%lu,%s,%I64u,%I64u",
            proc->pid, proc->name,
            (unsigned long long)proc->commitSize,
            (unsigned long long)proc->workingSet);
    }
    snprintf(json + off, sizeof(json) - off, "\"}");

    BYTE *cipherData;
    DWORD cipherLen;
    if (!EncryptData((BYTE *)json, (DWORD)strlen(json), &cipherData, &cipherLen))
        return FALSE;

    DWORD written;
    SetFilePointer(g_hDbFile, 0, NULL, FILE_END);
    WriteFile(g_hDbFile, &cipherLen, 4, &written, NULL);
    WriteFile(g_hDbFile, cipherData, cipherLen, &written, NULL);
    FlushFileBuffers(g_hDbFile);

    HeapFree(GetProcessHeap(), 0, cipherData);
    return TRUE;
}

BOOL AppendActionLog(ActionRecord *action) {
    if (g_hDbFile == INVALID_HANDLE_VALUE) return FALSE;

    char json[2048];
    snprintf(json, sizeof(json),
        "{\"type\":\"action\",\"t\":%I64d,\"bf\":%lu,\"af\":%lu,"
        "\"tc\":%d,\"fc\":%d,\"desc\":\"%s\"}",
        (long long)action->timestamp,
        action->pageFileBefore, action->pageFileAfter,
        action->trimmedCount, action->failedCount,
        action->description);

    BYTE *cipherData;
    DWORD cipherLen;
    if (!EncryptData((BYTE *)json, (DWORD)strlen(json), &cipherData, &cipherLen))
        return FALSE;

    DWORD written;
    SetFilePointer(g_hDbFile, 0, NULL, FILE_END);
    WriteFile(g_hDbFile, &cipherLen, 4, &written, NULL);
    WriteFile(g_hDbFile, cipherData, cipherLen, &written, NULL);
    FlushFileBuffers(g_hDbFile);

    HeapFree(GetProcessHeap(), 0, cipherData);
    return TRUE;
}
