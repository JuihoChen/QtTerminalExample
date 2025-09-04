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

    // Configure color schemes globally for all QTermWidget instances
    // Create a temporary widget to configure global settings
    {
        QTermWidget temp_widget;
        temp_widget.addCustomColorSchemeDir("/usr/share/qtermwidget5/color-schemes");
    } // temp_widget destroyed, but ColorSchemeManager retains the paths


    TerminalWindow window;
    window.show();

    return app.exec();
}