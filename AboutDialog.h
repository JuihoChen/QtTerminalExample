#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>

class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);

    // Shows the dialog modally over window: as its own window normally, but
    // as a panel inside window under WSLg (see inwindowdialog.h)
    static void showFor(QWidget *window);

private:
    void setupUI();

    QPushButton *m_closeButton = nullptr;
};

#endif // ABOUTDIALOG_H
