// Terminal creation and management functionality
#include "terminalwindow.h"
#include "enhanced_qtermwidget.h"
#include "commandsafety.h"

#include <QFont>
#include <QTimer>
#include <QApplication>
#include <QStatusBar>
#include <QClipboard>

QTermWidget* TerminalWindow::createTerminalWidget()
{
    EnhancedQTermWidget* terminal = new EnhancedQTermWidget(this);
    
    // Common settings for ALL terminals
    terminal->setHistorySize(200000);  // 200k lines - unlimited-like experience
    terminal->setColorScheme("Linux");
    terminal->setTerminalFont(QFont("Monospace", 12));
    terminal->setScrollBarPosition(QTermWidget::ScrollBarRight);
    terminal->setMotionAfterPasting(2);
    terminal->setContextMenuPolicy(Qt::CustomContextMenu);
    
    // Common signal connections
    connect(terminal, &QWidget::customContextMenuRequested, 
            this, &TerminalWindow::showContextMenu);
    connect(terminal, &QTermWidget::finished, this, &TerminalWindow::onTerminalFinished);
    
    return terminal;
}

// Updated createTerminal() method - for local terminals
QTermWidget* TerminalWindow::createTerminal()
{
    QTermWidget *terminal = createTerminalWidget();  // Use helper method
    
    // Local terminal specific settings
    terminal->setShellProgram("/bin/bash");
    
    return terminal;
}

// Create SSH terminal with connection parameters and password support
QTermWidget* TerminalWindow::createSSHTerminal(const SSHConnection &connection)
{
    QTermWidget *terminal = createTerminalWidget();

    // Mark this as an SSH terminal
    terminal->setProperty("isSSHTerminal", true);
    terminal->setProperty("sshConnection", QVariant::fromValue(connection));

    // SSH terminal specific setup
    terminal->setShellProgram("/bin/bash");
    terminal->startShellProgram();

    // Build safe SSH command using the helper
    QString sshCommand = CommandSafetyHelper::buildSafeSSHCommand(connection);

    // Monitor terminal output to detect when SSH connection ends
    connect(terminal, &QTermWidget::receivedData, this, [this, terminal](const QString &text) {
        if (text.contains("Connection to") && text.contains("closed.")) {
            bool isSSH = terminal->property("isSSHTerminal").toBool();
            if (isSSH) {
                int tabIndex = tabWidget->indexOf(terminal);
                if (tabIndex != -1) {
                    tabWidget->setTabText(tabIndex, "Terminal");
                    terminal->setProperty("isSSHTerminal", false);
                    terminal->setProperty("sshConnection", QVariant());
                    statusBar()->showMessage("SSH connection closed - returned to local shell", 3000);
                }
            }
        }
    });

    // Wait for terminal to be ready and send command cleanly
    QTimer::singleShot(300, [terminal, sshCommand]() {
        terminal->sendText("clear\n");
        QTimer::singleShot(100, [terminal, sshCommand]() {
            terminal->sendText(sshCommand + "\n");
        });
    });

    return terminal;
}

QTermWidget* TerminalWindow::getCurrentTerminal()
{
    return qobject_cast<QTermWidget*>(tabWidget->currentWidget());
}

QString TerminalWindow::getNextTabTitle()
{
    QString title = QString("Terminal %1").arg(tabCounter++);
    return title;
}

void TerminalWindow::newTab()
{
    QTermWidget *terminal = createTerminal();
    QString tabTitle = getNextTabTitle();
    
    int index = tabWidget->addTab(terminal, tabTitle);
    tabWidget->setCurrentIndex(index);
    
    // Focus the new terminal
    terminal->setFocus();
    
    updateStatusBar();
}

void TerminalWindow::closeTab(int index)
{
    if (tabWidget->count() <= 1) {
        // For the last tab, just close the window directly without asking
        // The closeEvent() will handle the confirmation
        close();
        return;
    }
    
    QWidget *widget = tabWidget->widget(index);
    tabWidget->removeTab(index);
    widget->deleteLater();
    
    updateStatusBar();
}

void TerminalWindow::closeCurrentTab()
{
    closeTab(tabWidget->currentIndex());
}

void TerminalWindow::onTabChanged(int index)
{
    Q_UNUSED(index)
    updateStatusBar();
    
    // Focus the current terminal
    QTermWidget *terminal = getCurrentTerminal();
    if (terminal) {
        terminal->setFocus();
    }
}

void TerminalWindow::onTerminalFinished()
{
    // Find which terminal finished and close its tab
    QTermWidget *finishedTerminal = qobject_cast<QTermWidget*>(sender());
    if (!finishedTerminal) return;
    
    // Find the tab index
    int tabIndex = -1;
    for (int i = 0; i < tabWidget->count(); ++i) {
        if (tabWidget->widget(i) == finishedTerminal) {
            tabIndex = i;
            break;
        }
    }
    
    if (tabIndex == -1) return;
    
    // Check if this was an SSH terminal
    bool isSSH = finishedTerminal->property("isSSHTerminal").toBool();
    if (isSSH) {
        // When SSH connection ends, the terminal returns to local shell
        // Update the tab title to reflect it's now a local terminal
        QString currentTitle = tabWidget->tabText(tabIndex);
        
        // Remove SSH-specific parts from title and make it a normal terminal title
        QString newTitle = "Terminal";  // or extract base name if you prefer
        tabWidget->setTabText(tabIndex, newTitle);
        
        // Mark as normal terminal now (the shell is still running locally)
        finishedTerminal->setProperty("isSSHTerminal", false);
        finishedTerminal->setProperty("sshConnection", QVariant());
        
        // Show message in status bar
        statusBar()->showMessage("SSH connection closed - returned to local shell", 3000);
        
        // Don't return here - let the terminal continue running as local
        // The terminal process itself hasn't finished, just the SSH connection
        // So we don't want to close or recreate anything
        return;
    }
    
    // For local terminals that actually finished, continue with normal closure logic
    if (tabWidget->count() <= 1) {
        QApplication::quit();
        return;
    }
    
    // For multiple tabs, just close this tab
    QWidget *widget = tabWidget->widget(tabIndex);
    tabWidget->removeTab(tabIndex);
    widget->deleteLater();
    
    updateStatusBar();
}

void TerminalWindow::copyClipboard()
{
        QTermWidget *terminal = getCurrentTerminal();
        //if (terminal) terminal->copyClipboard();
        QString text = terminal->selectedText(true);
        if (!text.isEmpty()) QApplication::clipboard()->setText(text);

}

void TerminalWindow::selectAllText()
{
    QTermWidget *terminal = getCurrentTerminal();
    if (terminal) {
        // Send Ctrl+A to select all text (universal terminal shortcut)
        //terminal->sendText("\x01"); // Ctrl+A ASCII code
        qobject_cast<EnhancedQTermWidget*>(terminal)->selectAll();
    }
}
