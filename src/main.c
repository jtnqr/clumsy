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
static Ihandle *durationTimer = NULL;

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
static int uiDurationTimerCb(Ihandle *ih);
static int uiListSelectCb(Ihandle *ih, char *text, int item, int state);
static int uiFilterTextCb(Ihandle *ih);
static void uiSetupModule(Module *module, Ihandle *parent);
static void toggleFiltering(void);
static void parseHotkeyConfig(const char* hotkeyStr);
static void formatHotkeyString(char* buf, size_t bufSize);

// serializing config files using a stupid custom format
#define CONFIG_FILE "config.yaml"
#define STATE_FILE "state.txt"
#define CONFIG_MAX_RECORDS 64
#define CONFIG_BUF_SIZE 16384

typedef struct {
    char name[128];
    char filter[1024];
    
    BOOL procFilterEnabled;
    char procFilterTarget[128];
    BOOL durationEnabled;
    int durationValueMs;
    
    struct {
        BOOL enabled;
        BOOL inbound;
        BOOL outbound;
        char time[64];
    } lag;
    
    struct {
        BOOL enabled;
        BOOL inbound;
        BOOL outbound;
        char chance[64];
    } drop;
    
    struct {
        BOOL enabled;
        BOOL inbound;
        BOOL outbound;
        char chance[64];
        char frame[64];
        BOOL drop;
    } throttle;
    
    struct {
        BOOL enabled;
        BOOL inbound;
        BOOL outbound;
        char chance[64];
        char count[64];
    } duplicate;
    
    struct {
        BOOL enabled;
        BOOL inbound;
        BOOL outbound;
        char chance[64];
    } ood;
    
    struct {
        BOOL enabled;
        BOOL inbound;
        BOOL outbound;
        char chance[64];
        BOOL checksum;
    } tamper;
    
    struct {
        BOOL enabled;
        BOOL inbound;
        BOOL outbound;
        char chance[64];
    } reset;
    
    struct {
        BOOL enabled;
        BOOL inbound;
        BOOL outbound;
        char limit[64];
    } bandwidth;
} ProfileRecord;

UINT filtersSize;
ProfileRecord filters[CONFIG_MAX_RECORDS] = {0};
static void uiApplyProfile(ProfileRecord *p);
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
    size_t pos = 0;
    buf[0] = '\0';
    
    // Build modifier string
    if (hotkeyModifiers & MOD_CONTROL) {
        pos += snprintf(buf + pos, bufSize - pos, "Ctrl+");
    }
    if (hotkeyModifiers & MOD_ALT) {
        pos += snprintf(buf + pos, bufSize - pos, "Alt+");
    }
    if (hotkeyModifiers & MOD_SHIFT) {
        pos += snprintf(buf + pos, bufSize - pos, "Shift+");
    }
    if (hotkeyModifiers & MOD_WIN) {
        pos += snprintf(buf + pos, bufSize - pos, "Win+");
    }
    
    // Format key name
    if (hotkeyVirtualKey >= VK_F1 && hotkeyVirtualKey <= VK_F12) {
        snprintf(keyName, sizeof(keyName), "F%d", hotkeyVirtualKey - VK_F1 + 1);
    } else if (hotkeyVirtualKey >= 'A' && hotkeyVirtualKey <= 'Z') {
        snprintf(keyName, sizeof(keyName), "%c", (char)hotkeyVirtualKey);
    } else if (hotkeyVirtualKey >= '0' && hotkeyVirtualKey <= '9') {
        snprintf(keyName, sizeof(keyName), "%c", (char)hotkeyVirtualKey);
    } else {
        snprintf(keyName, sizeof(keyName), "0x%X", hotkeyVirtualKey);
    }
    
    snprintf(buf + pos, bufSize - pos, "%s", keyName);
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
    if (p == NULL || (size_t)(p - path + 1 + strlen(CONFIG_FILE)) >= MSG_BUFSIZE) {
        LOG("Path too long for config file");
        return;
    }
    snprintf(p + 1, MSG_BUFSIZE - (p - path + 1), "%s", CONFIG_FILE);
    LOG("Config path: %s", path);
    f = fopen(path, "r");
    filtersSize = 0;
    if (f) {
        size_t len;
        len = fread(configBuf, sizeof(char), CONFIG_BUF_SIZE, f);
        if (len == CONFIG_BUF_SIZE) {
            LOG("Config file is larger than %d bytes, get truncated.", CONFIG_BUF_SIZE);
        }
        // always patch in a newline at the end to ease parsing
        configBuf[len] = '\n';
        configBuf[len+1] = '\0';
        fclose(f);

        // parse out YAML structure
        char *line = configBuf;
        
        // Stack to track current section hierarchy
        struct {
            int indent;
            char name[64];
        } stack[16];
        int stack_depth = 0;

        while (line && *line != '\0') {
            char *next_line = strchr(line, '\n');
            if (next_line) {
                *next_line = '\0';
                next_line++;
            }
            
            // Trim trailing carriage return
            size_t line_len = strlen(line);
            if (line_len > 0 && line[line_len - 1] == '\r') {
                line[line_len - 1] = '\0';
            }

            // 1. Calculate indentation (leading spaces)
            int indent = 0;
            char *p_line = line;
            while (*p_line == ' ' || *p_line == '\t') {
                if (*p_line == '\t') indent += 4;
                else indent++;
                p_line++;
            }

            // Skip empty or comment lines
            if (*p_line == '\0' || *p_line == '#') {
                line = next_line;
                continue;
            }

            // 2. Handle list item prefix "- " if present
            BOOL is_list_item = FALSE;
            if (p_line[0] == '-' && p_line[1] == ' ') {
                is_list_item = TRUE;
                p_line += 2;
                while (*p_line == ' ') {
                    p_line++;
                }
            }

            // 3. Split into key and value
            char *colon = strchr(p_line, ':');
            if (!colon) {
                line = next_line;
                continue;
            }

            *colon = '\0';
            char *key = p_line;
            char *value = colon + 1;

            // Trim spaces from key
            while (*key == ' ') key++;
            char *key_end = key + strlen(key) - 1;
            while (key_end > key && *key_end == ' ') {
                *key_end = '\0';
                key_end--;
            }

            // Trim spaces from value
            while (*value == ' ') value++;
            char *value_end = value + strlen(value) - 1;
            while (value_end > value && *value_end == ' ') {
                *value_end = '\0';
                value_end--;
            }

            // Remove surrounding quotes from value
            if (strlen(value) >= 2 && ((value[0] == '"' && value[value_end - value] == '"') || (value[0] == '\'' && value[value_end - value] == '\''))) {
                value[value_end - value] = '\0';
                value++;
            }

            // 4. Update stack based on current indent level
            while (stack_depth > 0 && stack[stack_depth - 1].indent >= indent) {
                stack_depth--;
            }

            // 5. If this is a section header (value is empty), push to stack
            if (*value == '\0') {
                if (stack_depth < 16) {
                    stack[stack_depth].indent = indent;
                    strncpy(stack[stack_depth].name, key, 63);
                    stack[stack_depth].name[63] = '\0';
                    stack_depth++;
                }
                line = next_line;
                continue;
            }

            // 6. Process key-value pair!
            if (is_list_item && strcmp(key, "name") == 0) {
                if (filtersSize < CONFIG_MAX_RECORDS) {
                    ProfileRecord *p = &filters[filtersSize];
                    memset(p, 0, sizeof(ProfileRecord));
                    
                    // Initialize with baseline defaults
                    p->procFilterEnabled = FALSE;
                    p->procFilterTarget[0] = '\0';
                    p->durationEnabled = FALSE;
                    p->durationValueMs = 10;
                    
                    p->lag.enabled = FALSE;
                    p->lag.inbound = TRUE;
                    p->lag.outbound = TRUE;
                    strcpy(p->lag.time, "50");
                    
                    p->drop.enabled = FALSE;
                    p->drop.inbound = TRUE;
                    p->drop.outbound = TRUE;
                    strcpy(p->drop.chance, "10.0");
                    
                    p->throttle.enabled = FALSE;
                    p->throttle.inbound = TRUE;
                    p->throttle.outbound = TRUE;
                    strcpy(p->throttle.chance, "10.0");
                    strcpy(p->throttle.frame, "30");
                    p->throttle.drop = FALSE;
                    
                    p->duplicate.enabled = FALSE;
                    p->duplicate.inbound = TRUE;
                    p->duplicate.outbound = TRUE;
                    strcpy(p->duplicate.chance, "10.0");
                    strcpy(p->duplicate.count, "2");
                    
                    p->ood.enabled = FALSE;
                    p->ood.inbound = TRUE;
                    p->ood.outbound = TRUE;
                    strcpy(p->ood.chance, "10.0");
                    
                    p->tamper.enabled = FALSE;
                    p->tamper.inbound = TRUE;
                    p->tamper.outbound = TRUE;
                    strcpy(p->tamper.chance, "10.0");
                    p->tamper.checksum = TRUE;
                    
                    p->reset.enabled = FALSE;
                    p->reset.inbound = TRUE;
                    p->reset.outbound = TRUE;
                    strcpy(p->reset.chance, "0");
                    
                    p->bandwidth.enabled = FALSE;
                    p->bandwidth.inbound = TRUE;
                    p->bandwidth.outbound = TRUE;
                    strcpy(p->bandwidth.limit, "10");

                    strncpy(p->name, value, 127);
                    p->name[127] = '\0';
                    filtersSize++;
                }
            }

            if (stack_depth == 0) {
                if (strcmp(key, "hotkey") == 0) {
                    parseHotkeyConfig(value);
                    strncpy(hotkeyConfigStr, value, sizeof(hotkeyConfigStr)-1);
                }
            } else if (filtersSize > 0) {
                ProfileRecord *p = &filters[filtersSize - 1];
                
                BOOL under_profiles = FALSE;
                BOOL under_proc_filter = FALSE;
                BOOL under_duration = FALSE;
                BOOL under_modules = FALSE;
                char current_module[64] = "";

                for (int i = 0; i < stack_depth; i++) {
                    if (strcmp(stack[i].name, "profiles") == 0) {
                        under_profiles = TRUE;
                    } else if (strcmp(stack[i].name, "process_filter") == 0) {
                        under_proc_filter = TRUE;
                    } else if (strcmp(stack[i].name, "duration") == 0) {
                        under_duration = TRUE;
                    } else if (strcmp(stack[i].name, "modules") == 0) {
                        under_modules = TRUE;
                    } else if (under_modules) {
                        strncpy(current_module, stack[i].name, sizeof(current_module)-1);
                    }
                }

                if (under_profiles) {
                    if (under_modules && current_module[0] != '\0') {
                        BOOL val_bool = (strcmp(value, "true") == 0 || strcmp(value, "on") == 0 || strcmp(value, "1") == 0);
                        
                        if (strcmp(current_module, "lag") == 0) {
                            if (strcmp(key, "enabled") == 0) p->lag.enabled = val_bool;
                            else if (strcmp(key, "inbound") == 0) p->lag.inbound = val_bool;
                            else if (strcmp(key, "outbound") == 0) p->lag.outbound = val_bool;
                            else if (strcmp(key, "time") == 0) strncpy(p->lag.time, value, sizeof(p->lag.time)-1);
                        } else if (strcmp(current_module, "drop") == 0) {
                            if (strcmp(key, "enabled") == 0) p->drop.enabled = val_bool;
                            else if (strcmp(key, "inbound") == 0) p->drop.inbound = val_bool;
                            else if (strcmp(key, "outbound") == 0) p->drop.outbound = val_bool;
                            else if (strcmp(key, "chance") == 0) strncpy(p->drop.chance, value, sizeof(p->drop.chance)-1);
                        } else if (strcmp(current_module, "throttle") == 0) {
                            if (strcmp(key, "enabled") == 0) p->throttle.enabled = val_bool;
                            else if (strcmp(key, "inbound") == 0) p->throttle.inbound = val_bool;
                            else if (strcmp(key, "outbound") == 0) p->throttle.outbound = val_bool;
                            else if (strcmp(key, "chance") == 0) strncpy(p->throttle.chance, value, sizeof(p->throttle.chance)-1);
                            else if (strcmp(key, "frame") == 0) strncpy(p->throttle.frame, value, sizeof(p->throttle.frame)-1);
                            else if (strcmp(key, "drop") == 0) p->throttle.drop = val_bool;
                        } else if (strcmp(current_module, "duplicate") == 0) {
                            if (strcmp(key, "enabled") == 0) p->duplicate.enabled = val_bool;
                            else if (strcmp(key, "inbound") == 0) p->duplicate.inbound = val_bool;
                            else if (strcmp(key, "outbound") == 0) p->duplicate.outbound = val_bool;
                            else if (strcmp(key, "chance") == 0) strncpy(p->duplicate.chance, value, sizeof(p->duplicate.chance)-1);
                            else if (strcmp(key, "count") == 0) strncpy(p->duplicate.count, value, sizeof(p->duplicate.count)-1);
                        } else if (strcmp(current_module, "ood") == 0) {
                            if (strcmp(key, "enabled") == 0) p->ood.enabled = val_bool;
                            else if (strcmp(key, "inbound") == 0) p->ood.inbound = val_bool;
                            else if (strcmp(key, "outbound") == 0) p->ood.outbound = val_bool;
                            else if (strcmp(key, "chance") == 0) strncpy(p->ood.chance, value, sizeof(p->ood.chance)-1);
                        } else if (strcmp(current_module, "tamper") == 0) {
                            if (strcmp(key, "enabled") == 0) p->tamper.enabled = val_bool;
                            else if (strcmp(key, "inbound") == 0) p->tamper.inbound = val_bool;
                            else if (strcmp(key, "outbound") == 0) p->tamper.outbound = val_bool;
                            else if (strcmp(key, "chance") == 0) strncpy(p->tamper.chance, value, sizeof(p->tamper.chance)-1);
                            else if (strcmp(key, "checksum") == 0) p->tamper.checksum = val_bool;
                        } else if (strcmp(current_module, "reset") == 0) {
                            if (strcmp(key, "enabled") == 0) p->reset.enabled = val_bool;
                            else if (strcmp(key, "inbound") == 0) p->reset.inbound = val_bool;
                            else if (strcmp(key, "outbound") == 0) p->reset.outbound = val_bool;
                            else if (strcmp(key, "chance") == 0) strncpy(p->reset.chance, value, sizeof(p->reset.chance)-1);
                        } else if (strcmp(current_module, "bandwidth") == 0) {
                            if (strcmp(key, "enabled") == 0) p->bandwidth.enabled = val_bool;
                            else if (strcmp(key, "inbound") == 0) p->bandwidth.inbound = val_bool;
                            else if (strcmp(key, "outbound") == 0) p->bandwidth.outbound = val_bool;
                            else if (strcmp(key, "limit") == 0) strncpy(p->bandwidth.limit, value, sizeof(p->bandwidth.limit)-1);
                        }
                    } else if (under_proc_filter) {
                        BOOL val_bool = (strcmp(value, "true") == 0 || strcmp(value, "on") == 0 || strcmp(value, "1") == 0);
                        if (under_duration) {
                            if (strcmp(key, "enabled") == 0) p->durationEnabled = val_bool;
                            else if (strcmp(key, "value_ms") == 0) p->durationValueMs = atoi(value);
                        } else {
                            if (strcmp(key, "enabled") == 0) p->procFilterEnabled = val_bool;
                            else if (strcmp(key, "target") == 0) strncpy(p->procFilterTarget, value, sizeof(p->procFilterTarget)-1);
                        }
                    } else {
                        if (strcmp(key, "filter") == 0) {
                            strncpy(p->filter, value, sizeof(p->filter)-1);
                        } else if (strcmp(key, "name") == 0 && !is_list_item) {
                            strncpy(p->name, value, sizeof(p->name)-1);
                        }
                    }
                }
            }

            line = next_line;
        }
        LOG("Loaded %u records from YAML.", filtersSize);
    }

    if (!f || filtersSize == 0)
    {
        LOG("Failed to load from config. Fill in a simple default profile.");
        // config is missing or ill-formed. fill in some simple ones
        ProfileRecord *p = &filters[0];
        memset(p, 0, sizeof(ProfileRecord));
        strcpy(p->name, "loopback packets");
        strcpy(p->filter, "outbound and ip.DstAddr >= 127.0.0.1 and ip.DstAddr <= 127.255.255.255");
        
        p->procFilterEnabled = FALSE;
        p->durationEnabled = FALSE;
        p->durationValueMs = 10;
        
        p->lag.enabled = FALSE;
        p->lag.inbound = TRUE;
        p->lag.outbound = TRUE;
        strcpy(p->lag.time, "50");
        
        p->drop.enabled = FALSE;
        p->drop.inbound = TRUE;
        p->drop.outbound = TRUE;
        strcpy(p->drop.chance, "10.0");
        
        p->throttle.enabled = FALSE;
        p->throttle.inbound = TRUE;
        p->throttle.outbound = TRUE;
        strcpy(p->throttle.chance, "10.0");
        strcpy(p->throttle.frame, "30");
        p->throttle.drop = FALSE;
        
        p->duplicate.enabled = FALSE;
        p->duplicate.inbound = TRUE;
        p->duplicate.outbound = TRUE;
        strcpy(p->duplicate.chance, "10.0");
        strcpy(p->duplicate.count, "2");
        
        p->ood.enabled = FALSE;
        p->ood.inbound = TRUE;
        p->ood.outbound = TRUE;
        strcpy(p->ood.chance, "10.0");
        
        p->tamper.enabled = FALSE;
        p->tamper.inbound = TRUE;
        p->tamper.outbound = TRUE;
        strcpy(p->tamper.chance, "10.0");
        p->tamper.checksum = TRUE;
        
        p->reset.enabled = FALSE;
        p->reset.inbound = TRUE;
        p->reset.outbound = TRUE;
        strcpy(p->reset.chance, "0");
        
        p->bandwidth.enabled = FALSE;
        p->bandwidth.inbound = TRUE;
        p->bandwidth.outbound = TRUE;
        strcpy(p->bandwidth.limit, "10");

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
    
    // Save process filter state
    {
        const char *procVal = uiGetProcessFilterTarget();
        fprintf(f, "process-filter-target: %s\n", procVal ? procVal : "");
        short procEnabled = uiIsProcessFilterEnabled();
        fprintf(f, "process-filter-enabled: %s\n", procEnabled ? "on" : "off");
        fprintf(f, "process-filter-duration: %d\n", uiGetDurationValue());
        fprintf(f, "process-filter-duration-enabled: %s\n", uiIsDurationEnabled() ? "on" : "off");
    }

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
    Ihandle *processFilterFrame = uiCreateProcessFilterFrame();

    // dialog
    dialog = IupDialog(
        dialogVBox = IupVbox(
            topFrame,
            processFilterFrame,
            bottomFrame,
            statusLabel,
            NULL
        )
    );

    IupSetAttribute(dialog, "TITLE", "clumsy " CLUMSY_VERSION);
    IupSetAttribute(dialog, "SIZE", "540x"); // add padding manually to width (extra space for hotkey label)
    IupSetAttribute(dialog, "MINSIZE", "450x300");
    IupSetAttribute(dialog, "RESIZE", "YES");
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

    const char *manualFilter = IupGetAttribute(filterText, "VALUE");
    static char combinedFilter[4096];

    if (uiIsProcessFilterEnabled()) {
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
        int ms = uiGetDurationValue();
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



