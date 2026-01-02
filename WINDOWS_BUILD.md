# Windows Build Guide for radi8c2

This guide provides step-by-step instructions for building radi8c2 on Windows using PowerShell.

## Prerequisites

### 1. Install Visual Studio Build Tools

You need a C++ compiler. Choose one:

**Option A: Visual Studio 2019/2022 (Recommended)**
- Download from: https://visualstudio.microsoft.com/downloads/
- Install "Desktop development with C++" workload
- This includes MSVC compiler and Windows SDK

**Option B: MinGW-w64**
- Download from: https://www.mingw-w64.org/
- Or install via MSYS2: https://www.msys2.org/

### 2. Install CMake

Download and install CMake from: https://cmake.org/download/

During installation, select "Add CMake to system PATH"

Verify installation:
```powershell
cmake --version
```

### 3. Install Git

Download from: https://git-scm.com/download/win

Make sure Git is in your PATH.

### 4. Install OpenSSL

**Option A: Using vcpkg (Recommended)**

vcpkg is a package manager for C++ libraries on Windows.

```powershell
# Clone vcpkg
cd C:\
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg

# Bootstrap vcpkg
.\bootstrap-vcpkg.bat

# Install OpenSSL
.\vcpkg install openssl:x64-windows

# Integrate with Visual Studio (makes libraries available globally)
.\vcpkg integrate install
```

**Option B: Pre-built binaries**

Download OpenSSL installer from: https://slproweb.com/products/Win32OpenSSL.html

- Choose "Win64 OpenSSL v3.x.x" (not the "Light" version)
- Install to default location: `C:\Program Files\OpenSSL-Win64`

## Building the Project

### Step 1: Clone the Repository

```powershell
cd C:\Users\<YourUsername>\projects\C++
git clone https://github.com/yourusername/radi8c.git
cd radi8c
```

### Step 2: Initialize Submodules

FTXUI is included as a Git submodule:

```powershell
git submodule update --init --recursive
```

### Step 3: Configure CMake

**If using vcpkg:**

```powershell
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

**If using manually installed OpenSSL:**

```powershell
mkdir build
cd build
cmake .. -DOPENSSL_ROOT_DIR="C:/Program Files/OpenSSL-Win64"
```

### Step 4: Build the Project

```powershell
cmake --build . --config Release
```

This will create the executable at: `build\Release\radi8c2.exe`

For a debug build:
```powershell
cmake --build . --config Debug
```

## Running the Application

From the build directory:

```powershell
.\Release\radi8c2.exe
```

Or specify the full path:

```powershell
C:\Users\<YourUsername>\projects\C++\radi8c\build\Release\radi8c2.exe
```

## Troubleshooting

### OpenSSL Not Found

**Error:** `Could NOT find OpenSSL`

**Solution:**
- If using vcpkg, make sure you ran `vcpkg integrate install`
- If using manual install, verify the path: `dir "C:\Program Files\OpenSSL-Win64"`
- Try specifying the path explicitly:
  ```powershell
  cmake .. -DOPENSSL_ROOT_DIR="C:/Program Files/OpenSSL-Win64"
  ```

### FTXUI Submodule Missing

**Error:** `Could not find a package configuration file provided by FTXUI`

**Solution:**
```powershell
git submodule update --init --recursive
```

### Winsock Errors

If you get linker errors about `WSAStartup` or other socket functions, make sure `ws2_32.lib` is being linked. This should be automatic with the updated `CMakeLists.txt`.

### Visual Studio Not Found

If CMake can't find Visual Studio:

```powershell
# List available generators
cmake --help

# Specify generator explicitly
cmake .. -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

### Runtime DLL Missing

If you get an error about missing DLLs when running the app:

**For vcpkg:** Copy DLLs from vcpkg installed directory:
```powershell
copy C:\vcpkg\installed\x64-windows\bin\*.dll .\Release\
```

**For manual OpenSSL:** Add OpenSSL to PATH or copy DLLs:
```powershell
copy "C:\Program Files\OpenSSL-Win64\bin\*.dll" .\Release\
```

## Clean Build

To clean and rebuild from scratch:

```powershell
cd C:\Users\<YourUsername>\projects\C++\radi8c
Remove-Item -Recurse -Force build
mkdir build
cd build
# ... then run cmake commands again
```

## Development Tips

### Using Visual Studio IDE

You can open the project in Visual Studio:

```powershell
cd build
start radi8c2.sln
```

### Using VS Code

Install the "C/C++" and "CMake Tools" extensions, then:
1. Open the radi8c folder in VS Code
2. Select a kit (Visual Studio compiler)
3. Configure and build using the CMake extension

## Platform-Specific Notes

### Windows Terminal vs PowerShell

For the best TUI experience, use Windows Terminal:
- Download from Microsoft Store or: https://github.com/microsoft/terminal
- Better ANSI color support than legacy cmd.exe or PowerShell

### Firewall

Windows Firewall may prompt you when the application tries to make network connections. Allow the application through the firewall.

### OpenSSL Certificate Store

Windows uses its own certificate store. The application accepts self-signed certificates by default (SSL_VERIFY_NONE), so this shouldn't be an issue for most use cases.

## Next Steps

Once built successfully:
1. Read the main [README.md](README.md) for usage instructions
2. Configure your connection settings in the TUI login dialog
3. Connect to your radi8d server

## Support

For issues specific to Windows builds, check:
- CMake output for error messages
- Visual Studio build output
- Windows Event Viewer for runtime crashes
