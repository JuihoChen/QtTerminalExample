#ifndef GRIPSPLITTER_H
#define GRIPSPLITTER_H

#include <QSplitter>
#include <QSplitterHandle>
#include <QPainter>

class GripSplitterHandle : public QSplitterHandle
{
public:
    GripSplitterHandle(Qt::Orientation orientation, QSplitter *parent)
        : QSplitterHandle(orientation, parent) {
        if (orientation == Qt::Horizontal) {
            setFixedWidth(6);
        } else {
            setFixedHeight(8);
        }
        // Enable mouse tracking to detect hover changes
        setMouseTracking(true);
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        QPainter painter(this);

        bool hovered = underMouse();

        // Background color
        QColor bgColor = hovered ? QColor(100, 149, 237) : QColor(240, 240, 240);
        painter.fillRect(rect(), bgColor);

        // Draw borders
        painter.setPen(QColor(200, 200, 200));
        if (orientation() == Qt::Horizontal) {
            painter.drawLine(0, 0, 0, height());
            painter.drawLine(width()-1, 0, width()-1, height());
        } else {
            painter.drawLine(0, 0, width(), 0);
            painter.drawLine(0, height()-1, width(), height()-1);
        }

        // Draw grip dots - make them darker on hover
        painter.setPen(Qt::NoPen);
        QColor dotColor = hovered ? QColor(80, 80, 80) : QColor(120, 120, 120);
        painter.setBrush(dotColor);

        int centerX = width() / 2;
        int centerY = height() / 2;
        int dotSize = 2;
        int spacing = 6;

        if (orientation() == Qt::Horizontal) {
            // Vertical dots for horizontal splitter
            for (int i = -2; i <= 2; i++) {
                int y = centerY + (i * spacing);
                painter.drawEllipse(centerX - dotSize/2, y - dotSize/2, dotSize, dotSize);
            }
        } else {
            // Horizontal dots for vertical splitter
            for (int i = -2; i <= 2; i++) {
                int x = centerX + (i * spacing);
                painter.drawEllipse(x - dotSize/2, centerY - dotSize/2, dotSize, dotSize);
            }
        }
    }

    void enterEvent(QEvent *event) override {
        update(); // Force redraw when mouse enters
        QSplitterHandle::enterEvent(event);
    }
    
    void leaveEvent(QEvent *event) override {
        update(); // Force redraw when mouse leaves
        QSplitterHandle::leaveEvent(event);
    }
};

class GripSplitter : public QSplitter
{
public:
    GripSplitter(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QSplitter(orientation, parent) {}

protected:
    QSplitterHandle *createHandle() override {
        return new GripSplitterHandle(orientation(), this);
    }
};

#endif // GRIPSPLITTER_H