# Qt Terminal with SSH Connection Manager

A modern Qt-based terminal emulator with built-in SSH connection management, inspired by mRemoteNG but focused on terminal connections.

![Qt Terminal Screenshot](screenshot.png)

## Features

### ✅ Implemented Features
- **🖥️ Multi-tab terminal interface** with QTermWidget
- **🌳 SSH Connection Tree** - Organized sidebar with folder structure and emoji icons
- **📁 Connection Management** - Add, edit, delete SSH connections with visual dialogs
- **💾 Persistent Storage** - Connections saved to JSON file automatically
- **🎨 Visual Organization** - Emoji icons for different server types and folders
- **📋 Context Menus** - Right-click menus for terminals and connections
- **⌨️ Keyboard Shortcuts** - Standard shortcuts for copy/paste, new tabs, etc.
- **🔤 Font Customization** - Change font family and size (Ctrl++ / Ctrl+-)
- **🎨 Color Schemes** - Multiple terminal color themes
- **💡 Connection Testing** - Test host connectivity before connecting
- **📊 Status Bar** - Shows font info, tab count, and connection count
- **🔍 Smart Defaults** - Intelligent folder suggestions and connection validation

### 🚧 Planned Features
- **🔐 SSH Key Management** - Integrate SSH key authentication  
- **🚀 One-click SSH Connections** - Currently shows SSH command (Feature 4)
- **📈 Connection History** - Track recent connections
- **🔍 Quick Connect Toolbar** - Fast SSH connections with search
- **🎯 Connection Import/Export** - Share connection configurations
- **🔒 Encrypted Storage** - Secure password storage options

## Connection Organization

The application organizes connections into folders with distinctive emoji icons:

- **🏢 Production** - Live servers and production environments
- **🔧 Development** - Development and staging servers  
- **👤 Personal** - Personal VPS and cloud instances
- **🧪 Testing** - Test servers and QA environments
- **🚀 Staging** - Pre-production staging environments
- **📁 Custom** - User-defined folder names

Connection types are automatically detected and iconized:
- **🖥️ Web Servers** - Web, WWW, HTTP servers
- **🗄️ Databases** - Database, DB, SQL servers  
- **💻 Development** - Dev boxes and development servers
- **🧪 Test Servers** - Testing and QA servers
- **☁️ Cloud/VPS** - Cloud instances and VPS servers

## Requirements

### System Requirements
- **Qt 5.12+** or **Qt 6.x**
- **qtermwidget5** library
- **Linux/Unix** system (tested on Ubuntu/Debian)
- **C++11** compatible compiler
- **CMake 3.10+** or **qmake**

### Runtime Dependencies  
- **OpenSSH client** (for SSH connections)
- **ping utility** (for connection testing)

## Installation

### Install Dependencies

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install qtbase5-dev qtermwidget5-dev build-essential cmake openssh-client
```

**Fedora/RHEL:**
```bash
sudo dnf install qt5-qtbase-devel qtermwidget-qt5-devel gcc-c++ make cmake openssh-clients
```

**Arch Linux:**
```bash
sudo pacman -S qt5-base qtermwidget cmake openssh
```

### Build with CMake
```bash
git clone https://github.com/yourusername/QtTerminalExample.git
cd QtTerminalExample
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Build with qmake (alternative)
```bash
git clone https://github.com/yourusername/QtTerminalExample.git
cd QtTerminalExample
qmake
make -j$(nproc)
```

### Run
```bash
./QtTerminalExample
```

## Usage Guide

### Terminal Operations
- **New Tab**: `Ctrl+T` or File → New Tab
- **Close Tab**: `Ctrl+W` or click the × on tab
- **Copy**: `Ctrl+C` (in terminal)  
- **Paste**: `Ctrl+V` (in terminal)
- **Select All**: `Ctrl+A`
- **Font Size**: `Ctrl++` / `Ctrl+-` or View menu
- **Clear Terminal**: Right-click → Clear

### Connection Management
- **New Connection**: `Ctrl+N` or Connections → New Connection
- **Edit Connection**: Right-click connection → Edit
- **Delete Connection**: Right-click connection → Delete  
- **Test Connection**: Use "Test Connection" button in connection dialog
- **Connect**: Double-click any connection (shows SSH command currently)
- **Organize**: Drag connections to different folders
- **Refresh**: `F5` or right-click → Refresh

### Connection Folders
- Right-click on folders to add connections directly to them
- Folders auto-expand to show connections
- Create custom folders by typing new names in the connection dialog

## Configuration Files

### Connection Storage
- **Location**: `~/.config/QtTerminalExample/connections.json`
- **Format**: JSON with connection details and folder organization
- **Backup**: Automatically creates defaults if file is missing or corrupted

### Application Settings  
- **Qt Settings**: Window geometry, fonts, and preferences
- **Auto-save**: Settings saved on application exit

## Project Structure

```
QtTerminalExample/
├── main.cpp                 # Application entry point with X11 platform setup
├── terminalwindow.h         # Main window header with SSH connection structure  
├── terminalwindow.cpp       # Main window implementation with full feature set
├── connectiondialog.h       # Connection add/edit dialog header
├── connectiondialog.cpp     # Connection dialog with validation and testing
├── CMakeLists.txt           # CMake build configuration
├── QtTerminalExample.pro    # qmake project file (alternative build)
└── README.md               # This documentation
```

## Keyboard Shortcuts

| Action | Shortcut | Menu Location |
|--------|----------|---------------|
| New Tab | `Ctrl+T` | File → New Tab |
| Close Tab | `Ctrl+W` | File → Close Tab |
| New Connection | `Ctrl+N` | Connections → New Connection |
| Copy | `Ctrl+C` | Edit → Copy |
| Paste | `Ctrl+V` | Edit → Paste |
| Select All | `Ctrl+A` | Edit → Select All |
| Zoom In | `Ctrl++` | View → Zoom In |
| Zoom Out | `Ctrl+-` | View → Zoom Out |
| Reset Zoom | `Ctrl+0` | View → Reset Zoom |
| Refresh | `F5` | Connections → Refresh |
| Quit | `Ctrl+Q` | File → Quit |

## Development Status

This project follows an incremental development approach:

1. **✅ Feature 1**: Basic terminal with connection tree (COMPLETE)
2. **✅ Feature 2**: JSON connection storage and persistence (COMPLETE)  
3. **✅ Feature 3**: Add/Edit/Delete connection dialogs with validation (COMPLETE)
4. **🚧 Feature 4**: SSH connection implementation (IN PROGRESS)
5. **📋 Feature 5**: Advanced connection management and import/export (PLANNED)
6. **📋 Feature 6**: SSH key management and security features (PLANNED)

### Current State (v0.3)
The application is fully functional for connection management with a polished UI. SSH connections currently display the command that would be executed - actual SSH execution is the next major milestone.

## Screenshots

### Main Interface
- Clean split-pane design with connection tree (left) and terminals (right)
- Professional emoji-based organization system
- Multi-tab terminal interface with close buttons
- Context-sensitive menus throughout

### Connection Management
- Intuitive add/edit dialogs with form validation
- Connection testing with progress indicators  
- Smart folder management with predefined categories
- Visual feedback for all operations

## Troubleshooting

### Common Issues

**"qtermwidget not found"**
```bash
# Ubuntu/Debian
sudo apt install qtermwidget5-dev

# Find qtermwidget location
find /usr -name "qtermwidget.h" 2>/dev/null
```

**"X11 platform issues"**
- The app forces X11 platform to avoid Wayland terminal issues
- If you experience display problems, try: `export QT_QPA_PLATFORM=xcb`

**"Connections not saving"**
- Check permissions: `ls -la ~/.config/QtTerminalExample/`
- Manually create directory: `mkdir -p ~/.config/QtTerminalExample/`

**"SSH connections not working"**
- SSH execution is not yet implemented (Feature 4)
- Currently shows the SSH command that would be executed
- Use the displayed command in your terminal for now

### Development Setup
1. Fork the repository  
2. Install Qt development packages
3. Build with CMake or qmake
4. Test your changes with various connection types
5. Submit a pull request

### Code Style
- Follow Qt naming conventions
- Use 4-space indentation
- Comment complex terminal operations
- Include error handling for file operations

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- Built with [Qt Framework](https://www.qt.io/) for cross-platform GUI
- Uses [qtermwidget](https://github.com/lxqt/qtermwidget) for terminal emulation
- Inspired by [mRemoteNG](https://mremoteng.org/) for connection management UX
- Terminal design influenced by modern terminal emulators
- Thanks to the Qt and Linux terminal community for guidance

## Author

**QtTerminalExample** - A modern terminal with SSH connection management  
Created with ❤️ for the Linux terminal community

---

**Status**: Active development | **Version**: 0.3 | **Next Milestone**: SSH Connection Implementation

For questions, feature requests, or bug reports, please use the [GitHub Issues](../../issues) page.