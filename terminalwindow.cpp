// ============ terminalwindow.cpp ============
#include "terminalwindow.h"

#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QAction>
#include <QMenuBar>
#include <QLabel>
#include <QStatusBar>
#include <QFont>
#include <QFontDialog>
#include <QColorDialog>
#include <QSettings>
#include <QCloseEvent>
#include <QMessageBox>
#include <QKeySequence>

TerminalWindow::TerminalWindow(QWidget *parent) 
    : QMainWindow(parent), terminal(nullptr)
{
    setupUI();
    setupMenus();
    loadSettings();
}

TerminalWindow::~TerminalWindow()
{
    saveSettings();
}

void TerminalWindow::closeEvent(QCloseEvent *event)
{
    // Simple approach - always ask for confirmation
    int ret = QMessageBox::question(this, "Close Terminal",
                                  "Close terminal window?",
                                  QMessageBox::Yes | QMessageBox::No);
    if (ret == QMessageBox::No) {
        event->ignore();
        return;
    }
    event->accept();
}

void TerminalWindow::openFontDialog()
{
    bool ok;
    QFont font = QFontDialog::getFont(&ok, terminal->getTerminalFont(), this);
    if (ok) {
        terminal->setTerminalFont(font);
        updateStatusBar();
    }
}

void TerminalWindow::changeColorScheme()
{
    static QStringList schemes = {"Linux", "GreenOnBlack", "WhiteOnBlack", "BlackOnWhite"};
    static int currentScheme = 0;
    
    currentScheme = (currentScheme + 1) % schemes.size();
    terminal->setColorScheme(schemes.at(currentScheme));
    
    statusBar()->showMessage(QString("Color scheme: %1").arg(schemes.at(currentScheme)), 2000);
}

void TerminalWindow::increaseFont()
{
    QFont font = terminal->getTerminalFont();
    font.setPointSize(font.pointSize() + 1);
    terminal->setTerminalFont(font);
    updateStatusBar();
}

void TerminalWindow::decreaseFont()
{
    QFont font = terminal->getTerminalFont();
    if (font.pointSize() > 6) {
        font.setPointSize(font.pointSize() - 1);
        terminal->setTerminalFont(font);
        updateStatusBar();
    }
}

void TerminalWindow::resetFont()
{
    terminal->setTerminalFont(QFont("Monospace", 12));
    updateStatusBar();
}

void TerminalWindow::updateStatusBar()
{
    QFont font = terminal->getTerminalFont();
    statusBar()->showMessage(QString("Font: %1 %2pt").arg(font.family()).arg(font.pointSize()));
}

void TerminalWindow::newTab()
{
    TerminalWindow *newWindow = new TerminalWindow();
    newWindow->show();
}

void TerminalWindow::selectAllText()
{
    // Send Ctrl+A to select all text (universal terminal shortcut)
    terminal->sendText("\x01"); // Ctrl+A ASCII code
}

void TerminalWindow::setupUI()
{
    setWindowTitle("Advanced Qt Terminal");

    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout *layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(2, 2, 2, 2);

    terminal = new QTermWidget(this);
    
    terminal->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(terminal, &QWidget::customContextMenuRequested, 
            this, &TerminalWindow::showContextMenu);

    terminal->setShellProgram("/bin/bash");
    terminal->setColorScheme("Linux");
    terminal->setTerminalFont(QFont("Monospace", 12));
    terminal->setScrollBarPosition(QTermWidget::ScrollBarRight);
    terminal->setMotionAfterPasting(2);

    layout->addWidget(terminal);

    // Connect terminal finished signal to close window
    connect(terminal, &QTermWidget::finished, this, [this]() {
        this->close();
    });

    statusBar()->showMessage("Ready");
    updateStatusBar();

    resize(1200, 800);
}

void TerminalWindow::setupMenus()
{
    QMenuBar *menuBar = this->menuBar();

    // File menu
    QMenu *fileMenu = menuBar->addMenu("&File");
    fileMenu->addAction("&New Terminal", this, &TerminalWindow::newTab, QKeySequence::New);
    fileMenu->addSeparator();
    fileMenu->addAction("&Quit", this, &QWidget::close, QKeySequence::Quit);

    // Edit menu
    QMenu *editMenu = menuBar->addMenu("&Edit");
    editMenu->addAction("&Copy", terminal, &QTermWidget::copyClipboard, QKeySequence::Copy);
    editMenu->addAction("&Paste", terminal, &QTermWidget::pasteClipboard, QKeySequence::Paste);
    editMenu->addAction("Select &All", this, &TerminalWindow::selectAllText, QKeySequence::SelectAll);
    editMenu->addSeparator();
    editMenu->addAction("&Clear", terminal, &QTermWidget::clear);

    // View menu
    QMenu *viewMenu = menuBar->addMenu("&View");
    viewMenu->addAction("&Font...", this, &TerminalWindow::openFontDialog);
    viewMenu->addAction("&Color Scheme", this, &TerminalWindow::changeColorScheme);
    viewMenu->addSeparator();
    viewMenu->addAction("Zoom &In", this, &TerminalWindow::increaseFont, QKeySequence::ZoomIn);
    viewMenu->addAction("Zoom &Out", this, &TerminalWindow::decreaseFont, QKeySequence::ZoomOut);
    viewMenu->addAction("&Reset Zoom", this, &TerminalWindow::resetFont, QKeySequence(Qt::CTRL + Qt::Key_0));
}

void TerminalWindow::showContextMenu(const QPoint &pos)
{
    QMenu menu;
    
    menu.addAction("Copy", terminal, &QTermWidget::copyClipboard);
    menu.addAction("Paste", terminal, &QTermWidget::pasteClipboard);
    menu.addSeparator();
    menu.addAction("Select All", this, &TerminalWindow::selectAllText);
    menu.addSeparator();
    menu.addAction("Clear", terminal, &QTermWidget::clear);
    menu.addSeparator();
    
    QMenu *fontMenu = menu.addMenu("Font Size");
    fontMenu->addAction("Increase", this, &TerminalWindow::increaseFont);
    fontMenu->addAction("Decrease", this, &TerminalWindow::decreaseFont);
    fontMenu->addAction("Reset", this, &TerminalWindow::resetFont);
    
    menu.addAction("Change Color Scheme", this, &TerminalWindow::changeColorScheme);

    menu.exec(terminal->mapToGlobal(pos));
}

void TerminalWindow::saveSettings()
{
    QSettings settings;
    settings.setValue("geometry", saveGeometry());
    settings.setValue("font", terminal->getTerminalFont());
}

void TerminalWindow::loadSettings()
{
    QSettings settings;
    restoreGeometry(settings.value("geometry").toByteArray());
    
    QFont font = settings.value("font", QFont("Monospace", 12)).value<QFont>();
    terminal->setTerminalFont(font);
    updateStatusBar();
}
