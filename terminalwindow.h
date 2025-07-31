// ============ Updated terminalwindow.h ============
#ifndef TERMINALWINDOW_H
#define TERMINALWINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include <QTreeWidget>
#include <QSplitter>
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
    void closeTab(int index);
    void closeCurrentTab();
    void showContextMenu(const QPoint &pos);
    void selectAllText();
    void onTabChanged(int index);
    void onTerminalFinished();
    
    // New slots for connection tree
    void onConnectionDoubleClicked(QTreeWidgetItem *item, int column);
    void showConnectionContextMenu(const QPoint &pos);

private:
    void setupUI();
    void setupMenus();
    void setupConnectionTree();
    void createDummyConnections();
    void saveSettings();
    void loadSettings();
    QTermWidget* createTerminal();
    QTermWidget* getCurrentTerminal();
    QString getNextTabTitle();

    QTabWidget *tabWidget;
    QTreeWidget *connectionTree;
    QSplitter *splitter;
    int tabCounter;
};

#endif // TERMINALWINDOW_H