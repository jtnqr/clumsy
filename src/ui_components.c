#include <string.h>
#include "ui_components.h"

static Ihandle *processFilterFrame = NULL;
static Ihandle *processFilterToggle = NULL;
static Ihandle *processFilterText = NULL;
static Ihandle *processFilterLabel = NULL;

Ihandle* uiCreateProcessFilterFrame(void) {
    processFilterFrame = IupFrame(
        IupHbox(
            processFilterLabel = IupLabel("Target Process:"),
            processFilterText = IupText(NULL),
            processFilterToggle = IupToggle("Enable Process Filter", NULL),
            NULL
        )
    );
    IupSetAttribute(processFilterFrame, "TITLE", "Process Filter");
    IupSetAttribute(processFilterFrame, "EXPAND", "HORIZONTAL");
    IupSetAttribute(processFilterText, "VISIBLECOLUMNS", "12");
    IupSetAttribute(processFilterText, "VALUE", "roblox");
    IupSetAttribute(processFilterToggle, "VALUE", "OFF");

    Ihandle *hbox = IupGetChild(processFilterFrame, 0);
    IupSetAttribute(hbox, "ALIGNMENT", "ACENTER");
    IupSetAttribute(hbox, "GAP", "8");
    IupSetAttribute(hbox, "NCMARGIN", "4x4");

    // Restore from IupGlobal if loaded from state
    if (IupGetGlobal("process-filter-target")) {
        IupStoreAttribute(processFilterText, "VALUE", IupGetGlobal("process-filter-target"));
    }
    if (IupGetGlobal("process-filter-enabled")) {
        const char *enabled = IupGetGlobal("process-filter-enabled");
        if (strcmp(enabled, "on") == 0) {
            IupSetAttribute(processFilterToggle, "VALUE", "ON");
        } else {
            IupSetAttribute(processFilterToggle, "VALUE", "OFF");
        }
    }

    return processFilterFrame;
}

BOOL uiIsProcessFilterEnabled(void) {
    if (!processFilterToggle) return FALSE;
    return IupGetInt(processFilterToggle, "VALUE");
}

const char* uiGetProcessFilterTarget(void) {
    if (!processFilterText) return "roblox";
    const char *val = IupGetAttribute(processFilterText, "VALUE");
    return val ? val : "roblox";
}

void uiSetProcessFilterActive(BOOL active) {
    if (processFilterText) {
        IupSetAttribute(processFilterText, "ACTIVE", active ? "YES" : "NO");
    }
    if (processFilterToggle) {
        IupSetAttribute(processFilterToggle, "ACTIVE", active ? "YES" : "NO");
    }
}
