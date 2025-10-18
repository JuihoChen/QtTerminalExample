// Font and appearance-related functionality
#include "terminalwindow.h"

#include <QFontDialog>
#include <QFont>
#include <QStatusBar>

void TerminalWindow::openFontDialog()
{
    QTermWidget *terminal = getCurrentTerminal();
    if (!terminal) return;
    
    bool ok;
    QFont font = QFontDialog::getFont(&ok, terminal->getTerminalFont(), this);
    if (ok) {
        // Apply font to all terminals
        for (int i = 0; i < tabWidget->count(); ++i) {
            QTermWidget *term = qobject_cast<QTermWidget*>(tabWidget->widget(i));
            if (term) {
                term->setTerminalFont(font);
            }
        }
        updateStatusBar();
    }
}

void TerminalWindow::changeColorScheme()
{
    static QStringList schemes = {"Linux", "GreenOnBlack", "WhiteOnBlack", "BlackOnWhite"};
    static int currentScheme = 0;
    
    currentScheme = (currentScheme + 1) % schemes.size();
    
    // Apply color scheme to all terminals
    for (int i = 0; i < tabWidget->count(); ++i) {
        QTermWidget *term = qobject_cast<QTermWidget*>(tabWidget->widget(i));
        if (term) {
            term->setColorScheme(schemes.at(currentScheme));
        }
    }
    
    statusBar()->showMessage(QString("Color scheme: %1").arg(schemes.at(currentScheme)), 2000);
}

void TerminalWindow::increaseFont()
{
    QTermWidget *terminal = getCurrentTerminal();
    if (!terminal) return;
    
    QFont font = terminal->getTerminalFont();
    font.setPointSize(font.pointSize() + 1);
    
    // Apply to all terminals
    for (int i = 0; i < tabWidget->count(); ++i) {
        QTermWidget *term = qobject_cast<QTermWidget*>(tabWidget->widget(i));
        if (term) {
            term->setTerminalFont(font);
        }
    }
    updateStatusBar();
}

void TerminalWindow::decreaseFont()
{
    QTermWidget *terminal = getCurrentTerminal();
    if (!terminal) return;
    
    QFont font = terminal->getTerminalFont();
    if (font.pointSize() > 6) {
        font.setPointSize(font.pointSize() - 1);
        
        // Apply to all terminals
        for (int i = 0; i < tabWidget->count(); ++i) {
            QTermWidget *term = qobject_cast<QTermWidget*>(tabWidget->widget(i));
            if (term) {
                term->setTerminalFont(font);
            }
        }
        updateStatusBar();
    }
}

void TerminalWindow::resetFont()
{
    QFont defaultFont("Monospace", 12);
    
    // Apply to all terminals
    for (int i = 0; i < tabWidget->count(); ++i) {
        QTermWidget *term = qobject_cast<QTermWidget*>(tabWidget->widget(i));
        if (term) {
            term->setTerminalFont(defaultFont);
        }
    }
    updateStatusBar();
}
