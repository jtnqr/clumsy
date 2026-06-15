#pragma once
#include <winsock2.h>
#include <Windows.h>
#include "common.h"

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

extern UINT filtersSize;
extern ProfileRecord filters[CONFIG_MAX_RECORDS];
extern char configBuf[CONFIG_BUF_SIZE+2];
extern BOOL parameterized;
extern BOOL stateLoaded;

void loadConfig(void);
void saveState(void);
void loadState(void);
