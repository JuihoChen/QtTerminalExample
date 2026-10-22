// ============ Updated main.cpp with Feature 4 ============
#include <QApplication>
#include "terminalwindow.h"
#include "sftpworker.h"

int main(int argc, char *argv[])
{
    // Default to X11 to avoid Wayland issues, but let QT_QPA_PLATFORM pick
    // another platform (e.g. QT_QPA_PLATFORM=wayland) to compare redrawing.
    // On WSLg, X11 leaves a closed menu's picture on screen for a second, so
    // use Wayland there, falling back to X11 if Qt's Wayland plugin is missing.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        bool wslg = !qEnvironmentVariableIsEmpty("WSL_DISTRO_NAME")
                    && !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
        qputenv("QT_QPA_PLATFORM", wslg ? "wayland;xcb" : "xcb");
    }
    
    // Programs in the terminals need TERM; a desktop launcher does not set it
    if (qEnvironmentVariableIsEmpty("TERM")) {
        qputenv("TERM", "xterm-256color");
    }

    QApplication app(argc, argv);
    
    app.setOrganizationName("MyCompany");
    app.setApplicationName("QtTerminal");
    app.setApplicationVersion(APP_VERSION);
    // Matches the qt-terminal.desktop launcher from the README. On Wayland
    // (and WSLg's Windows taskbar) the window is matched to its launcher, and
    // so to its icon, by this name rather than by the window icon set below.
    app.setDesktopFileName("qt-terminal");

    // Register the custom type for QVariant system (Feature 4)
    qRegisterMetaType<SSHConnection>("SSHConnection");

    // Bundled app icon as PNGs, so it shows without Qt's SVG plugin. They are
    // rendered from icons/qtterminal.svg (16-32 px from qtterminal-small.svg).
    QIcon appIcon;
    for (int size : {16, 24, 32, 48, 64, 128, 256})
        appIcon.addFile(QString(":/icons/qtterminal-%1.png").arg(size), QSize(size, size));
    app.setWindowIcon(appIcon);

    // Configure color schemes globally for all QTermWidget instances.
    // The color-schemes folder next to the executable is copied there by
    // CMake at build time, so schemes load without installing qtermwidget.
    // The system path is kept as a fallback for packaged qtermwidget.
    QTermWidget::addCustomColorSchemeDir(QCoreApplication::applicationDirPath() + "/color-schemes");
    QTermWidget::addCustomColorSchemeDir("/usr/share/qtermwidget5/color-schemes");


    int result;
    {
        TerminalWindow window;
        window.show();
        result = app.exec();
    }

    // Let file browser connections that are still closing finish cleanly
    SftpWorker::waitForRetired(3000);
    return result;
}