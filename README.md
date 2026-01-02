# radi8c2 - TUI Chat Client for radi8d

A terminal user interface (TUI) chat client for the radi8d server with SSL/TLS support, featuring a clean three-pane layout with ANSI colors and URL highlighting.

## Features

- **Three-Pane Layout**
  - Left pane: Channel list with unread message counts
  - Center pane: Chat messages with topic display
  - Right pane: User list for active channel

- **ANSI Color Support** - Automatic color assignment for usernames based on terminal capabilities
- **URL Detection** - Links are automatically underlined in messages
- **SSL/TLS Support** - Connect securely to SSL-enabled radi8d servers
- **Multi-Channel** - Join and switch between multiple channels
- **Emote Support** - Send /me actions with italic formatting
- **Real-time Updates** - Threaded message receiving for responsive UI

## Requirements

- C++17 compiler (g++ or clang++)
- CMake 3.11 or higher
- OpenSSL 3.x (for SSL support)
- FTXUI (included as submodule)
- POSIX-compliant operating system (Linux, macOS, BSD)

### macOS Installation
```bash
brew install cmake openssl@3
```

### Linux Installation
```bash
# Debian/Ubuntu
sudo apt-get install cmake libssl-dev build-essential

# Fedora/RHEL
sudo dnf install cmake openssl-devel gcc-c++
```

## Building

```bash
# Clone with submodules
git submodule update --init --recursive

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .

# Run
./radi8c2
```

To clean the build:
```bash
rm -rf build
```

## Usage

Start the client:
```bash
./radi8c2
```

You will be presented with a login dialog where you can enter:
- **Host**: Server hostname (default: localhost)
- **Port**: Server port (default: 1337)
- **SSL**: Enable SSL (y/n, default: n)
- **Username**: Your username
- **Password**: Optional password

Use `Tab` to move between fields, `Enter` to connect, or `Ctrl+C` to quit.

## Commands

Once connected, you can use the following commands:

| Command | Description |
|---------|-------------|
| `/join <channel>` or `/j <channel>` | Join a channel |
| `/leave` or `/part` or `/l` | Leave current channel |
| `/me <action>` | Send an emote/action |
| `/topic [new_topic]` | View or set channel topic |
| `/list` | Request channel list |
| `/help` or `/h` | Show help message |
| `/quit` or `/exit` or `/q` | Disconnect and quit |

Regular text messages are sent directly to the active channel.

## Interface

```
┌─ Channels ──────┐┌─ #general - Welcome Channel ───────────────────┐┌─ Users ─────┐
│ #general (2)    ││ alice: Hey everyone!                            ││ alice       │
│ #random         ││ bob: Hi alice, how's it going?                  ││ bob         │
│ #offtopic       ││ charlie: Check out https://example.com          ││ charlie     │
│                 ││ * alice waves                                   ││ david       │
│                 ││ [SERVER] david joined the channel               ││             │
│                 ││                                                 ││             │
└─────────────────┘└─────────────────────────────────────────────────┘└─────────────┘
┌─────────────────────────────────────────────────────────────────────────────────────┐
│ > Hello world!                                                                      │
└─────────────────────────────────────────────────────────────────────────────────────┘
 Connected as alice
```

## Color Scheme

The client automatically uses ANSI colors if your terminal supports them:

- **Usernames**: Colored and bold (colors assigned per user)
- **System Messages**: Red
- **Emotes**: Italic formatting
- **URLs**: Underlined
- **Active Channel**: Highlighted in reverse video

## Keyboard Controls

- `Enter`: Send message
- `Backspace`: Delete character
- `Ctrl+C`: Quit application
- Regular typing: Compose message

## Connecting to radi8d Server

### Non-SSL Connection
```
Host: localhost
Port: 1337
SSL:  n
Username: alice
Password: 
```

### SSL Connection
```
Host: secure.example.com
Port: 1337
SSL:  y
Username: alice
Password: mypassword
```

The client accepts self-signed certificates automatically for convenience.

## Protocol Support

radi8c2 implements the radi8d protocol including:

- User authentication (`!name`)
- Channel operations (`!jnchn`, `!lvchn`)
- Message sending (`!msg`)
- Emotes (`!emote`)
- Channel listing (`!chanlist`)
- User listing (`!userlist`)
- Topic management (`!topic`, `!settopic`)
- MOTD display (`!motd`)

## Development

### Project Structure
```
radi8c2/
├── src/
│   ├── main.cpp        # Main entry point and event loop
│   ├── Connection.cpp  # Network connection handling (SSL/non-SSL)
│   ├── Protocol.cpp    # radi8d protocol implementation
│   └── TUI.cpp         # Terminal UI rendering and input
├── include/
│   ├── Connection.h
│   ├── Protocol.h
│   └── TUI.h
├── Makefile
└── README.md
```

### Building for Debug
```bash
make clean
make CXXFLAGS="-std=c++11 -Wall -Wextra -Iinclude -I/opt/homebrew/opt/openssl@3/include -pthread -g -DDEBUG"
```

## Troubleshooting

### Cannot Connect
- Verify the radi8d server is running
- Check hostname and port are correct
- If using SSL, ensure the server has SSL enabled

### Terminal Colors Not Working
The client automatically detects color support. If colors aren't showing:
- Check your `TERM` environment variable: `echo $TERM`
- Try setting it to a color-capable terminal: `export TERM=xterm-256color`

### Build Errors
**OpenSSL not found:**
```bash
# Update OPENSSL_PATH in Makefile or install OpenSSL
brew install openssl@3  # macOS
```

**ncurses not found:**
```bash
brew install ncurses  # macOS
sudo apt-get install libncurses-dev  # Debian/Ubuntu
```

### Garbled Display
If the display appears corrupted:
- Try resizing your terminal window
- Ensure terminal is at least 80x24 characters
- Restart the client

## Known Limitations

- No scrollback history (shows last N messages that fit in window)
- No private messaging UI (can be implemented via protocol)
- No file transfer support
- Single-line input only (no multi-line message composition)

## License

GPL License - For non-commercial use only.

Copyright 2026 David Estes

Based on the radi8d server protocol.

## Author

David Estes (Chrono)

Client implementation with TUI by Warp Agent.

## See Also

- [radi8d Server](https://github.com/davydotcom/radi8d) - The radi8d server this client connects to
