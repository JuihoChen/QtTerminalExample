# Qt Terminal with SSH Connection Manager

A modern Qt-based terminal emulator with built-in SSH connection management, inspired by mRemoteNG but focused on terminal connections.

![Screenshot](screenshot.png)

## Features

### ✅ Implemented (v0.1)
- **Multi-tab terminal interface** with QTermWidget
- **SSH Connection Tree** - Organized sidebar with folder structure
- **Visual organization** with emoji icons for different server types
- **Tabbed interface** supporting multiple terminal sessions
- **Font customization** - Change font family and size
- **Color schemes** - Multiple terminal color themes
- **Settings persistence** - Window geometry and preferences saved
- **Context menus** - Right-click menus for terminals and connections
- **Keyboard shortcuts** - Standard shortcuts for copy/paste, new tabs, etc.

### 🚧 Planned Features
- **Connection storage** - Save/load SSH connections from JSON
- **Add/Edit connection dialogs** - Manage your SSH connections
- **One-click SSH connections** - Double-click to connect
- **Connection folders** - Organize connections by project/environment
- **Quick connect toolbar** - Fast SSH connections with recent history
- **SSH key management** - Integrate SSH key authentication

## Requirements

- **Qt 5.12+** or **Qt 6.x**
- **qtermwidget** library
- **Linux/Unix** system (tested on Ubuntu/Debian)
- **C++11** compatible compiler

## Building

### Install Dependencies

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install qt5-default qtermwidget5-dev build-essential
```

**Fedora/RHEL:**
```bash
sudo dnf install qt5-qtbase-devel qtermwidget-qt5-devel gcc-c++
```

### Compile
```bash
git clone https://github.com/yourusername/QtTerminalExample.git
cd QtTerminalExample
qmake
make
```

### Run
```bash
./QtTerminalExample
```

## Usage

### Basic Terminal Operations
- **New Tab**: `Ctrl+T` or File → New Tab
- **Close Tab**: `Ctrl+W` or click the × on tab
- **Copy**: `Ctrl+C` (in terminal)
- **Paste**: `Ctrl+V` (in terminal)
- **Font Size**: `Ctrl++` / `Ctrl+-` or View menu

### SSH Connections (Coming Soon)
- Browse connections in the left sidebar
- Double-click any connection to open SSH session
- Right-click to manage connections
- Organize connections in folders (Production, Development, etc.)

## Project Structure

```
QtTerminalExample/
├── main.cpp              # Application entry point
├── terminalwindow.h      # Main window header
├── terminalwindow.cpp    # Main window implementation
├── QtTerminalExample.pro # Qt project file
└── README.md            # This file
```

## Development Roadmap

This project is being developed incrementally with these planned features:

1. **Feature 1**: ✅ Basic connection tree widget with dummy data
2. **Feature 2**: 🚧 Connection storage (JSON persistence)
3. **Feature 3**: 🚧 Add/Edit connection dialogs
4. **Feature 4**: 🚧 SSH connection implementation
5. **Feature 5**: 🚧 Connection folders and organization
6. **Feature 6**: 🚧 Quick connect toolbar

## Screenshots

### Current Interface (v0.1)
The application features a clean, modern interface with:
- Connection tree on the left with organized folders
- Multi-tab terminal interface on the right
- Professional styling with emoji icons
- Context menus and keyboard shortcuts

## Contributing

This is a learning project, but contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add some amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- Built with [Qt Framework](https://www.qt.io/)
- Uses [qtermwidget](https://github.com/lxqt/qtermwidget) for terminal emulation
- Inspired by [mRemoteNG](https://mremoteng.org/) for connection management UI
- Thanks to the Qt and Linux terminal community

## Author

Created by [Claude] - feel free to reach out with questions or suggestions!

---

**Note**: This is an active development project. Features are being added incrementally. Check the [Issues](../../issues) page for planned features and known bugs.