# FTXUI Migration Complete! 🎉

The radi8c2 client has been successfully migrated from ncurses to FTXUI!

## What Changed

### Build System
- **Switched from Make to CMake** - Modern build system with better dependency management
- **Added FTXUI as git submodule** - Latest version from GitHub
- **Updated compiler requirements** - Now requires C++17

### UI Framework
- **Replaced ncurses with FTXUI** - Modern, component-based TUI framework
- **Declarative UI** - Layout defined functionally rather than imperatively
- **Better rendering** - Smoother updates and cleaner rendering
- **Proper event handling** - Component-based event system

### Features Retained
✅ Three-pane layout (channels, chat, users)
✅ Color-coded usernames
✅ URL underlining
✅ Channel management
✅ Message history per channel
✅ SSL/TLS support
✅ All protocol commands (/join, /leave, /me, etc.)
✅ Login dialog
✅ Error dialogs

### Features Improved
🎨 **Better visuals** - Cleaner borders and layout
⚡ **Faster rendering** - More efficient screen updates
🎯 **Better input handling** - Native FTXUI input component
🔧 **Easier to maintain** - Component-based architecture
📦 **Self-contained** - FTXUI bundled as submodule

## How to Build

```bash
# First time setup
git submodule update --init --recursive

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .

# Run
./radi8c2
```

## How to Run

From the project root:
```bash
./run.sh
```

Or directly:
```bash
cd build
./radi8c2
```

## Testing the New UI

1. **Start the server:**
   ```bash
   cd ../radi8d2
   ./radi8d -p 1337
   ```

2. **Run the client:**
   ```bash
   cd build
   ./radi8c2
   ```

3. **Login:**
   - Host: localhost
   - Port: 1337
   - SSL: n
   - Username: (your choice)
   - Password: (leave empty)

4. **Try commands:**
   - `/join test` - Join a channel
   - `Hello!` - Send a message
   - `/me waves` - Send an emote
   - `/quit` - Exit (or press Esc)

## Keyboard Shortcuts

- **Enter** - Send message/submit form
- **Esc** - Quit application
- **Ctrl+C** - Quit application
- **Tab** - Navigate between form fields (in login dialog)

## Architecture

### Old (ncurses)
```
main.cpp
  ├─ Manual event loop with getch()
  ├─ TUI.cpp with manual window management
  └─ Manual rendering every frame
```

### New (FTXUI)
```
main.cpp
  ├─ FTXUI event loop (screen.Loop())
  ├─ TUI.cpp with component composition
  └─ Automatic rendering on state change
```

## File Structure

```
radi8c2/
├── CMakeLists.txt          # CMake build configuration
├── external/
│   └── FTXUI/              # FTXUI library (submodule)
├── src/
│   ├── Connection.cpp      # SSL/TCP connection (unchanged)
│   ├── Protocol.cpp        # radi8d protocol (unchanged)
│   ├── TUI.cpp             # FTXUI-based UI (rewritten)
│   └── main.cpp            # Main loop (updated for FTXUI)
├── include/
│   ├── Connection.h
│   ├── Protocol.h
│   └── TUI.h               # Updated interface
└── build/
    └── radi8c2             # Compiled binary
```

## Migration Notes

### Backup Files
The original ncurses implementation has been backed up:
- `src/TUI_ncurses_backup.cpp` - Original TUI implementation
- `src/main_ncurses_backup.cpp` - Original main.cpp
- `Makefile` - Original Make build (still works with backups)

### Key API Differences

**ncurses:**
```cpp
WINDOW* win = newwin(height, width, y, x);
mvwprintw(win, y, x, "text");
wrefresh(win);
```

**FTXUI:**
```cpp
auto component = Renderer([] {
    return text("text") | border;
});
```

### Benefits of FTXUI

1. **Component Composition** - Build UI from reusable components
2. **Declarative Style** - Describe what you want, not how to draw it
3. **Automatic Layout** - Flexbox-style layout with `flex`, `size()`, etc.
4. **Modern C++** - Uses C++17 features, lambdas, smart pointers
5. **Active Development** - FTXUI is actively maintained
6. **Better Docs** - Excellent examples and documentation

## Known Issues

None currently! The migration is complete and functional.

## Future Enhancements

Possible improvements now that we're on FTXUI:

- Add scrollable message history
- Add clickable channel list
- Add keyboard shortcuts for channel switching
- Add tabs for multiple active channels
- Add customizable color themes
- Add message search
- Add user mentions with @ support

## Performance

FTXUI is more efficient than the manual ncurses implementation:
- Renders only when state changes (not every frame)
- Smart diffing reduces terminal updates
- Better memory management with C++ smart pointers

## Compatibility

Tested on:
- ✅ macOS (Apple Silicon)
- ⏹️ Linux (should work, not tested)
- ⏹️ BSD (should work, not tested)

## Credits

- **FTXUI** - https://github.com/ArthurSonzogni/FTXUI
- **radi8d protocol** - Original server by David Estes
- **Migration** - Warp AI Agent

---

**Status:** ✅ Complete and ready to use!
**Date:** January 1, 2026
