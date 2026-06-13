#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "process_filter.h"

#define FILTER_BUFSIZE 2048

static char g_target_process_name[MAX_PATH] = {0};
static char g_final_filter_expr[FILTER_BUFSIZE] = {0};
static HANDLE g_hStartEvent = NULL;
static HANDLE g_hReadyEvent = NULL;
static HANDLE g_hWorkerThread = NULL;
static volatile BOOL g_bShouldExit = FALSE;

static int caseInsensitiveStrstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    int hlen = (int)strlen(haystack);
    int nlen = (int)strlen(needle);
    if (nlen == 0) return 1;
    if (hlen < nlen) return 0;
    for (int i = 0; i <= hlen - nlen; i++) {
        int match = 1;
        for (int j = 0; j < nlen; j++) {
            char hc = haystack[i + j];
            char nc = needle[j];
            if (hc >= 'A' && hc <= 'Z') hc += 32;
            if (nc >= 'A' && nc <= 'Z') nc += 32;
            if (hc != nc) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }
    return 0;
}

static int hasPort(USHORT port, USHORT *ports, int count) {
    for (int i = 0; i < count; i++) {
        if (ports[i] == port) return 1;
    }
    return 0;
}

static int getPortsForProcessSubstring(const char *processNameSub, USHORT *ports, int maxPorts) {
    DWORD matchingPids[1024];
    int matchingPidCount = 0;
    int portCount = 0;

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(pe);
        if (Process32First(hSnapshot, &pe)) {
            do {
                if (caseInsensitiveStrstr(pe.szExeFile, processNameSub)) {
                    if (matchingPidCount < 1024) {
                        matchingPids[matchingPidCount++] = pe.th32ProcessID;
                    }
                }
            } while (Process32Next(hSnapshot, &pe));
        }
        CloseHandle(hSnapshot);
    }

    if (matchingPidCount == 0) {
        return 0;
    }

    DWORD dwSize = 0;
    DWORD dwRet = 0;
    PMIB_UDPTABLE_OWNER_PID pTable = NULL;

    do {
        dwRet = GetExtendedUdpTable(pTable, &dwSize, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
        if (dwRet == ERROR_INSUFFICIENT_BUFFER) {
            if (pTable) free(pTable);
            pTable = (PMIB_UDPTABLE_OWNER_PID)malloc(dwSize);
            if (!pTable) break;
        } else {
            break;
        }
    } while (1);

    if (dwRet == NO_ERROR && pTable) {
        for (DWORD i = 0; i < pTable->dwNumEntries; i++) {
            DWORD pid = pTable->table[i].dwOwningPid;
            int isMatch = 0;
            for (int k = 0; k < matchingPidCount; k++) {
                if (matchingPids[k] == pid) {
                    isMatch = 1;
                    break;
                }
            }
            if (isMatch) {
                USHORT port = ntohs((USHORT)pTable->table[i].dwLocalPort);
                if (port > 0 && !hasPort(port, ports, portCount) && portCount < maxPorts) {
                    ports[portCount++] = port;
                }
            }
        }
    }
    if (pTable) free(pTable);

    dwSize = 0;
    dwRet = 0;
    PMIB_UDP6TABLE_OWNER_PID pTable6 = NULL;

    do {
        dwRet = GetExtendedUdpTable(pTable6, &dwSize, FALSE, AF_INET6, UDP_TABLE_OWNER_PID, 0);
        if (dwRet == ERROR_INSUFFICIENT_BUFFER) {
            if (pTable6) free(pTable6);
            pTable6 = (PMIB_UDP6TABLE_OWNER_PID)malloc(dwSize);
            if (!pTable6) break;
        } else {
            break;
        }
    } while (1);

    if (dwRet == NO_ERROR && pTable6) {
        for (DWORD i = 0; i < pTable6->dwNumEntries; i++) {
            DWORD pid = pTable6->table[i].dwOwningPid;
            int isMatch = 0;
            for (int k = 0; k < matchingPidCount; k++) {
                if (matchingPids[k] == pid) {
                    isMatch = 1;
                    break;
                }
            }
            if (isMatch) {
                USHORT port = ntohs((USHORT)pTable6->table[i].dwLocalPort);
                if (port > 0 && !hasPort(port, ports, portCount) && portCount < maxPorts) {
                    ports[portCount++] = port;
                }
            }
        }
    }
    if (pTable6) free(pTable6);

    return portCount;
}

static DWORD WINAPI ProcessFilterWorkerThread(LPVOID lpParam) {
    UNREFERENCED_PARAMETER(lpParam);
    USHORT ports[1024];

    while (1) {
        WaitForSingleObject(g_hStartEvent, INFINITE);

        if (g_bShouldExit) {
            break;
        }

        int count = getPortsForProcessSubstring(g_target_process_name, ports, 1024);

        if (count > 0) {
            g_final_filter_expr[0] = '\0';
            size_t current_len = 0;
            BOOL limit_reached = FALSE;
            for (int i = 0; i < count; i++) {
                if (i > 0) {
                    if (current_len + 4 >= FILTER_BUFSIZE) {
                        limit_reached = TRUE;
                        break;
                    }
                    int written = snprintf(g_final_filter_expr + current_len, FILTER_BUFSIZE - current_len, " || ");
                    if (written < 0 || (size_t)written >= FILTER_BUFSIZE - current_len) {
                        limit_reached = TRUE;
                        break;
                    }
                    current_len += written;
                }

                int written = snprintf(g_final_filter_expr + current_len, FILTER_BUFSIZE - current_len, "udp.DstPort == %u", ports[i]);
                if (written < 0 || (size_t)written >= FILTER_BUFSIZE - current_len) {
                    limit_reached = TRUE;
                    break;
                }
                current_len += written;
            }

            if (limit_reached) {
                snprintf(g_final_filter_expr, FILTER_BUFSIZE, "udp.DstPort == 0");
            }
        } else {
            snprintf(g_final_filter_expr, FILTER_BUFSIZE, "udp.DstPort == 0");
        }

        SetEvent(g_hReadyEvent);
    }
    return 0;
}

void processFilterInit(void) {
    g_bShouldExit = FALSE;
    g_hStartEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    g_hReadyEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    g_hWorkerThread = CreateThread(NULL, 0, ProcessFilterWorkerThread, NULL, 0, NULL);
}

void processFilterCleanup(void) {
    if (g_hWorkerThread) {
        g_bShouldExit = TRUE;
        if (g_hStartEvent) {
            SetEvent(g_hStartEvent); // Wake up the thread
        }

        // Wait gracefully for the thread to exit
        WaitForSingleObject(g_hWorkerThread, 1000);
        CloseHandle(g_hWorkerThread);
        g_hWorkerThread = NULL;
    }
    if (g_hStartEvent) {
        CloseHandle(g_hStartEvent);
        g_hStartEvent = NULL;
    }
    if (g_hReadyEvent) {
        CloseHandle(g_hReadyEvent);
        g_hReadyEvent = NULL;
    }
}

BOOL processFilterTrigger(const char *processNameSub) {
    if (!processNameSub || strlen(processNameSub) >= MAX_PATH) {
        return FALSE;
    }
    snprintf(g_target_process_name, sizeof(g_target_process_name), "%s", processNameSub);
    ResetEvent(g_hReadyEvent);
    SetEvent(g_hStartEvent);

    if (WaitForSingleObject(g_hReadyEvent, 2000) == WAIT_OBJECT_0) {
        return TRUE;
    }
    return FALSE;
}

const char* processFilterGetExpression(void) {
    return g_final_filter_expr;
}
