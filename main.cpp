// ============ Updated main.cpp with Feature 4 ============
#include <QApplication>
#include "terminalwindow.h"

int main(int argc, char *argv[])
{
    // Force X11 platform to avoid Wayland issues
    qputenv("QT_QPA_PLATFORM", "xcb");
    
    QApplication app(argc, argv);
    
    app.setOrganizationName("MyCompany");
    app.setApplicationName("QtTerminal");

    // Register the custom type for QVariant system (Feature 4)
    qRegisterMetaType<SSHConnection>("SSHConnection");

    app.setWindowIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));

    // Configure color schemes globally for all QTermWidget instances.
    // The color-schemes folder next to the executable is copied there by
    // CMake at build time, so schemes load without installing qtermwidget.
    // The system path is kept as a fallback for packaged qtermwidget.
    QTermWidget::addCustomColorSchemeDir(QCoreApplication::applicationDirPath() + "/color-schemes");
    QTermWidget::addCustomColorSchemeDir("/usr/share/qtermwidget5/color-schemes");


    TerminalWindow window;
    window.show();

    return app.exec();
}