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

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void handleShiftClick(QMouseEvent *mouseEvent);
    void updateSelectionState();
    QPoint getPixelsFromPosition(int row, int col);
    std::pair<double, double> getCharacterDimensions();
    std::pair<int, int> getPositionFromPixels(int x, int y);

private:
    QWidget* m_terminalDisplay = nullptr;
    bool m_hasActiveSelection = false;
    bool m_isDragging = false;
    int m_selectionAnchorRow = 0;
    int m_selectionAnchorCol = 0;
    int m_clickRow = 0;
    int m_clickCol = 0;
};

#endif