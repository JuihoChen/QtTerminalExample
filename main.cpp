// ============ main.cpp ============
#include <QApplication>
#include "terminalwindow.h"

int main(int argc, char *argv[])
{
    // Force X11 platform to avoid Wayland issues
    qputenv("QT_QPA_PLATFORM", "xcb");
    
    QApplication app(argc, argv);
    
    app.setOrganizationName("MyCompany");
    app.setApplicationName("QtTerminal");

    TerminalWindow window;
    window.show();

    return app.exec();
}
