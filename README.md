# Qt Terminal with SSH Connection Manager

A modern Qt-based terminal emulator with built-in SSH connection management, inspired by mRemoteNG but focused on terminal connections.

![Qt Terminal Screenshot](screenshot.png)

## Features

### ✅ Implemented Features
- **🖥️ Multi-tab terminal interface** with QTermWidget
- **🌳 SSH Connection Tree** - Organized sidebar with folder structure and emoji icons
- **📁 Connection Management** - Add, edit, delete SSH connections with visual dialogs
- **🔑 Password Support** - Store SSH passwords with show/hide toggle (optional)
- **💾 Persistent Storage** - Connections saved to JSON file automatically
- **🎨 Visual Organization** - Emoji icons for different server types and folders
- **📋 Context Menus** - Right-click menus for terminals and connections
- **⌨️ Keyboard Shortcuts** - Standard shortcuts for copy/paste, new tabs, etc.
- **🔤 Font Customization** - Change font family and size (Ctrl++ / Ctrl+-)
- **🎨 Color Schemes** - Multiple terminal color themes
- **💡 Connection Testing** - Test host connectivity before connecting
- **📊 Status Bar** - Shows font info, tab count, and connection count
- **🔍 Smart Defaults** - Intelligent folder suggestions and connection validation
- **🚀 Automatic SSH Connections** - One-click SSH with automatic password authentication

### 🚧 Planned Features
- **🔐 SSH Key Management** - Integrate SSH key authentication  
- **📈 Connection History** - Track recent connections
- **🔍 Quick Connect Toolbar** - Fast SSH connections with search
- **🎯 Connection Import/Export** - Share connection configurations
- **🔒 Encrypted Storage** - Secure password storage options

## Password Authentication

The application now supports password-based SSH authentication:

### Password Storage
- **Optional Field**: Password storage is completely optional
- **Plain Text Warning**: Passwords are currently stored in plain text in the JSON file
- **Show/Hide Toggle**: Connection dialog includes a checkbox to show/hide password while typing
- **Automatic Authentication**: When password is provided, connections use `sshpass` for automatic login

### Security Considerations
- **Local Storage**: Passwords are stored locally in `~/.config/QtTerminalExample/connections.json`
- **File Permissions**: Ensure your config directory has appropriate permissions (600/700)
- **Production Use**: For production environments, consider using SSH keys instead of passwords
- **Future Enhancement**: Encrypted storage is planned for future versions

### SSH Host Key Management
The application automatically handles SSH host key verification for new connections:

**Automatic Host Key Acceptance:**
- Uses `-o StrictHostKeyChecking=accept-new` SSH option
- New host keys are automatically accepted and added to `~/.ssh/known_hosts`
- Prevents connection hanging on first-time connections
- Changed host keys will still prompt for verification (security feature)

**Manual Host Key Verification (for higher security):**
```bash
# Pre-verify host keys before connecting
ssh-keyscan -H hostname >> ~/.ssh/known_hosts

# Or connect manually first to verify
ssh user@hostname
```

**Security Note:** The automatic acceptance is convenient but less secure than manual verification. For production environments, consider pre-populating known_hosts with verified keys.

**Install sshpass:**
```bash
# Ubuntu/Debian
sudo apt install sshpass

# Fedora/RHEL
sudo dnf install sshpass

# Arch Linux
sudo pacman -S sshpass
```

**Without sshpass**: If `sshpass` is not installed, the application will connect normally and prompt for password manually.

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
- **sshpass** (optional, for automatic password authentication)
- **ping utility** (for connection testing)

## Installation

### Install Dependencies

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install qtbase5-dev qtermwidget5-dev build-essential cmake openssh-client sshpass
```

**Fedora/RHEL:**
```bash
sudo dnf install qt5-qtbase-devel qtermwidget-qt5-devel gcc-c++ make cmake openssh-clients sshpass
```

**Arch Linux:**
```bash
sudo pacman -S qt5-base qtermwidget cmake openssh sshpass
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
- **Connect**: Double-click any connection for automatic SSH connection
- **Organize**: Drag connections to different folders
- **Refresh**: `F5` or right-click → Refresh

### Password Management
- **Add Password**: In connection dialog, enter password in the password field
- **Show/Hide**: Use the "Show password" checkbox to toggle visibility
- **Optional**: Leave password field empty to use key-based or manual authentication
- **Edit Password**: Edit connection and modify password field
- **Security**: Connection tooltips show "(password saved)" indicator

### Connection Folders
- Right-click on folders to add connections directly to them
- Folders auto-expand to show connections
- Create custom folders by typing new names in the connection dialog

## Configuration Files

### Connection Storage
- **Location**: `~/.config/QtTerminalExample/connections.json`
- **Format**: JSON with connection details, passwords, and folder organization
- **Backup**: Automatically creates defaults if file is missing or corrupted
- **Security**: Contains passwords in plain text - secure your config directory

### Application Settings  
- **Qt Settings**: Window geometry, fonts, and preferences
- **Auto-save**: Settings saved on application exit

## Project Structure

```
QtTerminalExample/
├── main.cpp                 # Application entry point with X11 platform setup
├── terminalwindow.h         # Main window header with SSH connection structure  
├── terminalwindow.cpp       # Main window implementation with password support
├── connectiondialog.h       # Connection add/edit dialog header with password field
├── connectiondialog.cpp     # Connection dialog with password input and validation
├── CMakeLists.txt           # CMake build configuration
├── QtTerminalExample.pro    # qmake project file (alternative build)
└── README.md               # This documentation
```

## Security Best Practices

### Password Security
1. **File Permissions**: Secure your config directory:
   ```bash
   chmod 700 ~/.config/QtTerminalExample
   chmod 600 ~/.config/QtTerminalExample/connections.json
   ```

2. **SSH Keys Preferred**: For production use, consider SSH key authentication:
   ```bash
   ssh-keygen -t rsa -b 4096
   ssh-copy-id user@hostname
   ```

3. **Backup Security**: If backing up connections.json, ensure secure storage

4. **Shared Systems**: Avoid storing passwords on shared or public systems

### Connection Security
- Use strong, unique passwords for each server
- Regularly rotate passwords
- Monitor SSH logs for unauthorized access
- Use fail2ban or similar tools on servers

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
4. **✅ Feature 4**: SSH connection implementation with password support (COMPLETE)
5. **📋 Feature 5**: Advanced connection management and import/export (PLANNED)
6. **📋 Feature 6**: SSH key management and security features (PLANNED)

### Current State (v0.4)
The application is fully functional for SSH connection management with password support. All core features are implemented and working, including automatic SSH connections with saved passwords.

## Screenshots

### Main Interface
- Clean split-pane design with connection tree (left) and terminals (right)
- Professional emoji-based organization system
- Multi-tab terminal interface with close buttons
- Context-sensitive menus throughout

### Connection Management
- Intuitive add/edit dialogs with password field and show/hide toggle
- Connection testing with progress indicators  
- Smart folder management with predefined categories
- Visual feedback for all operations
- Password indicators in connection tooltips

## Troubleshooting

### Common Issues

**"qtermwidget not found"**
```bash
# Ubuntu/Debian
sudo apt install qtermwidget5-dev

# Find qtermwidget location
find /usr -name "qtermwidget.h" 2>/dev/null
```

**"sshpass not found" warning**
- Install sshpass: `sudo apt install sshpass`
- Without sshpass, you'll need to enter passwords manually
- The application will show a warning and proceed with standard SSH

**"X11 platform issues"**
- The app forces X11 platform to avoid Wayland terminal issues
- If you experience display problems, try: `export QT_QPA_PLATFORM=xcb`

**"Connections not saving"**
- Check permissions: `ls -la ~/.config/QtTerminalExample/`
- Manually create directory: `mkdir -p ~/.config/QtTerminalExample/`
- Ensure write permissions: `chmod 700 ~/.config/QtTerminalExample/`

**"Password not working"**
- Verify sshpass is installed: `which sshpass`
- Check if server allows password authentication
- Try connecting manually first: `ssh user@host`
- Some servers require key-based authentication only

**"SSH connection hangs on first connection"**
- This happens when SSH encounters an unknown host key
- The application uses `-o StrictHostKeyChecking=accept-new` to handle this
- New host keys are automatically accepted and added to known_hosts
- For higher security, you can manually verify host keys first:
  ```bash
  ssh-keyscan -H hostname >> ~/.ssh/known_hosts
  ```

### Password-related Issues

**"Password visible in process list"**
- This is a known limitation of sshpass
- For sensitive environments, use SSH keys instead
- Consider this when using on shared systems

**"Connection fails with correct password"**
- Server might have disabled password authentication
- Check server SSH config: `PasswordAuthentication yes`
- Try manual connection to verify credentials
- Some servers require specific SSH client options

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
- Secure password handling practices

## Feature Comparison

| Feature | Status | Description |
|---------|--------|-------------|
| Multi-tab Terminal | ✅ Complete | Full qtermwidget integration |
| Connection Tree | ✅ Complete | Folder organization with emoji icons |
| SSH Connections | ✅ Complete | One-click SSH with automatic login |
| Password Storage | ✅ Complete | Optional password field with show/hide |
| Connection Testing | ✅ Complete | Ping-based connectivity testing |
| JSON Persistence | ✅ Complete | Automatic save/load of connections |
| Context Menus | ✅ Complete | Right-click actions throughout UI |
| Font Management | ✅ Complete | Font selection and zoom controls |
| Color Schemes | ✅ Complete | Multiple terminal color themes |
| SSH Key Support | 🚧 Planned | Integration with SSH agent and keys |
| Encrypted Storage | 🚧 Planned | Secure password encryption |
| Import/Export | 🚧 Planned | Share connection configurations |

## API Reference

### SSHConnection Structure
```cpp
struct SSHConnection {
    QString name;        // Display name for the connection
    QString host;        // Hostname or IP address
    QString username;    // SSH username
    QString password;    // SSH password (optional)
    int port;           // SSH port (default: 22)
    QString folder;     // Organization folder
};
```

### Key Methods
- `createSSHTerminal(const SSHConnection &connection)` - Creates terminal with SSH connection
- `loadConnections()` - Loads connections from JSON file
- `saveConnections()` - Saves connections to JSON file
- `addNewConnection()` - Opens dialog to create new connection
- `editConnection(QTreeWidgetItem *item)` - Edit existing connection

## Contributing

### Areas for Contribution
1. **SSH Key Management** - Integrate with SSH agent and key files
2. **Password Encryption** - Implement secure password storage
3. **Connection Import** - Support for other terminal manager formats
4. **Themes** - Additional terminal color schemes
5. **Shortcuts** - More keyboard shortcuts and customization
6. **Documentation** - Improve code documentation and examples

### Development Guidelines
1. Maintain Qt coding standards
2. Add unit tests for new features
3. Update documentation for API changes
4. Test on multiple Linux distributions
5. Consider security implications for password-related features

## Version History

### v0.4 (Current)
- ✅ Added password field to SSH connections
- ✅ Implemented sshpass integration for automatic authentication
- ✅ Added show/hide password toggle in connection dialog
- ✅ Updated JSON storage to include password field
- ✅ Enhanced connection tooltips with password indicators
- ✅ Improved security documentation and best practices

### v0.3
- ✅ Complete SSH connection implementation
- ✅ Add/Edit/Delete connection dialogs
- ✅ Connection testing functionality
- ✅ JSON persistence with folder organization

### v0.2
- ✅ Connection tree with folder organization
- ✅ Emoji-based visual organization
- ✅ Basic connection management

### v0.1
- ✅ Basic terminal interface
- ✅ Multi-tab support
- ✅ Font and color management

## Roadmap

### Short Term (v0.5)
- SSH key file integration
- Connection import/export
- Enhanced error handling
- Connection history tracking

### Medium Term (v0.6)
- Encrypted password storage
- SSH agent integration
- Quick connect toolbar
- Advanced SSH options

### Long Term (v1.0)
- Plugin system
- Scripting support
- Remote file browser
- Session recording

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Acknowledgments

- Built with [Qt Framework](https://www.qt.io/) for cross-platform GUI
- Uses [qtermwidget](https://github.com/lxqt/qtermwidget) for terminal emulation
- Inspired by [mRemoteNG](https://mremoteng.org/) for connection management UX
- [sshpass](https://sourceforge.net/projects/sshpass/) for password automation
- Terminal design influenced by modern terminal emulators
- Thanks to the Qt and Linux terminal community for guidance

## Support

For questions, feature requests, or bug reports:
- **Issues**: Use [GitHub Issues](../../issues) for bug reports and feature requests
- **Discussions**: Use [GitHub Discussions](../../discussions) for questions and ideas
- **Security**: Report security issues privately via email
- **Documentation**: Check this README and code comments

## Author

**QtTerminalExample** - A modern terminal with SSH connection management  
Created with ❤️ for the Linux terminal community

---

**Status**: Active development | **Version**: 0.4 | **Next Milestone**: SSH Key Management

*Secure your connections, streamline your workflow* 🚀