#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <Windows.h>
#include "iup.h"
#include "common.h"

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

// global iup handlers
static Ihandle *dialog, *topFrame, *bottomFrame; 
static Ihandle *statusLabel;
static Ihandle *filterText, *filterButton;
static Ihandle *hotkeyLabel;
Ihandle *filterSelectList;
// timer to update icons
static Ihandle *stateIcon;
static Ihandle *timer;
static Ihandle *timeout = NULL;

// Hotkey configuration
#define HOTKEY_ID 1
#define DEFAULT_HOTKEY_MOD (MOD_CONTROL | MOD_SHIFT)
#define DEFAULT_HOTKEY_KEY 'C'
static UINT hotkeyModifiers = DEFAULT_HOTKEY_MOD;
static UINT hotkeyVirtualKey = DEFAULT_HOTKEY_KEY;
static HWND mainHwnd = NULL;
static BOOL hotkeyRegistered = FALSE;
static WNDPROC originalWndProc = NULL;

void showStatus(const char *line);
static int uiOnDialogShow(Ihandle *ih, int state);
static int uiStopCb(Ihandle *ih);
static int uiStartCb(Ihandle *ih);
static int uiTimerCb(Ihandle *ih);
static int uiTimeoutCb(Ihandle *ih);
static int uiListSelectCb(Ihandle *ih, char *text, int item, int state);
static int uiFilterTextCb(Ihandle *ih);
static void uiSetupModule(Module *module, Ihandle *parent);
static void toggleFiltering(void);
static void parseHotkeyConfig(const char* hotkeyStr);
static void formatHotkeyString(char* buf, size_t bufSize);

// serializing config files using a stupid custom format
#define CONFIG_FILE "config.txt"
#define STATE_FILE "state.txt"
#define CONFIG_MAX_RECORDS 64
#define CONFIG_BUF_SIZE 4096
typedef struct {
    char* filterName;
    char* filterValue;
} filterRecord;
UINT filtersSize;
filterRecord filters[CONFIG_MAX_RECORDS] = {0};
char configBuf[CONFIG_BUF_SIZE+2]; // add some padding to write \n
BOOL parameterized = 0; // parameterized flag, means reading args from command line
static BOOL stateLoaded = 0; // flag to indicate state was loaded (don't auto-start)
static char hotkeyConfigStr[64] = ""; // store hotkey config string

// State persistence
static void saveState(void);
static void loadState(void);

// Parse hotkey configuration string like "ctrl+shift+c" or "alt+f10"
static void parseHotkeyConfig(const char* hotkeyStr) {
    char buf[64];
    char *token, *saveptr;
    UINT mods = 0;
    UINT key = 0;
    
    if (!hotkeyStr || strlen(hotkeyStr) == 0) {
        LOG("No hotkey config, using default");
        return;
    }
    
    strncpy(buf, hotkeyStr, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    
    // Convert to lowercase for easier parsing
    for (char *p = buf; *p; ++p) *p = (char)tolower(*p);
    
    // Parse tokens separated by +
    token = strtok_s(buf, "+", &saveptr);
    while (token) {
        // Trim whitespace
        while (*token == ' ') token++;
        char *end = token + strlen(token) - 1;
        while (end > token && *end == ' ') *end-- = '\0';
        
        // Check for modifiers
        if (strcmp(token, "ctrl") == 0 || strcmp(token, "control") == 0) {
            mods |= MOD_CONTROL;
        } else if (strcmp(token, "alt") == 0) {
            mods |= MOD_ALT;
        } else if (strcmp(token, "shift") == 0) {
            mods |= MOD_SHIFT;
        } else if (strcmp(token, "win") == 0) {
            mods |= MOD_WIN;
        }
        // Check for function keys F1-F12
        else if (token[0] == 'f' && strlen(token) <= 3) {
            int fnum = atoi(token + 1);
            if (fnum >= 1 && fnum <= 12) {
                key = VK_F1 + (fnum - 1);
            }
        }
        // Check for single letter a-z
        else if (strlen(token) == 1 && token[0] >= 'a' && token[0] <= 'z') {
            key = 'A' + (token[0] - 'a'); // VK codes are uppercase
        }
        // Check for number 0-9
        else if (strlen(token) == 1 && token[0] >= '0' && token[0] <= '9') {
            key = '0' + (token[0] - '0');
        }
        
        token = strtok_s(NULL, "+", &saveptr);
    }
    
    // Only update if we got a valid key
    if (key != 0) {
        hotkeyModifiers = mods;
        hotkeyVirtualKey = key;
        LOG("Hotkey configured: mods=0x%x key=0x%x", mods, key);
    } else {
        LOG("Invalid hotkey config '%s', using default", hotkeyStr);
    }
}

// Format hotkey as human-readable string (e.g., "Ctrl+Shift+C")
static void formatHotkeyString(char* buf, size_t bufSize) {
    char keyName[32] = "";
    buf[0] = '\0';
    
    // Build modifier string
    if (hotkeyModifiers & MOD_CONTROL) {
        strcat(buf, "Ctrl+");
    }
    if (hotkeyModifiers & MOD_ALT) {
        strcat(buf, "Alt+");
    }
    if (hotkeyModifiers & MOD_SHIFT) {
        strcat(buf, "Shift+");
    }
    if (hotkeyModifiers & MOD_WIN) {
        strcat(buf, "Win+");
    }
    
    // Format key name
    if (hotkeyVirtualKey >= VK_F1 && hotkeyVirtualKey <= VK_F12) {
        sprintf(keyName, "F%d", hotkeyVirtualKey - VK_F1 + 1);
    } else if (hotkeyVirtualKey >= 'A' && hotkeyVirtualKey <= 'Z') {
        sprintf(keyName, "%c", (char)hotkeyVirtualKey);
    } else if (hotkeyVirtualKey >= '0' && hotkeyVirtualKey <= '9') {
        sprintf(keyName, "%c", (char)hotkeyVirtualKey);
    } else {
        sprintf(keyName, "0x%X", hotkeyVirtualKey);
    }
    
    strncat(buf, keyName, bufSize - strlen(buf) - 1);
}

// Subclassed window procedure to handle WM_HOTKEY messages
static LRESULT CALLBACK hotkeyWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_HOTKEY && wParam == HOTKEY_ID) {
        LOG("Hotkey pressed, toggling filtering");
        toggleFiltering();
        return 0;
    }
    // Call original window procedure for all other messages
    return CallWindowProc(originalWndProc, hWnd, msg, wParam, lParam);
}

// loading up filters and fill in
void loadConfig() {
    char path[MSG_BUFSIZE];
    char *p;
    FILE *f;
    GetModuleFileName(NULL, path, MSG_BUFSIZE);
    LOG("Executable path: %s", path);
    p = strrchr(path, '\\');
    if (p == NULL) p = strrchr(path, '/'); // holy shit
    strcpy(p+1, CONFIG_FILE);
    LOG("Config path: %s", path);
    f = fopen(path, "r");
    if (f) {
        size_t len;
        char *current, *last;
        len = fread(configBuf, sizeof(char), CONFIG_BUF_SIZE, f);
        if (len == CONFIG_BUF_SIZE) {
            LOG("Config file is larger than %d bytes, get truncated.", CONFIG_BUF_SIZE);
        }
        // always patch in a newline at the end to ease parsing
        configBuf[len] = '\n';
        configBuf[len+1] = '\0';

        // parse out the kv pairs. isn't quite safe
        filtersSize = 0;
        last = current = configBuf;
        do {
            // eat up empty lines
EAT_SPACE:  while (isspace(*current)) { ++current; }
            if (*current == '#') {
                current = strchr(current, '\n');
                if (!current) break;
                current = current + 1;
                goto EAT_SPACE;
            }

            // now we can start
            last = current;
            current = strchr(last, ':');
            if (!current) break;
            *current = '\0';
            filters[filtersSize].filterName = last;
            current += 1;
            while (isspace(*current)) { ++current; } // eat potential space after :
            last = current;
            current = strchr(last, '\n');
            if (!current) break;
            filters[filtersSize].filterValue = last;
            *current = '\0';
            if (*(current-1) == '\r') *(current-1) = 0;
            last = current = current + 1;
            
            // Check if this is the hotkey config (not a filter preset)
            if (strcmp(filters[filtersSize].filterName, "hotkey") == 0) {
                parseHotkeyConfig(filters[filtersSize].filterValue);
                // Don't increment filtersSize - this isn't a filter preset
            } else {
                ++filtersSize;
            }
        } while (last && last - configBuf < CONFIG_BUF_SIZE);
        LOG("Loaded %u records.", filtersSize);
    }

    if (!f || filtersSize == 0)
    {
        LOG("Failed to load from config. Fill in a simple one.");
        // config is missing or ill-formed. fill in some simple ones
        filters[filtersSize].filterName = "loopback packets";
        filters[filtersSize].filterValue = "outbound and ip.DstAddr >= 127.0.0.1 and ip.DstAddr <= 127.255.255.255";
        filtersSize = 1;
    }
}

// Get path to state file (in same directory as executable)
static void getStatePath(char* path, size_t pathSize) {
    char *p;
    GetModuleFileName(NULL, path, (DWORD)pathSize);
    p = strrchr(path, '\\');
    if (p == NULL) p = strrchr(path, '/');
    strcpy(p + 1, STATE_FILE);
}

// Save current state to state.txt
static void saveState(void) {
    char path[MSG_BUFSIZE];
    FILE *f;
    const char *filterValue;
    
    getStatePath(path, sizeof(path));
    f = fopen(path, "w");
    if (!f) {
        LOG("Failed to open state file for writing: %s", path);
        return;
    }
    
    LOG("Saving state to: %s", path);
    fprintf(f, "# clumsy last state - auto-generated\n");
    
    // Save filter text
    filterValue = IupGetAttribute(filterText, "VALUE");
    if (filterValue && strlen(filterValue) > 0) {
        fprintf(f, "filter: %s\n", filterValue);
    }
    
    // Save module states
    // We need to get values from the internal volatile variables
    // since the UI might not have the synced values accessible easily
    {
        UINT ix;
        for (ix = 0; ix < MODULE_CNT; ++ix) {
            Module *module = modules[ix];
            short enabled = *(module->enabledFlag);
            fprintf(f, "%s: %s\n", module->shortName, enabled ? "on" : "off");
        }
    }
    
    // Save detailed module settings using naming convention module-setting
    // These match the command-line parameter names used in setFromParameter
    
    // Lag module: lag-inbound, lag-outbound, lag-time
    fprintf(f, "lag-inbound: %s\n", IupGetGlobal("lag-inbound") ? IupGetGlobal("lag-inbound") : "on");
    fprintf(f, "lag-outbound: %s\n", IupGetGlobal("lag-outbound") ? IupGetGlobal("lag-outbound") : "on");
    fprintf(f, "lag-time: %s\n", IupGetGlobal("lag-time") ? IupGetGlobal("lag-time") : "50");
    
    // Drop module: drop-inbound, drop-outbound, drop-chance
    fprintf(f, "drop-inbound: %s\n", IupGetGlobal("drop-inbound") ? IupGetGlobal("drop-inbound") : "on");
    fprintf(f, "drop-outbound: %s\n", IupGetGlobal("drop-outbound") ? IupGetGlobal("drop-outbound") : "on");
    fprintf(f, "drop-chance: %s\n", IupGetGlobal("drop-chance") ? IupGetGlobal("drop-chance") : "10.0");
    
    // Throttle module
    fprintf(f, "throttle-inbound: %s\n", IupGetGlobal("throttle-inbound") ? IupGetGlobal("throttle-inbound") : "on");
    fprintf(f, "throttle-outbound: %s\n", IupGetGlobal("throttle-outbound") ? IupGetGlobal("throttle-outbound") : "on");
    fprintf(f, "throttle-chance: %s\n", IupGetGlobal("throttle-chance") ? IupGetGlobal("throttle-chance") : "10.0");
    fprintf(f, "throttle-frame: %s\n", IupGetGlobal("throttle-frame") ? IupGetGlobal("throttle-frame") : "30");
    
    // OOD module
    fprintf(f, "ood-inbound: %s\n", IupGetGlobal("ood-inbound") ? IupGetGlobal("ood-inbound") : "on");
    fprintf(f, "ood-outbound: %s\n", IupGetGlobal("ood-outbound") ? IupGetGlobal("ood-outbound") : "on");
    fprintf(f, "ood-chance: %s\n", IupGetGlobal("ood-chance") ? IupGetGlobal("ood-chance") : "10.0");
    
    // Duplicate module
    fprintf(f, "duplicate-inbound: %s\n", IupGetGlobal("duplicate-inbound") ? IupGetGlobal("duplicate-inbound") : "on");
    fprintf(f, "duplicate-outbound: %s\n", IupGetGlobal("duplicate-outbound") ? IupGetGlobal("duplicate-outbound") : "on");
    fprintf(f, "duplicate-chance: %s\n", IupGetGlobal("duplicate-chance") ? IupGetGlobal("duplicate-chance") : "10.0");
    fprintf(f, "duplicate-count: %s\n", IupGetGlobal("duplicate-count") ? IupGetGlobal("duplicate-count") : "2");
    
    // Tamper module
    fprintf(f, "tamper-inbound: %s\n", IupGetGlobal("tamper-inbound") ? IupGetGlobal("tamper-inbound") : "on");
    fprintf(f, "tamper-outbound: %s\n", IupGetGlobal("tamper-outbound") ? IupGetGlobal("tamper-outbound") : "on");
    fprintf(f, "tamper-chance: %s\n", IupGetGlobal("tamper-chance") ? IupGetGlobal("tamper-chance") : "10.0");
    fprintf(f, "tamper-checksum: %s\n", IupGetGlobal("tamper-checksum") ? IupGetGlobal("tamper-checksum") : "on");
    
    // Reset module
    fprintf(f, "reset-inbound: %s\n", IupGetGlobal("reset-inbound") ? IupGetGlobal("reset-inbound") : "on");
    fprintf(f, "reset-outbound: %s\n", IupGetGlobal("reset-outbound") ? IupGetGlobal("reset-outbound") : "on");
    fprintf(f, "reset-chance: %s\n", IupGetGlobal("reset-chance") ? IupGetGlobal("reset-chance") : "0");
    
    // Bandwidth module
    fprintf(f, "bandwidth-inbound: %s\n", IupGetGlobal("bandwidth-inbound") ? IupGetGlobal("bandwidth-inbound") : "on");
    fprintf(f, "bandwidth-outbound: %s\n", IupGetGlobal("bandwidth-outbound") ? IupGetGlobal("bandwidth-outbound") : "on");
    fprintf(f, "bandwidth-bandwidth: %s\n", IupGetGlobal("bandwidth-bandwidth") ? IupGetGlobal("bandwidth-bandwidth") : "10");
    
    fclose(f);
    LOG("State saved successfully");
}

// State buffer for loading (separate from config buffer)
static char stateBuf[CONFIG_BUF_SIZE + 2];

// Load state from state.txt (sets IupGlobal values that get applied during UI setup)
static void loadState(void) {
    char path[MSG_BUFSIZE];
    FILE *f;
    
    getStatePath(path, sizeof(path));
    f = fopen(path, "r");
    if (!f) {
        LOG("No state file found: %s", path);
        return;
    }
    
    LOG("Loading state from: %s", path);
    
    {
        size_t len;
        char *current, *last;
        len = fread(stateBuf, sizeof(char), CONFIG_BUF_SIZE, f);
        stateBuf[len] = '\n';
        stateBuf[len + 1] = '\0';
        fclose(f);
        
        // Parse key: value pairs
        last = current = stateBuf;
        do {
            char *key, *value;
            
            // Skip whitespace and comments
            while (isspace(*current)) { ++current; }
            if (*current == '#') {
                current = strchr(current, '\n');
                if (!current) break;
                current++;
                continue;
            }
            if (*current == '\0') break;
            
            // Parse key
            key = current;
            current = strchr(current, ':');
            if (!current) break;
            *current = '\0';
            current++;
            
            // Skip space after :
            while (*current == ' ') current++;
            
            // Parse value
            value = current;
            current = strchr(current, '\n');
            if (current) {
                *current = '\0';
                if (*(current - 1) == '\r') *(current - 1) = '\0';
                current++;
            }
            
            // Store in IupGlobal for setFromParameter to use
            LOG("State: %s = %s", key, value);
            IupStoreGlobal(key, value);
            
            last = current;
        } while (current && current - stateBuf < CONFIG_BUF_SIZE);
    }
    
    LOG("State loaded");
}

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

    topFrame = IupFrame(
        topVbox = IupVbox(
            filterText = IupText(NULL),
            controlHbox = IupHbox(
                stateIcon = IupLabel(NULL),
                filterButton = IupButton("Start", NULL),
                hotkeyLabel = IupLabel(""),
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
        IupStoreAttribute(filterSelectList, ixBuf, filters[ix].filterName);
    }
    IupSetAttribute(filterSelectList, "VALUE", "1");
    IupSetCallback(filterSelectList, "ACTION", (Icallback)uiListSelectCb);
    // set filter text value since the callback won't take effect before main loop starts
    IupSetAttribute(filterText, "VALUE", filters[0].filterValue);
    
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

    // dialog
    dialog = IupDialog(
        dialogVBox = IupVbox(
            topFrame,
            bottomFrame,
            statusLabel,
            NULL
        )
    );

    IupSetAttribute(dialog, "TITLE", "clumsy " CLUMSY_VERSION);
    IupSetAttribute(dialog, "SIZE", "540x"); // add padding manually to width (extra space for hotkey label)
    IupSetAttribute(dialog, "RESIZE", "NO");
    IupSetCallback(dialog, "SHOW_CB", (Icallback)uiOnDialogShow);


    // global layout settings to affect childrens
    IupSetAttribute(dialogVBox, "ALIGNMENT", "ACENTER");
    IupSetAttribute(dialogVBox, "NCMARGIN", "4x4");
    IupSetAttribute(dialogVBox, "NCGAP", "4x2");

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
    UNREFERENCED_PARAMETER(ih);
    if (divertStart(IupGetAttribute(filterText, "VALUE"), buf) == 0) {
        showStatus(buf);
        return IUP_DEFAULT;
    }

    // successfully started
    showStatus("Started filtering. Enable functionalities to take effect.");
    IupSetAttribute(dialog, "TITLE", "clumsy " CLUMSY_VERSION " (running)");
    IupSetAttribute(filterText, "ACTIVE", "NO");
    IupSetAttribute(filterButton, "TITLE", "Stop");
    IupSetCallback(filterButton, "ACTION", uiStopCb);
    IupSetAttribute(timer, "RUN", "YES");

    return IUP_DEFAULT;
}

static int uiStopCb(Ihandle *ih) {
    int ix;
    UNREFERENCED_PARAMETER(ih);
    
    // try stopping
    IupSetAttribute(filterButton, "ACTIVE", "NO");
    IupFlush(); // flush to show disabled state
    divertStop();

    IupSetAttribute(dialog, "TITLE", "clumsy " CLUMSY_VERSION);
    IupSetAttribute(filterText, "ACTIVE", "YES");
    IupSetAttribute(filterButton, "TITLE", "Start");
    IupSetAttribute(filterButton, "ACTIVE", "YES");
    IupSetCallback(filterButton, "ACTION", uiStartCb);

    // stop timer and clean up icons
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
static void toggleFiltering(void) {
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

static int uiListSelectCb(Ihandle *ih, char *text, int item, int state) {
    UNREFERENCED_PARAMETER(text);
    UNREFERENCED_PARAMETER(ih);
    if (state == 1) {
        IupSetAttribute(filterText, "VALUE", filters[item-1].filterValue);
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
    Ihandle *groupBox, *toggle, *controls, *icon;
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
    
    // Set tooltip on toggle
    if (tooltip) {
        IupSetAttribute(toggle, "TIP", tooltip);
    }

    // set default icon
    IupSetAttribute(icon, "IMAGE", "none_icon");
    IupSetAttribute(icon, "PADDING", "4x");
    module->iconHandle = icon;

    // parameterize toggle
    if (parameterized) {
        setFromParameter(toggle, "VALUE", module->shortName);
    }
}

int main(int argc, char* argv[]) {
    LOG("Is Run As Admin: %d", IsRunAsAdmin());
    LOG("Is Elevated: %d", IsElevated());
    init(argc, argv);
    startup();
    cleanup();
    return 0;
}
