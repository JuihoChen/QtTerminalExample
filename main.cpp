// ============ Updated main.cpp with Feature 4 ============
#include <QApplication>
#include "terminalwindow.h"
#include "sftpworker.h"
#include "popuprepaint.h"

int main(int argc, char *argv[])
{
    // Default to X11 to avoid Wayland issues, but let QT_QPA_PLATFORM pick
    // another platform (e.g. QT_QPA_PLATFORM=wayland) to compare redrawing
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "xcb");
    }
    
    // Programs in the terminals need TERM; a desktop launcher does not set it
    if (qEnvironmentVariableIsEmpty("TERM")) {
        qputenv("TERM", "xterm-256color");
    }

    QApplication app(argc, argv);
    
    app.setOrganizationName("MyCompany");
    app.setApplicationName("QtTerminal");
    app.setApplicationVersion(APP_VERSION);

    // Don't leave a closed menu's picture behind on WSLg
    app.installEventFilter(new PopupRepaintFilter(&app));

    // Register the custom type for QVariant system (Feature 4)
    qRegisterMetaType<SSHConnection>("SSHConnection");

    app.setWindowIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));

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