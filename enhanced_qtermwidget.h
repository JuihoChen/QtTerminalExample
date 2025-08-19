#ifndef ENHANCED_QTERMWIDGET_H
#define ENHANCED_QTERMWIDGET_H

#include <qtermwidget.h>
#include <QWidget>
#include <QDebug>
#include <QTimer>
#include <QMouseEvent>
#include <QPaintEvent>

class EnhancedQTermWidget : public QTermWidget {
    Q_OBJECT

public:
    explicit EnhancedQTermWidget(QWidget *parent = nullptr);
    void selectAll();

private:
    QWidget* m_terminalDisplay = nullptr;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
};

#endif