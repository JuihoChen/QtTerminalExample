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

    TerminalWindow window;
    window.show();

    return app.exec();
}