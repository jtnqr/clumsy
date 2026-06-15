#include "config_manager.h"
#include "common.h"
#include "hotkey_manager.h"
#include "iup.h"
#include "ui_callbacks.h"
#include "ui_components.h"
#include <Windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>

UINT filtersSize = 0;
ProfileRecord filters[CONFIG_MAX_RECORDS] = {0};
BOOL parameterized = FALSE;
BOOL stateLoaded = FALSE;

extern Ihandle *filterText;

static const char *default_yaml_template =
    "# ============================================================================\n"
    "# CLUMSY GLOBAL CONFIGURATION\n"
    "# ============================================================================\n"
    "# This file defines the core operational profiles (presets) for packet manipulation.\n"
    "#\n"
    "# Profile Schema:\n"
    "#   - name: \"Profile Name\"      # Display name of the preset\n"
    "#     filter: \"WinDivert filter\" # e.g. \"inbound\", \"outbound and tcp\", \"udp\"\n"
    "#     process_filter:            # (Optional) Target specific processes\n"
    "#       enabled: true/false\n"
    "#       target: \"process_name\"   # e.g., \"roblox\", \"notepad\"\n"
    "#       duration:                # (Optional) Duration controls\n"
    "#         enabled: true/false\n"
    "#         value_ms: 10\n"
    "#     modules:                   # (Optional) Interception modules\n"
    "#                                # Only active/enabled modules need to be specified.\n"
    "#\n"
    "# Module Reference & Examples:\n"
    "#\n"
    "#   1. lag: Delay packets by a specified time.\n"
    "#      lag:\n"
    "#        enabled: true\n"
    "#        inbound: true           # Intercept inbound packets\n"
    "#        outbound: true          # Intercept outbound packets\n"
    "#        time: 100               # Delay time in milliseconds (integer)\n"
    "#\n"
    "#   2. drop: Drop packets randomly.\n"
    "#      drop:\n"
    "#        enabled: true\n"
    "#        inbound: true\n"
    "#        outbound: true\n"
    "#        chance: 10.0            # Drop probability in % (float, 0.0 - 100.0)\n"
    "#\n"
    "#   3. throttle: Block traffic for a timeframe then send it.\n"
    "#      throttle:\n"
    "#        enabled: true\n"
    "#        inbound: true\n"
    "#        outbound: true\n"
    "#        chance: 20.0            # Throttle probability in %\n"
    "#        frame: 30               # Block duration window in ms\n"
    "#        drop: false             # Whether to drop throttled packets instead of sending\n"
    "#\n"
    "#   4. duplicate: Send copy of packets.\n"
    "#      duplicate:\n"
    "#        enabled: true\n"
    "#        inbound: true\n"
    "#        outbound: true\n"
    "#        chance: 15.0            # Duplication probability in %\n"
    "#        count: 2                # Number of duplicate packets to send (integer)\n"
    "#\n"
    "#   5. ood: Out of order (ood) packets delivery.\n"
    "#      ood:\n"
    "#        enabled: true\n"
    "#        inbound: true\n"
    "#        outbound: true\n"
    "#        chance: 10.0            # Probability of out-of-order delivery in %\n"
    "#\n"
    "#   6. tamper: Tamper packet checksum or payloads.\n"
    "#      tamper:\n"
    "#        enabled: true\n"
    "#        inbound: true\n"
    "#        outbound: true\n"
    "#        chance: 5.0             # Tampering probability in %\n"
    "#        checksum: true          # Recalculate checksum or break it (bool)\n"
    "#\n"
    "#   7. reset: Send TCP reset / ICMP unreachable packets.\n"
    "#      reset:\n"
    "#        enabled: true\n"
    "#        inbound: true\n"
    "#        outbound: true\n"
    "#        chance: 10.0            # Probability in %\n"
    "#\n"
    "#   8. bandwidth: Limit bandwidth rate.\n"
    "#      bandwidth:\n"
    "#        enabled: true\n"
    "#        inbound: true\n"
    "#        outbound: true\n"
    "#        limit: 10               # Bandwidth limit (e.g. KB/s)\n"
    "# ============================================================================\n"
    "\n"
    "hotkey: \"f6\"\n"
    "\n"
    "profiles:\n"
    "  - name: \"roblox inbound\"\n"
    "    filter: \"inbound\"\n"
    "    process_filter:\n"
    "      enabled: true\n"
    "      target: \"roblox\"\n"
    "      duration:\n"
    "        enabled: false\n"
    "        value_ms: 10\n"
    "      modules:\n"
    "        drop:\n"
    "          enabled: true\n"
    "          inbound: true\n"
    "          outbound: false\n"
    "          chance: 100.0\n"
    "        bandwidth:\n"
    "          enabled: true\n"
    "          inbound: true\n"
    "          outbound: false\n"
    "          limit: 1\n"
    "\n"
    "  - name: \"localhost ipv4 all\"\n"
    "    filter: \"outbound and loopback\"\n"
    "\n"
    "  - name: \"localhost ipv4 tcp\"\n"
    "    filter: \"tcp and outbound and loopback\"\n"
    "\n"
    "  - name: \"localhost ipv4 udp\"\n"
    "    filter: \"udp and outbound and loopback\"\n"
    "\n"
    "  - name: \"all sending packets\"\n"
    "    filter: \"outbound\"\n"
    "\n"
    "  - name: \"all receiving packets\"\n"
    "    filter: \"inbound\"\n"
    "\n"
    "  - name: \"all ipv4 against specific ip\"\n"
    "    filter: \"ip.DstAddr == 198.51.100.1 or ip.SrcAddr == 198.51.100.1\"\n"
    "\n"
    "  - name: \"tcp ipv4 against specific ip\"\n"
    "    filter: \"tcp and (ip.DstAddr == 198.51.100.1 or ip.SrcAddr == 198.51.100.1)\"\n"
    "\n"
    "  - name: \"udp ipv4 against specific ip\"\n"
    "    filter: \"udp and (ip.DstAddr == 198.51.100.1 or ip.SrcAddr == 198.51.100.1)\"\n"
    "\n"
    "  - name: \"all ipv4 against port\"\n"
    "    filter: \"tcp.DstPort == 12354 or tcp.SrcPort == 12354 or udp.DstPort == 12354 or udp.SrcPort == 12354\"\n"
    "\n"
    "  - name: \"tcp ipv4 against port\"\n"
    "    filter: \"tcp and (tcp.DstPort == 12354 or tcp.SrcPort == 12354)\"\n"
    "\n"
    "  - name: \"udp ipv4 against port\"\n"
    "    filter: \"udp and (udp.DstPort == 12354 or udp.SrcPort == 12354)\"\n"
    "\n"
    "  - name: \"ipv6 all\"\n"
    "    filter: \"ipv6\"\n";

// loading up filters and fill in
void loadConfig() {
  char path[MSG_BUFSIZE];
  char *p;
  FILE *f;
  long size = 0;

  GetModuleFileName(NULL, path, MSG_BUFSIZE);
  LOG("Executable path: %s", path);
  p = strrchr(path, '\\');
  if (p == NULL)
    p = strrchr(path, '/'); // holy shit
  if (p == NULL ||
      (size_t)(p - path + 1 + strlen(CONFIG_FILE)) >= MSG_BUFSIZE) {
    LOG("Path too long for config file");
    return;
  }
  snprintf(p + 1, MSG_BUFSIZE - (p - path + 1), "%s", CONFIG_FILE);
  LOG("Config path: %s", path);

  // Open file once to check existence, size, and readiness
  f = fopen(path, "r");
  if (!f) {
    LOG("config.yaml not found, generating default...");
    FILE *writeF = fopen(path, "w");
    if (writeF) {
      fprintf(writeF, "%s", default_yaml_template);
      fclose(writeF);
      LOG("Successfully generated config.yaml at %s", path);
    } else {
      LOG("Error: Failed to write config.yaml to %s", path);
      return;
    }
    f = fopen(path, "r");
  } else {
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
      fclose(f);
      LOG("config.yaml is empty, regenerating default...");
      FILE *writeF = fopen(path, "w");
      if (writeF) {
        fprintf(writeF, "%s", default_yaml_template);
        fclose(writeF);
      }
      f = fopen(path, "r");
    }
  }

  filtersSize = 0;
  if (f) {
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size > 0) {
      char *dynamicBuf = (char *)malloc(size + 2);
      if (dynamicBuf) {
        size_t len = fread(dynamicBuf, sizeof(char), size, f);
        dynamicBuf[len] = '\n';
        dynamicBuf[len + 1] = '\0';

        // parse out YAML structure
        char *line = dynamicBuf;

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
            if (*p_line == '\t')
              indent += 4;
            else
              indent++;
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
          while (*key == ' ')
            key++;
          char *key_end = key + strlen(key) - 1;
          while (key_end > key && *key_end == ' ') {
            *key_end = '\0';
            key_end--;
          }

          // Trim spaces from value
          while (*value == ' ')
            value++;
          char *value_end = value + strlen(value) - 1;
          while (value_end > value && *value_end == ' ') {
            *value_end = '\0';
            value_end--;
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

          // Remove surrounding quotes from value
          if (strlen(value) >= 2 &&
              ((value[0] == '"' && value[value_end - value] == '"') ||
               (value[0] == '\'' && value[value_end - value] == '\''))) {
            value[value_end - value] = '\0';
            value++;
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
              strncpy(hotkeyConfigStr, value, sizeof(hotkeyConfigStr) - 1);
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
                strncpy(current_module, stack[i].name,
                        sizeof(current_module) - 1);
              }
            }

            if (under_profiles) {
              if (under_modules && current_module[0] != '\0') {
                BOOL val_bool =
                    (strcmp(value, "true") == 0 || strcmp(value, "on") == 0 ||
                     strcmp(value, "1") == 0);

                if (strcmp(current_module, "lag") == 0) {
                  if (strcmp(key, "enabled") == 0)
                    p->lag.enabled = val_bool;
                  else if (strcmp(key, "inbound") == 0)
                    p->lag.inbound = val_bool;
                  else if (strcmp(key, "outbound") == 0)
                    p->lag.outbound = val_bool;
                  else if (strcmp(key, "time") == 0)
                    strncpy(p->lag.time, value, sizeof(p->lag.time) - 1);
                } else if (strcmp(current_module, "drop") == 0) {
                  if (strcmp(key, "enabled") == 0)
                    p->drop.enabled = val_bool;
                  else if (strcmp(key, "inbound") == 0)
                    p->drop.inbound = val_bool;
                  else if (strcmp(key, "outbound") == 0)
                    p->drop.outbound = val_bool;
                  else if (strcmp(key, "chance") == 0)
                    strncpy(p->drop.chance, value, sizeof(p->drop.chance) - 1);
                } else if (strcmp(current_module, "throttle") == 0) {
                  if (strcmp(key, "enabled") == 0)
                    p->throttle.enabled = val_bool;
                  else if (strcmp(key, "inbound") == 0)
                    p->throttle.inbound = val_bool;
                  else if (strcmp(key, "outbound") == 0)
                    p->throttle.outbound = val_bool;
                  else if (strcmp(key, "chance") == 0)
                    strncpy(p->throttle.chance, value,
                            sizeof(p->throttle.chance) - 1);
                  else if (strcmp(key, "frame") == 0)
                    strncpy(p->throttle.frame, value,
                            sizeof(p->throttle.frame) - 1);
                  else if (strcmp(key, "drop") == 0)
                    p->throttle.drop = val_bool;
                } else if (strcmp(current_module, "duplicate") == 0) {
                  if (strcmp(key, "enabled") == 0)
                    p->duplicate.enabled = val_bool;
                  else if (strcmp(key, "inbound") == 0)
                    p->duplicate.inbound = val_bool;
                  else if (strcmp(key, "outbound") == 0)
                    p->duplicate.outbound = val_bool;
                  else if (strcmp(key, "chance") == 0)
                    strncpy(p->duplicate.chance, value,
                            sizeof(p->duplicate.chance) - 1);
                  else if (strcmp(key, "count") == 0)
                    strncpy(p->duplicate.count, value,
                            sizeof(p->duplicate.count) - 1);
                } else if (strcmp(current_module, "ood") == 0) {
                  if (strcmp(key, "enabled") == 0)
                    p->ood.enabled = val_bool;
                  else if (strcmp(key, "inbound") == 0)
                    p->ood.inbound = val_bool;
                  else if (strcmp(key, "outbound") == 0)
                    p->ood.outbound = val_bool;
                  else if (strcmp(key, "chance") == 0)
                    strncpy(p->ood.chance, value, sizeof(p->ood.chance) - 1);
                } else if (strcmp(current_module, "tamper") == 0) {
                  if (strcmp(key, "enabled") == 0)
                    p->tamper.enabled = val_bool;
                  else if (strcmp(key, "inbound") == 0)
                    p->tamper.inbound = val_bool;
                  else if (strcmp(key, "outbound") == 0)
                    p->tamper.outbound = val_bool;
                  else if (strcmp(key, "chance") == 0)
                    strncpy(p->tamper.chance, value,
                            sizeof(p->tamper.chance) - 1);
                  else if (strcmp(key, "checksum") == 0)
                    p->tamper.checksum = val_bool;
                } else if (strcmp(current_module, "reset") == 0) {
                  if (strcmp(key, "enabled") == 0)
                    p->reset.enabled = val_bool;
                  else if (strcmp(key, "inbound") == 0)
                    p->reset.inbound = val_bool;
                  else if (strcmp(key, "outbound") == 0)
                    p->reset.outbound = val_bool;
                  else if (strcmp(key, "chance") == 0)
                    strncpy(p->reset.chance, value,
                            sizeof(p->reset.chance) - 1);
                } else if (strcmp(current_module, "bandwidth") == 0) {
                  if (strcmp(key, "enabled") == 0)
                    p->bandwidth.enabled = val_bool;
                  else if (strcmp(key, "inbound") == 0)
                    p->bandwidth.inbound = val_bool;
                  else if (strcmp(key, "outbound") == 0)
                    p->bandwidth.outbound = val_bool;
                  else if (strcmp(key, "limit") == 0)
                    strncpy(p->bandwidth.limit, value,
                            sizeof(p->bandwidth.limit) - 1);
                }
              } else if (under_proc_filter) {
                BOOL val_bool =
                    (strcmp(value, "true") == 0 || strcmp(value, "on") == 0 ||
                     strcmp(value, "1") == 0);
                if (under_duration) {
                  if (strcmp(key, "enabled") == 0)
                    p->durationEnabled = val_bool;
                  else if (strcmp(key, "value_ms") == 0)
                    p->durationValueMs = atoi(value);
                } else {
                  if (strcmp(key, "enabled") == 0)
                    p->procFilterEnabled = val_bool;
                  else if (strcmp(key, "target") == 0)
                    strncpy(p->procFilterTarget, value,
                            sizeof(p->procFilterTarget) - 1);
                }
              } else {
                if (strcmp(key, "filter") == 0) {
                  strncpy(p->filter, value, sizeof(p->filter) - 1);
                } else if (strcmp(key, "name") == 0 && !is_list_item) {
                  strncpy(p->name, value, sizeof(p->name) - 1);
                }
              }
            }
          }

          line = next_line;
        }
        free(dynamicBuf);
        LOG("Loaded %u records from YAML.", filtersSize);
      } else {
        LOG("Error: Out of memory allocating %ld bytes for config", size);
      }
    }
    fclose(f);
  }

  if (filtersSize == 0) {
    LOG("Error: Failed to load profiles from config.yaml. No fallback presets "
        "configured in memory.");
  }
}

void saveConfig(void) {
  char path[MSG_BUFSIZE];
  FILE *f;
  char *p;

  GetModuleFileName(NULL, path, (DWORD)sizeof(path));
  p = strrchr(path, '\\');
  if (p == NULL)
    p = strrchr(path, '/');
  if (p == NULL ||
      (size_t)(p - path + 1 + strlen("config.yaml")) >= sizeof(path)) {
    LOG("Path too long for config file");
    return;
  }
  snprintf(p + 1, sizeof(path) - (p - path + 1), "config.yaml");

  LOG("Saving configuration to: %s", path);
  f = fopen(path, "w");
  if (!f) {
    LOG("Error: Failed to open config.yaml for writing");
    return;
  }

  fprintf(f, "# ============================================================================\n");
  fprintf(f, "# CLUMSY VERSION %s GLOBAL CONFIGURATION (AUTO-GENERATED)\n", CLUMSY_VERSION);
  fprintf(f, "# ============================================================================\n");
  fprintf(f, "# This file defines the core operational profiles (presets) for packet manipulation.\n");
  fprintf(f, "#\n");
  fprintf(f, "# Profile Schema:\n");
  fprintf(f, "#   - name: \"Profile Name\"      # Display name of the preset\n");
  fprintf(f, "#     filter: \"WinDivert filter\" # e.g. \"inbound\", \"outbound and tcp\", \"udp\"\n");
  fprintf(f, "#     process_filter:            # (Optional) Target specific processes\n");
  fprintf(f, "#       enabled: true/false\n");
  fprintf(f, "#       target: \"process_name\"   # e.g., \"roblox\", \"notepad\"\n");
  fprintf(f, "#       duration:                # (Optional) Duration controls\n");
  fprintf(f, "#         enabled: true/false\n");
  fprintf(f, "#         value_ms: 10\n");
  fprintf(f, "#     modules:                   # (Optional) Interception modules\n");
  fprintf(f, "#                                # Only active/enabled modules need to be specified.\n");
  fprintf(f, "#\n");
  fprintf(f, "# Module Reference & Examples:\n");
  fprintf(f, "#\n");
  fprintf(f, "#   1. lag: Delay packets by a specified time.\n");
  fprintf(f, "#      lag:\n");
  fprintf(f, "#        enabled: true\n");
  fprintf(f, "#        inbound: true           # Intercept inbound packets\n");
  fprintf(f, "#        outbound: true          # Intercept outbound packets\n");
  fprintf(f, "#        time: 100               # Delay time in milliseconds (integer)\n");
  fprintf(f, "#\n");
  fprintf(f, "#   2. drop: Drop packets randomly.\n");
  fprintf(f, "#      drop:\n");
  fprintf(f, "#        enabled: true\n");
  fprintf(f, "#        inbound: true\n");
  fprintf(f, "#        outbound: true\n");
  fprintf(f, "#        chance: 10.0            # Drop probability in %% (float, 0.0 - 100.0)\n");
  fprintf(f, "#\n");
  fprintf(f, "#   3. throttle: Block traffic for a timeframe then send it.\n");
  fprintf(f, "#      throttle:\n");
  fprintf(f, "#        enabled: true\n");
  fprintf(f, "#        inbound: true\n");
  fprintf(f, "#        outbound: true\n");
  fprintf(f, "#        chance: 20.0            # Throttle probability in %%\n");
  fprintf(f, "#        frame: 30               # Block duration window in ms\n");
  fprintf(f, "#        drop: false             # Whether to drop throttled packets instead of sending\n");
  fprintf(f, "#\n");
  fprintf(f, "#   4. duplicate: Send copy of packets.\n");
  fprintf(f, "#      duplicate:\n");
  fprintf(f, "#        enabled: true\n");
  fprintf(f, "#        inbound: true\n");
  fprintf(f, "#        outbound: true\n");
  fprintf(f, "#        chance: 15.0            # Duplication probability in %%\n");
  fprintf(f, "#        count: 2                # Number of duplicate packets to send (integer)\n");
  fprintf(f, "#\n");
  fprintf(f, "#   5. ood: Out of order (ood) packets delivery.\n");
  fprintf(f, "#      ood:\n");
  fprintf(f, "#        enabled: true\n");
  fprintf(f, "#        inbound: true\n");
  fprintf(f, "#        outbound: true\n");
  fprintf(f, "#        chance: 10.0            # Probability of out-of-order delivery in %%\n");
  fprintf(f, "#\n");
  fprintf(f, "#   6. tamper: Tamper packet checksum or payloads.\n");
  fprintf(f, "#      tamper:\n");
  fprintf(f, "#        enabled: true\n");
  fprintf(f, "#        inbound: true\n");
  fprintf(f, "#        outbound: true\n");
  fprintf(f, "#        chance: 5.0             # Tampering probability in %%\n");
  fprintf(f, "#        checksum: true          # Recalculate checksum or break it (bool)\n");
  fprintf(f, "#\n");
  fprintf(f, "#   7. reset: Send TCP reset / ICMP unreachable packets.\n");
  fprintf(f, "#      reset:\n");
  fprintf(f, "#        enabled: true\n");
  fprintf(f, "#        inbound: true\n");
  fprintf(f, "#        outbound: true\n");
  fprintf(f, "#        chance: 10.0            # Probability in %%\n");
  fprintf(f, "#\n");
  fprintf(f, "#   8. bandwidth: Limit bandwidth rate.\n");
  fprintf(f, "#      bandwidth:\n");
  fprintf(f, "#        enabled: true\n");
  fprintf(f, "#        inbound: true\n");
  fprintf(f, "#        outbound: true\n");
  fprintf(f, "#        limit: 10               # Bandwidth limit (e.g. KB/s)\n");
  fprintf(f, "# ============================================================================\n\n");

  fprintf(f, "hotkey: \"%s\"\n\n", hotkeyConfigStr);
  fprintf(f, "profiles:\n");

  for (UINT i = 0; i < filtersSize; ++i) {
    ProfileRecord *pr = &filters[i];
    fprintf(f, "  - name: \"%s\"\n", pr->name);
    fprintf(f, "    filter: \"%s\"\n", pr->filter);
    
    if (pr->procFilterEnabled || pr->procFilterTarget[0] != '\0' || pr->durationEnabled) {
      fprintf(f, "    process_filter:\n");
      fprintf(f, "      enabled: %s\n", pr->procFilterEnabled ? "true" : "false");
      fprintf(f, "      target: \"%s\"\n", pr->procFilterTarget);
      if (pr->durationEnabled || pr->durationValueMs != 10) {
        fprintf(f, "      duration:\n");
        fprintf(f, "        enabled: %s\n", pr->durationEnabled ? "true" : "false");
        fprintf(f, "        value_ms: %d\n", pr->durationValueMs);
      }
    }

    BOOL has_modules = pr->lag.enabled || pr->drop.enabled || pr->throttle.enabled ||
                       pr->duplicate.enabled || pr->ood.enabled || pr->tamper.enabled ||
                       pr->reset.enabled || pr->bandwidth.enabled;

    if (has_modules) {
      fprintf(f, "      modules:\n");

      // lag
      if (pr->lag.enabled) {
        fprintf(f, "        lag:\n");
        fprintf(f, "          enabled: true\n");
        fprintf(f, "          inbound: %s\n", pr->lag.inbound ? "true" : "false");
        fprintf(f, "          outbound: %s\n", pr->lag.outbound ? "true" : "false");
        fprintf(f, "          time: %s\n", pr->lag.time);
      }

      // drop
      if (pr->drop.enabled) {
        fprintf(f, "        drop:\n");
        fprintf(f, "          enabled: true\n");
        fprintf(f, "          inbound: %s\n", pr->drop.inbound ? "true" : "false");
        fprintf(f, "          outbound: %s\n", pr->drop.outbound ? "true" : "false");
        fprintf(f, "          chance: %s\n", pr->drop.chance);
      }

      // throttle
      if (pr->throttle.enabled) {
        fprintf(f, "        throttle:\n");
        fprintf(f, "          enabled: true\n");
        fprintf(f, "          inbound: %s\n", pr->throttle.inbound ? "true" : "false");
        fprintf(f, "          outbound: %s\n", pr->throttle.outbound ? "true" : "false");
        fprintf(f, "          chance: %s\n", pr->throttle.chance);
        fprintf(f, "          frame: %s\n", pr->throttle.frame);
        fprintf(f, "          drop: %s\n", pr->throttle.drop ? "true" : "false");
      }

      // duplicate
      if (pr->duplicate.enabled) {
        fprintf(f, "        duplicate:\n");
        fprintf(f, "          enabled: true\n");
        fprintf(f, "          inbound: %s\n", pr->duplicate.inbound ? "true" : "false");
        fprintf(f, "          outbound: %s\n", pr->duplicate.outbound ? "true" : "false");
        fprintf(f, "          chance: %s\n", pr->duplicate.chance);
        fprintf(f, "          count: %s\n", pr->duplicate.count);
      }

      // ood
      if (pr->ood.enabled) {
        fprintf(f, "        ood:\n");
        fprintf(f, "          enabled: true\n");
        fprintf(f, "          inbound: %s\n", pr->ood.inbound ? "true" : "false");
        fprintf(f, "          outbound: %s\n", pr->ood.outbound ? "true" : "false");
        fprintf(f, "          chance: %s\n", pr->ood.chance);
      }

      // tamper
      if (pr->tamper.enabled) {
        fprintf(f, "        tamper:\n");
        fprintf(f, "          enabled: true\n");
        fprintf(f, "          inbound: %s\n", pr->tamper.inbound ? "true" : "false");
        fprintf(f, "          outbound: %s\n", pr->tamper.outbound ? "true" : "false");
        fprintf(f, "          chance: %s\n", pr->tamper.chance);
        fprintf(f, "          checksum: %s\n", pr->tamper.checksum ? "true" : "false");
      }

      // reset
      if (pr->reset.enabled) {
        fprintf(f, "        reset:\n");
        fprintf(f, "          enabled: true\n");
        fprintf(f, "          inbound: %s\n", pr->reset.inbound ? "true" : "false");
        fprintf(f, "          outbound: %s\n", pr->reset.outbound ? "true" : "false");
        fprintf(f, "          chance: %s\n", pr->reset.chance);
      }

      // bandwidth
      if (pr->bandwidth.enabled) {
        fprintf(f, "        bandwidth:\n");
        fprintf(f, "          enabled: true\n");
        fprintf(f, "          inbound: %s\n", pr->bandwidth.inbound ? "true" : "false");
        fprintf(f, "          outbound: %s\n", pr->bandwidth.outbound ? "true" : "false");
        fprintf(f, "          limit: %s\n", pr->bandwidth.limit);
      }
    }

    fprintf(f, "\n");
  }
  fclose(f);
  LOG("Configuration saved successfully.");
}

// Get path to state file (in same directory as executable)
static void getStatePath(char *path, size_t pathSize) {
  char *p;
  GetModuleFileName(NULL, path, (DWORD)pathSize);
  p = strrchr(path, '\\');
  if (p == NULL)
    p = strrchr(path, '/');
  if (p == NULL || (size_t)(p - path + 1 + strlen(STATE_FILE)) >= pathSize) {
    LOG("Path too long for state file");
    return;
  }
  snprintf(p + 1, pathSize - (p - path + 1), "%s", STATE_FILE);
}

// Save current state to state.txt
void saveState(void) {
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
  {
    UINT ix;
    for (ix = 0; ix < MODULE_CNT; ++ix) {
      Module *module = modules[ix];
      short enabled = *(module->enabledFlag);
      fprintf(f, "%s: %s\n", module->shortName, enabled ? "on" : "off");
    }
  }

  // Save detailed module settings
  fprintf(f, "lag-inbound: %s\n",
          IupGetGlobal("lag-inbound") ? IupGetGlobal("lag-inbound") : "on");
  fprintf(f, "lag-outbound: %s\n",
          IupGetGlobal("lag-outbound") ? IupGetGlobal("lag-outbound") : "on");
  fprintf(f, "lag-time: %s\n",
          IupGetGlobal("lag-time") ? IupGetGlobal("lag-time") : "50");

  fprintf(f, "drop-inbound: %s\n",
          IupGetGlobal("drop-inbound") ? IupGetGlobal("drop-inbound") : "on");
  fprintf(f, "drop-outbound: %s\n",
          IupGetGlobal("drop-outbound") ? IupGetGlobal("drop-outbound") : "on");
  fprintf(f, "drop-chance: %s\n",
          IupGetGlobal("drop-chance") ? IupGetGlobal("drop-chance") : "10.0");

  fprintf(f, "throttle-inbound: %s\n",
          IupGetGlobal("throttle-inbound") ? IupGetGlobal("throttle-inbound")
                                           : "on");
  fprintf(f, "throttle-outbound: %s\n",
          IupGetGlobal("throttle-outbound") ? IupGetGlobal("throttle-outbound")
                                            : "on");
  fprintf(f, "throttle-chance: %s\n",
          IupGetGlobal("throttle-chance") ? IupGetGlobal("throttle-chance")
                                          : "10.0");
  fprintf(f, "throttle-frame: %s\n",
          IupGetGlobal("throttle-frame") ? IupGetGlobal("throttle-frame")
                                         : "30");

  fprintf(f, "ood-inbound: %s\n",
          IupGetGlobal("ood-inbound") ? IupGetGlobal("ood-inbound") : "on");
  fprintf(f, "ood-outbound: %s\n",
          IupGetGlobal("ood-outbound") ? IupGetGlobal("ood-outbound") : "on");
  fprintf(f, "ood-chance: %s\n",
          IupGetGlobal("ood-chance") ? IupGetGlobal("ood-chance") : "10.0");

  fprintf(f, "duplicate-inbound: %s\n",
          IupGetGlobal("duplicate-inbound") ? IupGetGlobal("duplicate-inbound")
                                            : "on");
  fprintf(f, "duplicate-outbound: %s\n",
          IupGetGlobal("duplicate-outbound")
              ? IupGetGlobal("duplicate-outbound")
              : "on");
  fprintf(f, "duplicate-chance: %s\n",
          IupGetGlobal("duplicate-chance") ? IupGetGlobal("duplicate-chance")
                                           : "10.0");
  fprintf(f, "duplicate-count: %s\n",
          IupGetGlobal("duplicate-count") ? IupGetGlobal("duplicate-count")
                                          : "2");

  fprintf(f, "tamper-inbound: %s\n",
          IupGetGlobal("tamper-inbound") ? IupGetGlobal("tamper-inbound")
                                         : "on");
  fprintf(f, "tamper-outbound: %s\n",
          IupGetGlobal("tamper-outbound") ? IupGetGlobal("tamper-outbound")
                                          : "on");
  fprintf(f, "tamper-chance: %s\n",
          IupGetGlobal("tamper-chance") ? IupGetGlobal("tamper-chance")
                                        : "10.0");
  fprintf(f, "tamper-checksum: %s\n",
          IupGetGlobal("tamper-checksum") ? IupGetGlobal("tamper-checksum")
                                          : "on");

  fprintf(f, "reset-inbound: %s\n",
          IupGetGlobal("reset-inbound") ? IupGetGlobal("reset-inbound") : "on");
  fprintf(f, "reset-outbound: %s\n",
          IupGetGlobal("reset-outbound") ? IupGetGlobal("reset-outbound")
                                         : "on");
  fprintf(f, "reset-chance: %s\n",
          IupGetGlobal("reset-chance") ? IupGetGlobal("reset-chance") : "0");

  fprintf(f, "bandwidth-inbound: %s\n",
          IupGetGlobal("bandwidth-inbound") ? IupGetGlobal("bandwidth-inbound")
                                            : "on");
  fprintf(f, "bandwidth-outbound: %s\n",
          IupGetGlobal("bandwidth-outbound")
              ? IupGetGlobal("bandwidth-outbound")
              : "on");
  fprintf(f, "bandwidth-bandwidth: %s\n",
          IupGetGlobal("bandwidth-bandwidth")
              ? IupGetGlobal("bandwidth-bandwidth")
              : "10");

  // Save process filter state
  {
    const char *procVal = uiGetProcessFilterTarget();
    fprintf(f, "process-filter-target: %s\n", procVal ? procVal : "");
    short procEnabled = uiIsProcessFilterEnabled();
    fprintf(f, "process-filter-enabled: %s\n", procEnabled ? "on" : "off");
    fprintf(f, "process-filter-duration: %d\n", uiGetDurationValue());
  }

  // Save current preset selection
  {
    int selected = IupGetInt(filterSelectList, "VALUE");
    if (selected >= 1 && selected <= (int)filtersSize) {
      LOG("Saving preset selection: %s", filters[selected - 1].name);
      fprintf(f, "preset: %s\n", filters[selected - 1].name);
    } else {
      LOG("Saving preset selection: <custom>");
      fprintf(f, "preset: <custom>\n");
    }
  }

  fclose(f);
  LOG("State saved successfully");
}

// Load state from state.txt (sets IupGlobal values that get applied during UI
// setup)
void loadState(void) {
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
    char localBuf[2048 + 2];
    size_t len;
    char *current, *last;
    len = fread(localBuf, sizeof(char), 2048, f);
    localBuf[len] = '\n';
    localBuf[len + 1] = '\0';
    fclose(f);

    // Parse key: value pairs
    last = current = localBuf;
    do {
      char *key, *value;

      // Skip whitespace and comments
      while (isspace((unsigned char)*current)) {
        ++current;
      }
      if (*current == '#') {
        current = strchr(current, '\n');
        if (!current)
          break;
        current++;
        continue;
      }
      if (*current == '\0')
        break;

      // Parse key
      key = current;
      current = strchr(current, ':');
      if (!current)
        break;
      *current = '\0';
      current++;

      // Skip space after :
      while (*current == ' ')
        current++;

      // Parse value
      value = current;
      current = strchr(current, '\n');
      if (current) {
        *current = '\0';
        if (*(current - 1) == '\r')
          *(current - 1) = '\0';
        current++;
      }

      // Store in IupGlobal for setFromParameter to use
      LOG("State: %s = %s", key, value);
      IupStoreGlobal(key, value);

      last = current;
    } while (current && current - localBuf < 2048);
  }

  LOG("State loaded");
}
