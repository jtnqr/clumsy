# clumsy

**clumsy makes your network condition on Windows significantly worse, but in a managed and interactive manner.**

Leveraging the awesome [WinDivert](http://reqrypt.org/windivert.html), clumsy intercepts live network packets, performs lag/drop/tamper/throttle operations on demand, and forwards them. Whether you want to debug connection edge-cases or test application resilience, clumsy is designed to be simple and lightweight:

- **No installation required**: run the executable directly without system installation.
- **No proxy configuration**: works without proxy setup or application code modifications.
- **System-wide capture**: intercepts network packets system-wide across all active applications.
- **Offline loopback support**: works even when offline (e.g., localhost/loopback connections).
- **Non-disruptive toggling**: keep target applications running while starting or stopping degradation.
- **Interactive controls**: configure network degradation dynamically with real-time feedback.

See the [clumsy homepage](http://jagt.github.io/clumsy) for more information and the original build instructions.

## Fork enhancements

This fork introduces key features, UI/UX refinements, and stability improvements:

### Dynamic kernel-level process filtering
- **Process targeting**: filter network traffic for specific applications by name (e.g., `discord.exe`, `chrome.exe`, `roblox`) instead of guessing static port ranges.
- **Dynamic socket mapping**: tracks connection mappings dynamically to match network sockets to active processes.
- **Safe cleanups**: automatically handles mid-session connections and releases system resources on stop or exit.

### Session timer (duration limit)
- **Execution limits**: restrict packet filtering sessions to a configured duration in milliseconds.
- **Auto-stop safety**: automatically stops network degradation when the duration timer expires.

### Global hotkey toggle
- **Custom shortcut**: configure a global hotkey in `config.yaml` (e.g., `hotkey: f6` or `hotkey: ctrl+shift+c`).
- **Visual shortcut guide**: displays the active hotkey next to the Start button in the UI.
- **One-click toggle**: toggle packet interceptors on or off globally at any time using the hotkey.

### State persistence, presets, and configuration
- **YAML format**: configuration file upgraded to a structured, documented `config.yaml`.
- **Default fallback**: automatically generates a default, valid `config.yaml` if missing on boot.
- **Preset editor**: save custom configurations or delete existing ones directly from the UI.
- **Dynamic `<custom>` selection**: automatically switches the dropdown selection to `<custom>` when settings are tweaked.
- **Legacy migration**: automatically parses and upgrades old `config.txt` configurations to `config.yaml` on startup.
- **State saving**: filter text, target process, duration timer, and module settings are saved to `state.txt` on exit.
- **Restore state**: restores active settings on launch without auto-starting.

### Visual layout and sizing safeguards
- **Side-by-side frames**: restructured UI layout to place Process Filter and Session Timer settings side-by-side.
- **DPI and layout safety**: dynamically calculates minimum window size based on natural element dimensions (`RASTERSIZE`) to prevent clipping and text wrapping across different screen DPIs.
- **Detached console**: fully detached console subsystem to run clumsy cleanly as a windowed application.

### Improved UI feedback and statistics
- **Active state title indicator**: window title displays `(running)` when packet filtering is active.
- **Per-module packet statistics**: real-time counters display the number of packets affected by each module.
- **Context tooltips**: comprehensive tooltips on all modules and controls, including a guide for WinDivert filter syntax.

## Details

Simulate network latency, delay, packet loss with clumsy on Windows:

![](clumsy-demo.gif)

## License

This project is licensed under the [MIT License](LICENSE).
