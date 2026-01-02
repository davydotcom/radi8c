# Quick Start Guide

## Testing the Client

### 1. Start the radi8d Server

In a separate terminal, start the radi8d server:

```bash
cd /Users/davydotcom/projects/C++/radi8d2
./radi8d -p 1337 --debug
```

Or with SSL:
```bash
./radi8d -p 1337 --ssl --debug
```

### 2. Start the Client

```bash
cd /Users/davydotcom/projects/C++/radi8c2
./radi8c2
```

### 3. Login

When the login dialog appears, enter:

**For non-SSL connection:**
- Host: `localhost`
- Port: `1337`
- SSL: `n`
- Username: `testuser` (or any name you want)
- Password: (leave empty or enter if server requires)

**For SSL connection:**
- Host: `localhost`
- Port: `1337`
- SSL: `y`
- Username: `testuser`
- Password: (leave empty)

Press `Tab` to move between fields, `Enter` to connect.

### 4. Basic Operations

Once connected:

1. **Join a channel:**
   ```
   /join test
   ```

2. **Send a message:**
   ```
   Hello, world!
   ```

3. **Send an emote:**
   ```
   /me waves to everyone
   ```

4. **Set topic:**
   ```
   /topic Welcome to the test channel!
   ```

5. **List channels:**
   ```
   /list
   ```

6. **View help:**
   ```
   /help
   ```

7. **Test URL detection (URLs will be underlined):**
   ```
   Check out https://github.com/davydotcom/radi8d
   ```

8. **Leave and quit:**
   ```
   /leave
   /quit
   ```

## Testing with Multiple Clients

Open multiple terminals and connect with different usernames to test:
- Multi-user chat
- Join/leave notifications
- Username colors
- User list updates

## Features to Test

- ✅ Connection (SSL and non-SSL)
- ✅ Authentication
- ✅ Joining channels
- ✅ Sending messages
- ✅ Receiving messages from other users
- ✅ Username color highlighting
- ✅ URL underlining
- ✅ Emotes (/me)
- ✅ Topic display and setting
- ✅ User list display
- ✅ Multiple channels
- ✅ Unread message counts
- ✅ System messages (joins/leaves)
- ✅ Channel switching (use /join to switch)
- ✅ Error handling

## Keyboard Reference

| Key | Action |
|-----|--------|
| `Tab` | (Login dialog) Move to next field |
| `Enter` | (Login) Connect / (Chat) Send message |
| `Backspace` | Delete character |
| `Ctrl+C` | Quit application |
| `/` | Start a command |

## Troubleshooting

### Client won't connect
- Make sure the server is running
- Check the host and port are correct
- Verify firewall settings

### Colors not showing
- Check your terminal supports colors: `echo $TERM`
- Try: `export TERM=xterm-256color`

### Display is garbled
- Ensure terminal is at least 80x24
- Try resizing the window
- Restart the client

## Advanced Testing

### Test SSL with Self-Signed Certificate

Start server with SSL:
```bash
cd /Users/davydotcom/projects/C++/radi8d2
./radi8d -p 1337 --ssl
```

Connect client with SSL:
```
Host: localhost
Port: 1337
SSL:  y
```

The client will automatically accept the self-signed certificate.

### Test Multiple Channels

```
/join general
/join random
/join offtopic
```

Switch between them with `/join <channel>` and watch the unread counts update.

## Known Issues

- No scrollback - only visible messages are stored
- Single-line input - no multiline message composition
- Terminal resize may require restart

## Next Steps

- Connect to a remote radi8d server
- Set up channel passwords
- Explore more protocol features
- Customize the color scheme (edit TUI.cpp)
