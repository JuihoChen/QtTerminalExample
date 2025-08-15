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
    
private:
    QWidget* m_terminalDisplay = nullptr;
    QPoint m_selectionStartPos;
    bool m_hasInitialSelection = false;
    bool m_isSelecting = false;
    
protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    
private slots:
    bool handleMousePress(QMouseEvent *event);
    void handleMouseMove(QMouseEvent *event);
    void handleMouseRelease(QMouseEvent *event);
    
public:
    void selectAll();
    void clearSelection();
    void extendSelectionTo(const QPoint &pos);
};

#endif