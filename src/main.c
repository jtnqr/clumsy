#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include "iup.h"
#include "common.h"
#include "process_filter.h"
#include "ui_components.h"
#include "config_manager.h"
#include "hotkey_manager.h"

#define noticeLabel statusLabel

// ! the order decides which module get processed first
Module* modules[MODULE_CNT] = {
    &lagModule,
    &dropModule,
    &throttleModule,
    &dupModule,
    &oodModule,
    &tamperModule,
    &resetModule,
	&bandwidthModule,
};

volatile short sendState = SEND_STATUS_NONE;
char g_manual_filter[4096] = {0};
BOOL g_process_filter_enabled = FALSE;

// global iup handlers
Ihandle *dialog, *topFrame, *bottomFrame; 
Ihandle *statusLabel;
Ihandle *filterText, *filterButton;
Ihandle *hotkeyLabel;
Ihandle *filterSelectList;
// timer to update icons
Ihandle *stateIcon;
Ihandle *timer;
Ihandle *timeout = NULL;
Ihandle *durationTimer = NULL;
void showStatus(const char *line);
static int uiOnDialogShow(Ihandle *ih, int state);
static int uiStopCb(Ihandle *ih);
static int uiStartCb(Ihandle *ih);
static int uiTimerCb(Ihandle *ih);
static int uiTimeoutCb(Ihandle *ih);
static int uiDurationTimerCb(Ihandle *ih);
static int uiListSelectCb(Ihandle *ih, char *text, int item, int state);
static int uiFilterTextCb(Ihandle *ih);
static void uiSetupModule(Module *module, Ihandle *parent);
void toggleFiltering(void);

static void uiApplyProfile(ProfileRecord *p);

// Parse hotkey configuration string like "ctrl+shift+c" or "alt+f10"




void init(int argc, char* argv[]) {
    UINT ix;
    Ihandle *topVbox, *bottomVbox, *dialogVBox, *controlHbox;
    Ihandle *noneIcon, *doingIcon, *errorIcon;
    char* arg_value = NULL;

    // fill in config
    loadConfig();

    // iup inits
    IupOpen(&argc, &argv);

    // this is so easy to get wrong so it's pretty worth noting in the program
    statusLabel = IupLabel("NOTICE: When capturing localhost (loopback) packets, you CAN'T include inbound criteria.\n"
        "Filters like 'udp' need to be 'udp and outbound' to work. See readme for more info.");
    IupSetAttribute(statusLabel, "EXPAND", "HORIZONTAL");
    IupSetAttribute(statusLabel, "PADDING", "8x8");
    IupSetAttribute(statusLabel, "FOREGROUND", "140 140 140");
    IupSetAttribute(noticeLabel, "WORDWRAP", "YES");

    topFrame = IupFrame(
        topVbox = IupVbox(
            filterText = IupText(NULL),
            controlHbox = IupHbox(
                stateIcon = IupLabel(NULL),
                filterButton = IupButton("Start", NULL),
                hotkeyLabel = IupLabel(""),
                IupFill(),
                IupLabel("Presets:  "),
                filterSelectList = IupList(NULL),
                NULL
            ),
            NULL
        )
    );

    // parse arguments and set globals *before* setting up UI.
    // arguments can be read and set after callbacks are setup
    // FIXME as Release is built as WindowedApp, stdout/stderr won't show
    LOG("argc: %d", argc);
    if (argc > 1) {
        if (!parseArgs(argc, argv)) {
            fprintf(stderr, "invalid argument count. ensure you're using options as \"--drop on\"");
            exit(-1); // fail fast.
        }
        parameterized = 1;
    } else {
        // No command-line args, try to load last saved state
        loadState();
        // Check if filter was loaded from state
        if (IupGetGlobal("filter") != NULL) {
            parameterized = 1;  // Enable parameter loading for modules
            stateLoaded = 1;    // Mark as state-loaded (don't auto-start)
            LOG("State loaded, enabling parameterized mode (no auto-start)");
        }
    }

    IupSetAttribute(topFrame, "TITLE", "Filtering");
    IupSetAttribute(topFrame, "EXPAND", "HORIZONTAL");
    IupSetAttribute(filterText, "EXPAND", "HORIZONTAL");
    IupSetCallback(filterText, "VALUECHANGED_CB", (Icallback)uiFilterTextCb);
    IupSetAttribute(filterText, "TIP", 
        "WinDivert filter expression\n"
        "\n"
        "Direction: inbound, outbound\n"
        "Protocol: tcp, udp, icmp\n"
        "IP: ip.SrcAddr, ip.DstAddr\n"
        "Ports: tcp.SrcPort, tcp.DstPort, udp.SrcPort, udp.DstPort\n"
        "Operators: ==, !=, <, >, <=, >=, and, or, not\n"
        "\n"
        "Examples:\n"
        "  outbound and tcp.DstPort == 80\n"
        "  udp and ip.DstAddr == 192.168.1.1\n"
        "  tcp.SrcPort == 443 or tcp.DstPort == 443\n"
        "\n"
        "Note: For localhost packets, use 'outbound' only");
    IupSetAttribute(filterButton, "PADDING", "8x");
    IupSetCallback(filterButton, "ACTION", uiStartCb);
    IupSetAttribute(filterButton, "TIP", "Start/Stop packet filtering");
    IupSetAttribute(topVbox, "NCMARGIN", "4x4");
    IupSetAttribute(topVbox, "NCGAP", "4x2");
    IupSetAttribute(controlHbox, "ALIGNMENT", "ACENTER");

    // setup state icon
    IupSetAttribute(stateIcon, "IMAGE", "none_icon");
    IupSetAttribute(stateIcon, "PADDING", "4x");
    
    // setup hotkey hint label (expands to fill space, text set when hotkey is registered)
    IupSetAttribute(hotkeyLabel, "EXPAND", "HORIZONTAL");
    IupSetAttribute(hotkeyLabel, "PADDING", "8x");

    // fill in options and setup callback
    IupSetAttribute(filterSelectList, "VISIBLECOLUMNS", "24");
    IupSetAttribute(filterSelectList, "DROPDOWN", "YES");
    for (ix = 0; ix < filtersSize; ++ix) {
        char ixBuf[4];
        sprintf(ixBuf, "%d", ix+1); // ! staring from 1, following lua indexing
        IupStoreAttribute(filterSelectList, ixBuf, filters[ix].name);
    }
    IupSetAttribute(filterSelectList, "VALUE", "1");
    IupSetCallback(filterSelectList, "ACTION", (Icallback)uiListSelectCb);

    // Apply the first profile on startup if state was not loaded
    if (filtersSize > 0 && !stateLoaded) {
        uiApplyProfile(&filters[0]);
    }
    
    // If state was loaded, restore the saved filter text and deselect preset
    if (stateLoaded) {
        const char *savedFilter = IupGetGlobal("filter");
        LOG("Restoring filter from state: %s", savedFilter ? savedFilter : "(null)");
        if (savedFilter && strlen(savedFilter) > 0) {
            IupStoreAttribute(filterText, "VALUE", savedFilter);
            IupSetAttribute(filterSelectList, "VALUE", "0");  // Deselect preset
        }
    }

    // functionalities frame 
    bottomFrame = IupFrame(
        bottomVbox = IupVbox(
            NULL
        )
    );
    IupSetAttribute(bottomFrame, "TITLE", "Functions");
    IupSetAttribute(bottomVbox, "NCMARGIN", "4x4");
    IupSetAttribute(bottomVbox, "NCGAP", "4x2");

    // create icons
    noneIcon = IupImage(8, 8, icon8x8);
    doingIcon = IupImage(8, 8, icon8x8);
    errorIcon = IupImage(8, 8, icon8x8);
    IupSetAttribute(noneIcon, "0", "BGCOLOR");
    IupSetAttribute(noneIcon, "1", "224 224 224");
    IupSetAttribute(doingIcon, "0", "BGCOLOR");
    IupSetAttribute(doingIcon, "1", "109 170 44");
    IupSetAttribute(errorIcon, "0", "BGCOLOR");
    IupSetAttribute(errorIcon, "1", "208 70 72");
    IupSetHandle("none_icon", noneIcon);
    IupSetHandle("doing_icon", doingIcon);
    IupSetHandle("error_icon", errorIcon);

    // setup module uis
    for (ix = 0; ix < MODULE_CNT; ++ix) {
        uiSetupModule(*(modules+ix), bottomVbox);
    }

    // Process filter frame setup using the ui_components abstraction module
    Ihandle *processFilterContainer = uiCreateProcessFilterFrame();

    // Wrap the statusLabel inside an explicit vertical container
    Ihandle *footerBox = IupVbox(statusLabel, NULL);
    IupSetAttribute(footerBox, "MARGIN", "0x4");
    IupSetAttribute(footerBox, "EXPAND", "HORIZONTAL");

    // dialog
    dialog = IupDialog(
        dialogVBox = IupVbox(
            topFrame,
            processFilterContainer,
            bottomFrame,
            footerBox,
            NULL
        )
    );

    IupSetAttribute(dialog, "TITLE", "clumsy " CLUMSY_VERSION);
    IupSetAttribute(dialog, "SIZE", "540x"); // add padding manually to width (extra space for hotkey label)
    IupSetAttribute(dialog, "RESIZE", "YES");
    IupSetCallback(dialog, "SHOW_CB", (Icallback)uiOnDialogShow);

    // global layout settings to affect childrens
    IupSetAttribute(dialogVBox, "ALIGNMENT", "ACENTER");
    IupSetAttribute(dialogVBox, "EXPAND", "HORIZONTAL");
    IupSetAttribute(dialogVBox, "MARGIN", "4x4");
    IupSetAttribute(dialogVBox, "GAP", "4");

    IupMap(dialog);
    IupRefresh(dialog);
    IupSetAttribute(dialog, "MINSIZE", IupGetAttribute(dialog, "RASTERSIZE"));

    // setup timer
    timer = IupTimer();
    IupSetAttribute(timer, "TIME", STR(ICON_UPDATE_MS));
    IupSetCallback(timer, "ACTION_CB", uiTimerCb);

    // setup timeout of program
    arg_value = IupGetGlobal("timeout");
    if(arg_value != NULL)
    {
        char valueBuf[16];
        sprintf(valueBuf, "%s000", arg_value);  // convert from seconds to milliseconds

        timeout = IupTimer();
        IupStoreAttribute(timeout, "TIME", valueBuf);
        IupSetCallback(timeout, "ACTION_CB", uiTimeoutCb);
        IupSetAttribute(timeout, "RUN", "YES");
    }

    // Initialize the process filter module
    processFilterInit();

    // Initialize duration timer
    durationTimer = IupTimer();
    IupSetCallback(durationTimer, "ACTION_CB", uiDurationTimerCb);
}

void startup() {
    // initialize seed
    srand((unsigned int)time(NULL));

    // kickoff event loops
    IupShowXY(dialog, IUP_CENTER, IUP_CENTER);
    IupMainLoop();
    // ! main loop won't return until program exit
}

void cleanup() {
    // Save current state before exiting
    saveState();
    
    // Unregister hotkey and restore window procedure
    if (hotkeyRegistered && mainHwnd) {
        // Restore original window procedure first
        if (originalWndProc) {
            SetWindowLongPtr(mainHwnd, GWLP_WNDPROC, (LONG_PTR)originalWndProc);
            originalWndProc = NULL;
        }
        UnregisterHotKey(mainHwnd, HOTKEY_ID);
        hotkeyRegistered = FALSE;
        LOG("Hotkey unregistered");
    }

    // Clean up the process filter module
    processFilterCleanup();

    IupDestroy(timer);
    if (timeout) {
        IupDestroy(timeout);
    }

    IupClose();
    endTimePeriod(); // try close if not closing
}

// ui logics
void showStatus(const char *line) {
    IupStoreAttribute(statusLabel, "TITLE", line); 
}

// in fact only 32bit binary would run on 64 bit os
// if this happens pop out message box and exit
static BOOL check32RunningOn64(HWND hWnd) {
    BOOL is64ret;
    // consider IsWow64Process return value
    if (IsWow64Process(GetCurrentProcess(), &is64ret) && is64ret) {
        MessageBox(hWnd, (LPCSTR)"You're running 32bit clumsy on 64bit Windows, which wouldn't work. Please use the 64bit clumsy version.",
            (LPCSTR)"Aborting", MB_OK);
        return TRUE;
    }
    return FALSE;
}

static BOOL checkIsRunning() {
    //It will be closed and destroyed when programm terminates (according to MSDN).
    HANDLE hStartEvent = CreateEventW(NULL, FALSE, FALSE, L"Global\\CLUMSY_IS_RUNNING_EVENT_NAME");

    if (hStartEvent == NULL)
        return TRUE;

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hStartEvent);
        hStartEvent = NULL;
        return TRUE;
    }

    return FALSE;
}


static int uiOnDialogShow(Ihandle *ih, int state) {
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
            sprintf(tipStr, "Start/Stop packet filtering (Hotkey: %s)", hotkeyStr);
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

static int uiStartCb(Ihandle *ih) {
    char buf[MSG_BUFSIZE];
    int ix;
    const char *filterExpr;
    UNREFERENCED_PARAMETER(ih);

    // 1. Duration input sanitization validation gate
    int ms = 0;
    if (uiIsDurationEnabled()) {
        Ihandle *dur_text = IupGetHandle("process_filter_duration_text");
        const char *rawDur = dur_text ? IupGetAttribute(dur_text, "VALUE") : NULL;
        
        BOOL valid = TRUE;
        if (!rawDur || *rawDur == '\0') {
            valid = FALSE;
        } else {
            const char *ptr = rawDur;
            while (*ptr) {
                if (*ptr < '0' || *ptr > '9') {
                    valid = FALSE;
                    break;
                }
                ptr++;
            }
        }
        
        if (valid) {
            ms = atoi(rawDur);
            if (ms <= 0) {
                valid = FALSE;
            }
        }
        
        if (!valid) {
            ms = 1000;
            if (dur_text) {
                IupSetAttribute(dur_text, "VALUE", "1000");
            }
            LOG("Duration input is invalid or 0. Coerced to 1000ms fallback.");
        }
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

static int uiStopCb(Ihandle *ih) {
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

static int uiTimerCb(Ihandle *ih) {
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
                sprintf(countBuf, "%ld", count);
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

static int uiTimeoutCb(Ihandle *ih) {
    UNREFERENCED_PARAMETER(ih);
    return IUP_CLOSE;
 }

static int uiDurationTimerCb(Ihandle *ih) {
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

static void uiApplyProfile(ProfileRecord *p) {
    // 1. Set filter text
    IupSetAttribute(filterText, "VALUE", p->filter);

    // 2. Set process filter settings
    Ihandle *proc_text = IupGetHandle("process_filter_text");
    Ihandle *proc_toggle = IupGetHandle("process_filter_toggle");
    Ihandle *dur_text = IupGetHandle("process_filter_duration_text");
    Ihandle *dur_toggle = IupGetHandle("process_filter_duration_toggle");

    if (proc_text) IupSetAttribute(proc_text, "VALUE", p->procFilterTarget);
    if (proc_toggle) {
        IupSetAttribute(proc_toggle, "VALUE", p->procFilterEnabled ? "ON" : "OFF");
    }
    if (dur_text) {
        char durBuf[32];
        snprintf(durBuf, sizeof(durBuf), "%d", p->durationValueMs);
        IupSetAttribute(dur_text, "VALUE", durBuf);
    }
    if (dur_toggle) {
        IupSetAttribute(dur_toggle, "VALUE", p->durationEnabled ? "ON" : "OFF");
    }

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

static int uiListSelectCb(Ihandle *ih, char *text, int item, int state) {
    UNREFERENCED_PARAMETER(text);
    UNREFERENCED_PARAMETER(ih);
    if (state == 1) {
        uiApplyProfile(&filters[item-1]);
    }
    return IUP_DEFAULT;
}

static int uiFilterTextCb(Ihandle *ih)  {
    UNREFERENCED_PARAMETER(ih);
    // unselect list
    IupSetAttribute(filterSelectList, "VALUE", "0");
    return IUP_DEFAULT;
}

static void uiSetupModule(Module *module, Ihandle *parent) {
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

#ifndef _MSC_VER
#undef __argc
#undef __argv
int __argc = 0;
char **__argv = NULL;
#ifdef _WIN64
int *__imp___argc = &__argc;
char ***__imp___argv = &__argv;
#else
int *_imp____argc = &__argc;
char ***_imp____argv = &__argv;
#endif

#ifdef _WIN64
__declspec(dllexport) int _setjmp(void *env, void *ctx) {
    return __builtin_setjmp(env);
}
#endif
#endif





int main(int argc, char* argv[]) {
#ifndef _MSC_VER
    __argc = argc;
    __argv = argv;
#endif
    LOG("Is Run As Admin: %d", IsRunAsAdmin());
    LOG("Is Elevated: %d", IsElevated());
    init(argc, argv);
    startup();
    cleanup();
    return 0;
}



