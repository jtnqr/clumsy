#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <winsock2.h>
#include <Windows.h>
#include "iup.h"
#include "common.h"
#include "process_filter.h"
#include "ui_components.h"
#include "config_manager.h"
#include "hotkey_manager.h"
#include "ui_callbacks.h"

#define noticeLabel statusLabel

// ui logics
void showStatus(const char *line) {
    IupStoreAttribute(statusLabel, "TITLE", line); 
}

// in fact only 32bit binary would run on 64 bit os
// if this happens pop out message box and exit
static BOOL check32RunningOn64(HWND hWnd) {
    BOOL is64ret;
    typedef BOOL (WINAPI *LPFN_ISWOW64PROCESS) (HANDLE, PBOOL);
    LPFN_ISWOW64PROCESS fnIsWow64Process = (LPFN_ISWOW64PROCESS) GetProcAddress(
        GetModuleHandle(TEXT("kernel32")), "IsWow64Process");
    if(NULL != fnIsWow64Process) {
        if (!fnIsWow64Process(GetCurrentProcess(),&is64ret)) {
            LOG("IsWow64Process failed.");
            return FALSE;
        }
        if (is64ret) {
            MessageBox(hWnd, (LPCSTR)"You're running a 32bit clumsy on a 64bit Windows.\n"
                "This doesn't work, please download the 64bit version of clumsy.",
                (LPCSTR)"Aborting", MB_OK);
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL checkIsRunning(void) {
    HANDLE hStartEvent = CreateEvent(NULL, TRUE, FALSE, "clumsy_event");
    if (hStartEvent == NULL)
        return TRUE;

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hStartEvent);
        hStartEvent = NULL;
        return TRUE;
    }

    return FALSE;
}

int uiOnDialogShow(Ihandle *ih, int state) {
    // only need to process on show
    HWND hWnd;
    BOOL exit;
    HICON icon;
    HINSTANCE hInstance;
    if (state != IUP_SHOW) return IUP_DEFAULT;
    hWnd = (HWND)IupGetAttribute(ih, "HWND");
    hInstance = GetModuleHandle(NULL);

    // set application icon
    icon = LoadIcon(hInstance, "CLUMSY_ICON");
    SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
    SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);

    // Register global hotkey for toggle
    mainHwnd = hWnd;
    if (RegisterHotKey(hWnd, HOTKEY_ID, hotkeyModifiers, hotkeyVirtualKey)) {
        char hotkeyStr[64];
        hotkeyRegistered = TRUE;
        LOG("Hotkey registered successfully (mods=0x%x key=0x%x)", hotkeyModifiers, hotkeyVirtualKey);
        
        // Update hotkey label to show the registered shortcut
        formatHotkeyString(hotkeyStr, sizeof(hotkeyStr));
        IupStoreAttribute(hotkeyLabel, "TITLE", hotkeyStr);
        
        // Update button tooltip to include the hotkey
        {
            char tipStr[128];
            snprintf(tipStr, sizeof(tipStr), "Start/Stop packet filtering (Hotkey: %s)", hotkeyStr);
            IupStoreAttribute(filterButton, "TIP", tipStr);
        }
        
        // Subclass window to handle WM_HOTKEY messages
        originalWndProc = (WNDPROC)SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)hotkeyWndProc);
        if (originalWndProc) {
            LOG("Window subclassed for hotkey handling");
        }
    } else {
        LOG("Failed to register hotkey (error=%lu)", GetLastError());
    }

    exit = checkIsRunning();
    if (exit) {
        MessageBox(hWnd, (LPCSTR)"Theres' already an instance of clumsy running.",
            (LPCSTR)"Aborting", MB_OK);
        return IUP_CLOSE;
    }

#ifdef _WIN32
    exit = check32RunningOn64(hWnd);
    if (exit) {
        return IUP_CLOSE;
    }
#endif

    // try elevate and decides whether to exit
    // Only be silent if this is actual command-line parameterized mode, not state restore
    exit = tryElevate(hWnd, parameterized && !stateLoaded);

    if (!exit && parameterized) {
        setFromParameter(filterText, "VALUE", "filter");
        // Only auto-start if this is command-line parameterized, NOT state restore
        if (!stateLoaded) {
            LOG("is parameterized, start filtering upon execution.");
            uiStartCb(filterButton);
        } else {
            LOG("State restored, NOT auto-starting (safety)");
        }
    }

    return exit ? IUP_CLOSE : IUP_DEFAULT;
}

int uiStartCb(Ihandle *ih) {
    char buf[MSG_BUFSIZE];
    int ix;
    const char *filterExpr;
    UNREFERENCED_PARAMETER(ih);

    // 1. Duration input sanitization validation gate
    int ms = 0;
    if (uiIsDurationEnabled()) {
        ms = uiGetDurationValue();
    }

    const char *manualFilter = IupGetAttribute(filterText, "VALUE");
    static char combinedFilter[4096];

    // Populate global filter coordination variables
    if (manualFilter) {
        strncpy(g_manual_filter, manualFilter, sizeof(g_manual_filter) - 1);
        g_manual_filter[sizeof(g_manual_filter) - 1] = '\0';
    } else {
        g_manual_filter[0] = '\0';
    }
    g_process_filter_enabled = uiIsProcessFilterEnabled();

    if (g_process_filter_enabled) {
        const char *procTarget = uiGetProcessFilterTarget();
        // Trigger process filter lookup
        if (processFilterTrigger(procTarget)) {
            const char *procExpr = processFilterGetExpression();
            if (manualFilter && strlen(manualFilter) > 0) {
                snprintf(combinedFilter, sizeof(combinedFilter), "(%s) && (%s)", manualFilter, procExpr);
            } else {
                snprintf(combinedFilter, sizeof(combinedFilter), "%s", procExpr);
            }
            filterExpr = combinedFilter;
            LOG("Process filter combined; final filter: %s", filterExpr);
        } else {
            IupMessage("Error", "Process filter lookup timed out or failed to discover active ports!");
            showStatus("Process filter lookup timed out!");
            return IUP_DEFAULT;
        }
    } else {
        filterExpr = manualFilter;
    }

    if (divertStart(filterExpr, buf) == 0) {
        showStatus(buf);
        return IUP_DEFAULT;
    }

    // Disable process filter inputs during active filtering
    uiSetProcessFilterActive(FALSE);

    // Reset packet counters for all modules
    for (ix = 0; ix < MODULE_CNT; ++ix) {
        InterlockedExchange(&(modules[ix]->processCount), 0);
        IupSetAttribute(modules[ix]->countLabel, "TITLE", "");
    }

    // successfully started
    showStatus("Started filtering. Enable functionalities to take effect.");
    IupSetAttribute(dialog, "TITLE", "clumsy " CLUMSY_VERSION " (running)");
    IupSetAttribute(filterText, "ACTIVE", "NO");
    IupSetAttribute(filterButton, "TITLE", "Stop");
    IupSetCallback(filterButton, "ACTION", uiStopCb);
    IupSetAttribute(timer, "RUN", "YES");

    if (uiIsDurationEnabled()) {
        if (ms > 0) {
            char timeBuf[32];
            snprintf(timeBuf, sizeof(timeBuf), "%d", ms);
            IupStoreAttribute(durationTimer, "TIME", timeBuf);
            IupSetAttribute(durationTimer, "RUN", "YES");
        }
    }

    return IUP_DEFAULT;
}

int uiStopCb(Ihandle *ih) {
    int ix;
    UNREFERENCED_PARAMETER(ih);
    
    if (durationTimer) {
        IupSetAttribute(durationTimer, "RUN", "NO");
    }

    // try stopping
    IupSetAttribute(filterButton, "ACTIVE", "NO");
    IupFlush(); // flush to show disabled state
    if (uiIsProcessFilterEnabled()) {
        processFilterStop();
    }
    divertStop();

    IupSetAttribute(dialog, "TITLE", "clumsy " CLUMSY_VERSION);
    IupSetAttribute(filterText, "ACTIVE", "YES");
    uiSetProcessFilterActive(TRUE);
    IupSetAttribute(filterButton, "TITLE", "Start");
    IupSetAttribute(filterButton, "ACTIVE", "YES");
    IupSetCallback(filterButton, "ACTION", uiStartCb);

    // stop timer and clean up icons (keep counters visible until next start)
    IupSetAttribute(timer, "RUN", "NO");
    for (ix = 0; ix < MODULE_CNT; ++ix) {
        modules[ix]->processTriggered = 0; // use = here since is threads already stopped
        IupSetAttribute(modules[ix]->iconHandle, "IMAGE", "none_icon");
    }
    sendState = SEND_STATUS_NONE;
    IupSetAttribute(stateIcon, "IMAGE", "none_icon");

    showStatus("Stopped. To begin again, edit criteria and click Start.");
    return IUP_DEFAULT;
}

// Toggle filtering on/off (called by hotkey)
void toggleFiltering(void) {
    const char* title = IupGetAttribute(filterButton, "TITLE");
    if (strcmp(title, "Start") == 0) {
        uiStartCb(filterButton);
    } else {
        uiStopCb(filterButton);
    }
}

static int uiToggleControls(Ihandle *ih, int state) {
    Ihandle *controls = (Ihandle*)IupGetAttribute(ih, CONTROLS_HANDLE);
    short *target = (short*)IupGetAttribute(ih, SYNCED_VALUE);
    int controlsActive = IupGetInt(controls, "ACTIVE");
    if (controlsActive && !state) {
        IupSetAttribute(controls, "ACTIVE", "NO");
        InterlockedExchange16(target, I2S(state));
    } else if (!controlsActive && state) {
        IupSetAttribute(controls, "ACTIVE", "YES");
        InterlockedExchange16(target, I2S(state));
    }

    return IUP_DEFAULT;
}

int uiTimerCb(Ihandle *ih) {
    int ix;
    UNREFERENCED_PARAMETER(ih);
    for (ix = 0; ix < MODULE_CNT; ++ix) {
        if (modules[ix]->processTriggered) {
            IupSetAttribute(modules[ix]->iconHandle, "IMAGE", "doing_icon");
            InterlockedAnd16(&(modules[ix]->processTriggered), 0);
        } else {
            IupSetAttribute(modules[ix]->iconHandle, "IMAGE", "none_icon");
        }
        
        // Update count label with current processCount
        {
            char countBuf[16];
            LONG count = modules[ix]->processCount;
            if (count > 0) {
                snprintf(countBuf, sizeof(countBuf), "%ld", count);
                IupStoreAttribute(modules[ix]->countLabel, "TITLE", countBuf);
            }
        }
    }

    // update global send status icon
    switch (sendState)
    {
    case SEND_STATUS_NONE:
        IupSetAttribute(stateIcon, "IMAGE", "none_icon");
        break;
    case SEND_STATUS_SEND:
        IupSetAttribute(stateIcon, "IMAGE", "doing_icon");
        InterlockedAnd16(&sendState, SEND_STATUS_NONE);
        break;
    case SEND_STATUS_FAIL:
        IupSetAttribute(stateIcon, "IMAGE", "error_icon");
        InterlockedAnd16(&sendState, SEND_STATUS_NONE);
        break;
    }

    return IUP_DEFAULT;
}

int uiTimeoutCb(Ihandle *ih) {
    UNREFERENCED_PARAMETER(ih);
    return IUP_CLOSE;
 }

int uiDurationTimerCb(Ihandle *ih) {
    UNREFERENCED_PARAMETER(ih);
    if (durationTimer) {
        IupSetAttribute(durationTimer, "RUN", "NO");
    }
    uiStopCb(NULL);
    return IUP_DEFAULT;
}

static void uiSetModuleState(const char *shortName, BOOL enabled, BOOL inbound, BOOL outbound, const char *valueStr) {
    char nameBuf[128];
    snprintf(nameBuf, sizeof(nameBuf), "toggle_%s", shortName);
    Ihandle *toggle = IupGetHandle(nameBuf);
    snprintf(nameBuf, sizeof(nameBuf), "controls_%s", shortName);
    Ihandle *controls = IupGetHandle(nameBuf);

    if (toggle && controls) {
        IupSetAttribute(toggle, "VALUE", enabled ? "ON" : "OFF");
        uiToggleControls(toggle, enabled ? 1 : 0);

        Ihandle *chk_inbound = (Ihandle*)IupGetAttribute(controls, "INBOUND_CHECKBOX");
        Ihandle *chk_outbound = (Ihandle*)IupGetAttribute(controls, "OUTBOUND_CHECKBOX");
        
        if (chk_inbound) {
            IupSetAttribute(chk_inbound, "VALUE", inbound ? "ON" : "OFF");
            short *inboundPtr = (short*)IupGetAttribute(chk_inbound, SYNCED_VALUE);
            if (inboundPtr) InterlockedExchange16(inboundPtr, I2S(inbound ? 1 : 0));
        }
        if (chk_outbound) {
            IupSetAttribute(chk_outbound, "VALUE", outbound ? "ON" : "OFF");
            short *outboundPtr = (short*)IupGetAttribute(chk_outbound, SYNCED_VALUE);
            if (outboundPtr) InterlockedExchange16(outboundPtr, I2S(outbound ? 1 : 0));
        }

        if (valueStr) {
            if (strcmp(shortName, "drop") == 0) {
                Ihandle *txt_chance = (Ihandle*)IupGetAttribute(controls, "CHANCE_INPUT");
                if (txt_chance) {
                    IupSetAttribute(txt_chance, "VALUE", valueStr);
                    short *chancePtr = (short*)IupGetAttribute(txt_chance, SYNCED_VALUE);
                    if (chancePtr) {
                        float fVal = (float)atof(valueStr);
                        InterlockedExchange16(chancePtr, (short)(fVal * 100));
                    }
                }
            } else if (strcmp(shortName, "lag") == 0) {
                Ihandle *txt_time = (Ihandle*)IupGetAttribute(controls, "TIME_INPUT");
                if (txt_time) {
                    IupSetAttribute(txt_time, "VALUE", valueStr);
                    short *timePtr = (short*)IupGetAttribute(txt_time, SYNCED_VALUE);
                    if (timePtr) {
                        int iVal = atoi(valueStr);
                        InterlockedExchange16(timePtr, (short)iVal);
                    }
                }
            } else if (strcmp(shortName, "bandwidth") == 0) {
                Ihandle *txt_limit = (Ihandle*)IupGetAttribute(controls, "BANDWIDTH_INPUT");
                if (txt_limit) {
                    IupSetAttribute(txt_limit, "VALUE", valueStr);
                    LONG *limitPtr = (LONG*)IupGetAttribute(txt_limit, SYNCED_VALUE);
                    if (limitPtr) {
                        int iVal = atoi(valueStr);
                        InterlockedExchange(limitPtr, iVal);
                    }
                }
            }
        }
    }
}

void uiApplyProfile(ProfileRecord *p) {
    // 1. Set filter text
    IupSetAttribute(filterText, "VALUE", p->filter);

    // 2. Set process filter settings
    uiSetProcessFilterTarget(p->procFilterTarget);
    uiSetProcessFilterEnabled(p->procFilterEnabled);
    uiSetDurationValue(p->durationValueMs);
    uiSetDurationEnabled(p->durationEnabled);

    // 3. Set modules
    for (int i = 0; i < MODULE_CNT; i++) {
        Module *m = modules[i];
        const char *name = m->shortName;
        
        char nameBuf[128];
        snprintf(nameBuf, sizeof(nameBuf), "toggle_%s", name);
        Ihandle *toggle = IupGetHandle(nameBuf);
        snprintf(nameBuf, sizeof(nameBuf), "controls_%s", name);
        Ihandle *controls = IupGetHandle(nameBuf);

        if (!toggle || !controls) continue;

        BOOL enabled = FALSE;
        BOOL inbound = TRUE;
        BOOL outbound = TRUE;
        const char *val1 = NULL;
        const char *val2 = NULL;
        BOOL boolVal = FALSE;

        // Fetch settings from ProfileRecord based on module name
        if (strcmp(name, "lag") == 0) {
            enabled = p->lag.enabled;
            inbound = p->lag.inbound;
            outbound = p->lag.outbound;
            val1 = p->lag.time;
        } else if (strcmp(name, "drop") == 0) {
            enabled = p->drop.enabled;
            inbound = p->drop.inbound;
            outbound = p->drop.outbound;
            val1 = p->drop.chance;
        } else if (strcmp(name, "throttle") == 0) {
            enabled = p->throttle.enabled;
            inbound = p->throttle.inbound;
            outbound = p->throttle.outbound;
            val1 = p->throttle.chance;
            val2 = p->throttle.frame;
            boolVal = p->throttle.drop;
        } else if (strcmp(name, "duplicate") == 0) {
            enabled = p->duplicate.enabled;
            inbound = p->duplicate.inbound;
            outbound = p->duplicate.outbound;
            val1 = p->duplicate.chance;
            val2 = p->duplicate.count;
        } else if (strcmp(name, "ood") == 0) {
            enabled = p->ood.enabled;
            inbound = p->ood.inbound;
            outbound = p->ood.outbound;
            val1 = p->ood.chance;
        } else if (strcmp(name, "tamper") == 0) {
            enabled = p->tamper.enabled;
            inbound = p->tamper.inbound;
            outbound = p->tamper.outbound;
            val1 = p->tamper.chance;
            boolVal = p->tamper.checksum;
        } else if (strcmp(name, "reset") == 0) {
            enabled = p->reset.enabled;
            inbound = p->reset.inbound;
            outbound = p->reset.outbound;
            val1 = p->reset.chance;
        } else if (strcmp(name, "bandwidth") == 0) {
            enabled = p->bandwidth.enabled;
            inbound = p->bandwidth.inbound;
            outbound = p->bandwidth.outbound;
            val1 = p->bandwidth.limit;
        }

        // Apply enabled state
        IupSetAttribute(toggle, "VALUE", enabled ? "ON" : "OFF");
        IupSetAttribute(controls, "ACTIVE", enabled ? "YES" : "NO");
        short *enabledPtr = (short*)IupGetAttribute(toggle, SYNCED_VALUE);
        if (enabledPtr) {
            InterlockedExchange16(enabledPtr, I2S(enabled ? 1 : 0));
        }

        // Apply inbound/outbound checkboxes
        Ihandle *chk_inbound = (Ihandle*)IupGetAttribute(controls, "INBOUND_CHECKBOX");
        Ihandle *chk_outbound = (Ihandle*)IupGetAttribute(controls, "OUTBOUND_CHECKBOX");
        if (chk_inbound) {
            IupSetAttribute(chk_inbound, "VALUE", inbound ? "ON" : "OFF");
            short *inboundPtr = (short*)IupGetAttribute(chk_inbound, SYNCED_VALUE);
            if (inboundPtr) InterlockedExchange16(inboundPtr, I2S(inbound ? 1 : 0));
        }
        if (chk_outbound) {
            IupSetAttribute(chk_outbound, "VALUE", outbound ? "ON" : "OFF");
            short *outboundPtr = (short*)IupGetAttribute(chk_outbound, SYNCED_VALUE);
            if (outboundPtr) InterlockedExchange16(outboundPtr, I2S(outbound ? 1 : 0));
        }

        // Apply primary value
        if (val1) {
            if (strcmp(name, "drop") == 0 || strcmp(name, "throttle") == 0 || strcmp(name, "duplicate") == 0 ||
                strcmp(name, "ood") == 0 || strcmp(name, "tamper") == 0 || strcmp(name, "reset") == 0) {
                Ihandle *chance_input = (Ihandle*)IupGetAttribute(controls, "CHANCE_INPUT");
                if (chance_input) {
                    IupSetAttribute(chance_input, "VALUE", val1);
                    short *chancePtr = (short*)IupGetAttribute(chance_input, SYNCED_VALUE);
                    if (chancePtr) {
                        float fVal = (float)atof(val1);
                        InterlockedExchange16(chancePtr, (short)(fVal * 100));
                    }
                }
            } else if (strcmp(name, "lag") == 0) {
                Ihandle *time_input = (Ihandle*)IupGetAttribute(controls, "TIME_INPUT");
                if (time_input) {
                    IupSetAttribute(time_input, "VALUE", val1);
                    short *timePtr = (short*)IupGetAttribute(time_input, SYNCED_VALUE);
                    if (timePtr) {
                        int iVal = atoi(val1);
                        InterlockedExchange16(timePtr, (short)iVal);
                    }
                }
            } else if (strcmp(name, "bandwidth") == 0) {
                Ihandle *bw_input = (Ihandle*)IupGetAttribute(controls, "BANDWIDTH_INPUT");
                if (bw_input) {
                    IupSetAttribute(bw_input, "VALUE", val1);
                    LONG *bwPtr = (LONG*)IupGetAttribute(bw_input, SYNCED_VALUE);
                    if (bwPtr) {
                        int iVal = atoi(val1);
                        InterlockedExchange(bwPtr, iVal);
                    }
                }
            }
        }

        // Apply module-specific inputs
        if (strcmp(name, "throttle") == 0) {
            if (val2) {
                Ihandle *frame_input = (Ihandle*)IupGetAttribute(controls, "FRAME_INPUT");
                if (frame_input) {
                    IupSetAttribute(frame_input, "VALUE", val2);
                    short *framePtr = (short*)IupGetAttribute(frame_input, SYNCED_VALUE);
                    if (framePtr) {
                        int iVal = atoi(val2);
                        InterlockedExchange16(framePtr, (short)iVal);
                    }
                }
            }
            Ihandle *drop_chk = (Ihandle*)IupGetAttribute(controls, "DROP_THROTTLED_CHECKBOX");
            if (drop_chk) {
                IupSetAttribute(drop_chk, "VALUE", boolVal ? "ON" : "OFF");
                short *dropPtr = (short*)IupGetAttribute(drop_chk, SYNCED_VALUE);
                if (dropPtr) InterlockedExchange16(dropPtr, I2S(boolVal ? 1 : 0));
            }
        } else if (strcmp(name, "duplicate") == 0) {
            if (val2) {
                Ihandle *count_input = (Ihandle*)IupGetAttribute(controls, "COUNT_INPUT");
                if (count_input) {
                    IupSetAttribute(count_input, "VALUE", val2);
                    short *countPtr = (short*)IupGetAttribute(count_input, SYNCED_VALUE);
                    if (countPtr) {
                        int iVal = atoi(val2);
                        InterlockedExchange16(countPtr, (short)iVal);
                    }
                }
            }
        } else if (strcmp(name, "tamper") == 0) {
            Ihandle *checksum_chk = (Ihandle*)IupGetAttribute(controls, "CHECKSUM_CHECKBOX");
            if (checksum_chk) {
                IupSetAttribute(checksum_chk, "VALUE", boolVal ? "ON" : "OFF");
                short *chkPtr = (short*)IupGetAttribute(checksum_chk, SYNCED_VALUE);
                if (chkPtr) InterlockedExchange16(chkPtr, I2S(boolVal ? 1 : 0));
            }
        }
    }
}

int uiListSelectCb(Ihandle *ih, char *text, int item, int state) {
    UNREFERENCED_PARAMETER(text);
    UNREFERENCED_PARAMETER(ih);
    if (state == 1) {
        uiApplyProfile(&filters[item-1]);
    }
    return IUP_DEFAULT;
}

int uiFilterTextCb(Ihandle *ih)  {
    UNREFERENCED_PARAMETER(ih);
    // unselect list
    IupSetAttribute(filterSelectList, "VALUE", "0");
    return IUP_DEFAULT;
}

void uiSetupModule(Module *module, Ihandle *parent) {
    Ihandle *groupBox, *toggle, *controls, *icon, *countLabel;
    const char *tooltip = NULL;
    
    // Tooltip descriptions for each module
    if (strcmp(module->shortName, "lag") == 0) {
        tooltip = "Delay packets by a specified time (ms)";
    } else if (strcmp(module->shortName, "drop") == 0) {
        tooltip = "Randomly drop packets based on chance";
    } else if (strcmp(module->shortName, "throttle") == 0) {
        tooltip = "Block packets for a time frame, then release all at once";
    } else if (strcmp(module->shortName, "duplicate") == 0) {
        tooltip = "Duplicate packets a specified number of times";
    } else if (strcmp(module->shortName, "ood") == 0) {
        tooltip = "Reorder packets to simulate out-of-order delivery";
    } else if (strcmp(module->shortName, "tamper") == 0) {
        tooltip = "Randomly modify packet payload bytes";
    } else if (strcmp(module->shortName, "reset") == 0) {
        tooltip = "Send TCP RST to reset connections";
    } else if (strcmp(module->shortName, "bandwidth") == 0) {
        tooltip = "Limit bandwidth to specified KB/s";
    }
    
    groupBox = IupHbox(
        icon = IupLabel(NULL),
        toggle = IupToggle(module->displayName, NULL),
        countLabel = IupLabel(""),
        IupFill(),
        controls = module->setupUIFunc(),
        NULL
    );
    IupSetAttribute(groupBox, "EXPAND", "HORIZONTAL");
    IupSetAttribute(groupBox, "ALIGNMENT", "ACENTER");
    IupSetAttribute(controls, "ALIGNMENT", "ACENTER");
    IupAppend(parent, groupBox);

    // set controls as attribute to toggle and enable toggle callback
    IupSetCallback(toggle, "ACTION", (Icallback)uiToggleControls);
    IupSetAttribute(toggle, CONTROLS_HANDLE, (char*)controls);
    IupSetAttribute(toggle, SYNCED_VALUE, (char*)module->enabledFlag);
    IupSetAttribute(controls, "ACTIVE", "NO"); // startup as inactive
    IupSetAttribute(controls, "NCGAP", "4"); // startup as inactive

    {
        char nameBuf[128];
        snprintf(nameBuf, sizeof(nameBuf), "toggle_%s", module->shortName);
        IupSetHandle(nameBuf, toggle);
        snprintf(nameBuf, sizeof(nameBuf), "controls_%s", module->shortName);
        IupSetHandle(nameBuf, controls);
    }
    
    // Set tooltip on toggle
    if (tooltip) {
        IupSetAttribute(toggle, "TIP", tooltip);
    }

    // set default icon
    IupSetAttribute(icon, "IMAGE", "none_icon");
    IupSetAttribute(icon, "PADDING", "4x");
    module->iconHandle = icon;
    
    // setup count label for statistics
    IupSetAttribute(countLabel, "SIZE", "40x");
    IupSetAttribute(countLabel, "TIP", "Packets affected by this module during current/last session. Resets when filtering starts.");
    module->countLabel = countLabel;

    // parameterize toggle
    if (parameterized) {
        setFromParameter(toggle, "VALUE", module->shortName);
    }
}
