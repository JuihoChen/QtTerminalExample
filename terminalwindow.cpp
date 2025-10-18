// ============ Updated terminalwindow.cpp with Split Left Pane ============
#include "terminalwindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QMessageBox>
#include <QSettings>

TerminalWindow::TerminalWindow(QWidget *parent) 
    : QMainWindow(parent), tabWidget(nullptr), tabCounter(1), hasSelectedConnection(false)
{
    setupUI();
    setupMenus();
    loadSettings();
    
    // Load connections from file
    loadConnections();
    
    // Create first tab
    newTab();
}

// Update destructor to save connections on exit:
TerminalWindow::~TerminalWindow()
{
    saveConnections();  // Save connections before exit
    saveSettings();
}

void TerminalWindow::closeEvent(QCloseEvent *event)
{
    // Always ask for confirmation when closing the window
    QString message;
    if (tabWidget->count() > 1) {
        message = QString("Close all %1 terminal tabs?").arg(tabWidget->count());
    } else {
        message = "Close terminal?";
    }
    
    int ret = QMessageBox::question(this, "Close Terminal", message,
                                  QMessageBox::Yes | QMessageBox::No);
    if (ret == QMessageBox::No) {
        event->ignore();
        return;
    }
    event->accept();
}

void TerminalWindow::saveSettings()
{
    QSettings settings;
    settings.setValue("geometry", saveGeometry());
    
    QTermWidget *terminal = getCurrentTerminal();
    if (terminal) {
        settings.setValue("font", terminal->getTerminalFont());
    }
}

void TerminalWindow::loadSettings()
{
    QSettings settings;
    restoreGeometry(settings.value("geometry").toByteArray());
}