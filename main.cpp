#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QMenu>
#include <QAction>

#include <qtermwidget.h>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle("Qt Embedded Terminal Example");

    QVBoxLayout *layout = new QVBoxLayout(&window);
    layout->setContentsMargins(0, 0, 0, 0);  // Full size terminal

    QTermWidget *terminal = new QTermWidget(&window);

    // Enable custom context menu
    terminal->setContextMenuPolicy(Qt::CustomContextMenu);

    // Connect to show a custom menu on right-click
    QObject::connect(terminal, &QWidget::customContextMenuRequested, terminal, [terminal](const QPoint &pos) {
        QMenu menu;
        QAction *copyAction = menu.addAction("Copy");
        QAction *pasteAction = menu.addAction("Paste");

        QObject::connect(copyAction, &QAction::triggered, terminal, &QTermWidget::copyClipboard);
        QObject::connect(pasteAction, &QAction::triggered, terminal, &QTermWidget::pasteClipboard);

        menu.exec(terminal->mapToGlobal(pos));
    });

    // Optional: Set preferred shell
    terminal->setShellProgram("/bin/bash");

    // Optional: Set color scheme
    terminal->setColorScheme("Linux");

    // Set font to control terminal "grid" size
    terminal->setTerminalFont(QFont("Monospace", 12));
    terminal->setScrollBarPosition(QTermWidget::ScrollBarRight);

    layout->addWidget(terminal);

    // Close the window when the terminal is closed
    QObject::connect(terminal, &QTermWidget::finished, &window, &QWidget::close);

    // Set the initial size of the window
    window.resize(1600, 800);
    window.show();

    return app.exec();
}
