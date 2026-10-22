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
    // as a panel inside window under WSLg (see AboutDialog.cpp)
    static void showFor(QWidget *window);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUI();
    void execInside(QWidget *window);
    void centerInBackdrop();
    bool isOwnWidget(QObject *object) const;

    QPushButton *m_closeButton = nullptr;
    QWidget *m_host = nullptr;
    QWidget *m_backdrop = nullptr;
};

#endif // ABOUTDIALOG_H
