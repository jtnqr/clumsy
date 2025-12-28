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

This fork adds several UI/UX improvements:

### Global Hotkey Toggle

- Configure a hotkey in `config.txt` (e.g., `hotkey: f6` or `hotkey: ctrl+shift+c`)
- Hotkey displayed next to the Start button
- Press the hotkey anytime to toggle filtering on/off

### State Persistence

- Filter text and module settings are saved to `state.txt` on exit
- Automatically restored on next launch (without auto-starting)

### Improved UI Feedback

- Window title shows "(running)" when filtering is active
- Comprehensive tooltips on all modules and controls
- WinDivert filter syntax reference in filter text tooltip

### Packet Statistics

- Per-module counter showing how many packets were affected
- Counters update in real-time, persist after stopping for review
- Reset when a new filtering session starts

## Details

Simulate network latency, delay, packet loss with clumsy on Windows 7/8/10:

![](clumsy-demo.gif)

## License

MIT
