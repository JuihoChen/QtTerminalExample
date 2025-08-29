#ifndef ENHANCED_QTERMWIDGET_H
#define ENHANCED_QTERMWIDGET_H

#include <qtermwidget.h>
#include <QWidget>
#include <QDebug>
#include <QTimer>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QScrollBar>

class TerminalPositionManager
{
public:
    explicit TerminalPositionManager(QTermWidget* terminal, QWidget* terminalDisplay = nullptr);
    
    std::pair<int, int> getPositionFromPixels(int x, int y);
    QPoint getPixelsFromPosition(int row, int col);

private:
    std::pair<double, double> getCharacterDimensions();
    
    QTermWidget* m_terminal;
    QWidget* m_terminalDisplay;
    QScrollBar* m_scrollBar;
};

class EnhancedQTermWidget : public QTermWidget {
    Q_OBJECT

public:
    explicit EnhancedQTermWidget(QWidget *parent = nullptr);
    void selectAll();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void handleShiftClick(QMouseEvent *mouseEvent);
    void updateSelectionState(bool skipAnchorUpdate);

    // Only keep the method that's actually used
    std::pair<int, int> getPositionFromPixels(int x, int y) {
        if (!m_positionManager) {
            m_positionManager = new TerminalPositionManager(this, m_terminalDisplay);
        }
        return m_positionManager->getPositionFromPixels(x, y);
    }

private:
    QWidget* m_terminalDisplay = nullptr;
    TerminalPositionManager* m_positionManager = nullptr;
    bool m_hasActiveSelection = false;
    bool m_isDragging = false;
    int m_selectionAnchorRow = 0;
    int m_selectionAnchorCol = 0;
    int m_clickRow = 0;
    int m_clickCol = 0;
};

#endif