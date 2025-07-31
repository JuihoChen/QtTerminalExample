// ============ terminalwindow.h ============
#ifndef TERMINALWINDOW_H
#define TERMINALWINDOW_H

#include <QMainWindow>
#include <qtermwidget.h>

QT_BEGIN_NAMESPACE
class QVBoxLayout;
class QMenu;
class QAction;
class QMenuBar;
class QStatusBar;
class QCloseEvent;
QT_END_NAMESPACE

class TerminalWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit TerminalWindow(QWidget *parent = nullptr);
    ~TerminalWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void openFontDialog();
    void changeColorScheme();
    void increaseFont();
    void decreaseFont();
    void resetFont();
    void updateStatusBar();
    void newTab();
    void showContextMenu(const QPoint &pos);
    void selectAllText();

private:
    void setupUI();
    void setupMenus();
    void saveSettings();
    void loadSettings();

    QTermWidget *terminal;
};

#endif // TERMINALWINDOW_H