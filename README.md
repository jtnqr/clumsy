# clumsy

**clumsy makes your network condition on Windows significantly worse, but in a managed and interactive manner.**

Leveraging the awesome [WinDivert](http://reqrypt.org/windivert.html), clumsy stops living network packets and capture them, lag/drop/tamper/.. the packets on demand, then send them away. Whether you want to track down weird bugs related to broken network, or evaluate your application on poor connections, clumsy will come in handy:

- No installation.
- No need for proxy setup or code change in your application.
- System wide network capturing means it works on any application.
- Works even if you're offline (ie, connecting from localhost to localhost).
- Your application keeps running, while clumsy can start and stop anytime.
- Interactive control how bad the network can be, with enough visual feedback to tell you what's going on.

See [this page](http://jagt.github.io/clumsy) for more info and original build instructions.

## Fork Enhancements

This fork adds several advanced UI/UX and feature improvements:

### Dynamic Kernel-Level Process Filtering
- Target specific applications by name (e.g., `discord.exe`, `chrome.exe`, `roblox`) instead of guessing static port ranges.
- Intercepts connection mappings dynamically to match network sockets to running processes.
- Automatically handles mid-session connections and cleanly releases resources on stop/close.

### Session Timer (Duration Limit)
- Restrict filtering sessions by configuring an execution duration in milliseconds.
- Automatically stops network degradation when the timer expires.

### Global Hotkey Toggle
- Configure a hotkey in `config.yaml` (e.g., `hotkey: f6` or `hotkey: ctrl+shift+c`).
- Hotkey displayed next to the Start button.
- Press the hotkey anytime to toggle filtering on/off.

### State Persistence & Robust Configs
- Config file upgraded to structured `config.yaml`.
- Automatically generates a default, valid `config.yaml` if missing to prevent boot failures.
- Filter text, target process, duration timer, and module settings are saved to `state.txt` on exit.
- Restores active settings on launch (without auto-starting).

### Visual Layout & Sizing Safeguards
- Restructured layout with dedicated side-by-side frames for "Process Filter" and "Session Timer".
- Programmatically enforces a minimum window size using computed natural dimensions (`RASTERSIZE`), preventing label clipping and text wrapping issues on any screen DPI.
- Fully detached console subsystem for clean windowed execution.

### Improved UI Feedback & Statistics
- Window title shows `(running)` when filtering is active.
- Real-time packet counters show exactly how many packets were affected per module.
- Tooltips on all modules and controls, including a WinDivert filter syntax guide.

## Details

Simulate network latency, delay, packet loss with clumsy on Windows 7/8/10:

![](clumsy-demo.gif)

## License

MIT
