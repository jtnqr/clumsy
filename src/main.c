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
#include "ui_callbacks.h"

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
Ihandle *addPresetButton, *delPresetButton;
// timer to update icons
Ihandle *stateIcon;
Ihandle *timer;
Ihandle *timeout = NULL;
Ihandle *durationTimer = NULL;


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
                addPresetButton = IupButton("Save", NULL),
                delPresetButton = IupButton("Delete", NULL),
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
    uiRefreshPresetsList();

    IupSetCallback(filterSelectList, "ACTION", (Icallback)uiListSelectCb);
    IupSetCallback(addPresetButton, "ACTION", (Icallback)uiSavePresetCb);
    IupSetAttribute(addPresetButton, "TIP", "Save current configuration as a preset");
    IupSetAttribute(addPresetButton, "PADDING", "8x");

    IupSetCallback(delPresetButton, "ACTION", (Icallback)uiDeletePresetCb);
    IupSetAttribute(delPresetButton, "TIP", "Delete the selected preset");
    IupSetAttribute(delPresetButton, "PADDING", "8x");

    // set g_applying_preset = TRUE before loading parameters so sync callbacks are not triggered to mark custom
    g_applying_preset = TRUE;

    // If state was loaded, restore the saved filter text
    if (stateLoaded) {
        const char *savedFilter = IupGetGlobal("filter");
        LOG("Restoring filter from state: %s", savedFilter ? savedFilter : "(null)");
        if (savedFilter && strlen(savedFilter) > 0) {
            IupStoreAttribute(filterText, "VALUE", savedFilter);
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

    // Now we are done programmatically setting up the UI from loaded state or defaults,
    // so we set g_applying_preset = FALSE.
    g_applying_preset = FALSE;

    // Determine what preset selection to show
    if (stateLoaded) {
        const char *savedPreset = IupGetGlobal("preset");
        LOG("Restoring preset from state: %s", savedPreset ? savedPreset : "(null)");
        int foundIdx = -1;
        if (savedPreset && strcmp(savedPreset, "<custom>") != 0) {
            for (ix = 0; ix < filtersSize; ++ix) {
                if (strcmp(filters[ix].name, savedPreset) == 0) {
                    foundIdx = ix;
                    break;
                }
            }
        }
        if (foundIdx != -1) {
            char valBuf[32];
            snprintf(valBuf, sizeof(valBuf), "%d", foundIdx + 1);
            IupSetAttribute(filterSelectList, "VALUE", valBuf);
        } else {
            char valBuf[32];
            snprintf(valBuf, sizeof(valBuf), "%d", filtersSize + 1);
            IupSetAttribute(filterSelectList, "VALUE", valBuf); // Select "<custom>"
        }
    } else {
        // Apply the first profile on startup if state was not loaded
        if (filtersSize > 0) {
            uiApplyProfile(&filters[0]);
            IupSetAttribute(filterSelectList, "VALUE", "1"); // Select first preset (index 1)
        } else {
            char valBuf[32];
            snprintf(valBuf, sizeof(valBuf), "%d", filtersSize + 1);
            IupSetAttribute(filterSelectList, "VALUE", valBuf); // Select "<custom>"
        }
    }

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
        snprintf(valueBuf, sizeof(valueBuf), "%s000", arg_value);  // convert from seconds to milliseconds

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



