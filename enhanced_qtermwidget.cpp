#include "enhanced_qtermwidget.h"
#include <QApplication>
#include <QScrollBar>
#include <QStyle>
#include <QAbstractButton>

EnhancedQTermWidget::EnhancedQTermWidget(QWidget *parent) : QTermWidget(parent) {
    
    // Find the terminal display widget (usually the first child widget that handles display)
    m_terminalDisplay = nullptr;
    
    // Method 1: Find by object name if it's set
    m_terminalDisplay = findChild<QWidget*>("TerminalDisplay");
    
    // Method 2: If not found by name, find the first widget that's likely the display
    if (!m_terminalDisplay) {
        QList<QWidget*> children = findChildren<QWidget*>();
        for (QWidget* child : children) {
            // Look for widgets that typically handle mouse events and drawing
            if (child->acceptDrops() || child->hasMouseTracking() || 
                child->objectName().contains("display", Qt::CaseInsensitive) ||
                child->objectName().contains("terminal", Qt::CaseInsensitive)) {
                m_terminalDisplay = child;
                break;
            }
        }
    }
    
    // Method 3: Fallback - use the first child widget if available
    if (!m_terminalDisplay && !children().isEmpty()) {
        for (QObject* child : children()) {
            if (QWidget* widget = qobject_cast<QWidget*>(child)) {
                // Skip scrollbars and other utility widgets
                if (!qobject_cast<QScrollBar*>(widget) && 
                    !qobject_cast<QAbstractButton*>(widget)) {
                    m_terminalDisplay = widget;
                    break;
                }
            }
        }
    }
    
    // Install event filter to intercept mouse events
    if (m_terminalDisplay) {
        m_terminalDisplay->installEventFilter(this);
        qDebug() << "Event filter installed on terminal display:" 
             << (m_terminalDisplay->objectName().isEmpty() ? 
                 "(unnamed)" : m_terminalDisplay->objectName());
    } else {
        qDebug() << "Warning: Could not find terminal display widget";
        // Fallback: install on the main widget itself
        installEventFilter(this);
    }
}

bool EnhancedQTermWidget::eventFilter(QObject *obj, QEvent *event) {
    // Skip our own synthetic events to prevent infinite loops
    if (event->spontaneous() == false) {
        return QTermWidget::eventFilter(obj, event);
    }
    
    // Handle keyboard events first
    if (obj == m_terminalDisplay && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);

        // Handle Ctrl+Z as interrupt (since Ctrl+C is hijacked by clipboard)
        if (keyEvent->key() == Qt::Key_Z && keyEvent->modifiers() & Qt::ControlModifier) {
            sendText(QByteArray(1, 0x03)); // Send interrupt (Ctrl+C character)
            return true; // Consume the event
        }
        
        // Handle Ctrl+A for select all
        if (keyEvent->key() == Qt::Key_A && keyEvent->modifiers() & Qt::ControlModifier) {
            selectAll();
            return true; // Consume the event
        }

        // Handle other control characters
        if (keyEvent->modifiers() & Qt::ControlModifier) {
            switch (keyEvent->key()) {
                case Qt::Key_D:
                    sendText(QByteArray(1, 0x04));  // Ctrl+D
                    return true;
                case Qt::Key_L:
                    sendText(QByteArray(1, 0x0C));  // Ctrl+L
                    return true;
                case Qt::Key_U:
                    sendText(QByteArray(1, 0x15));  // Ctrl+U
                    return true;
            }
        }
    }
    #if 0 // eventFilter is interfering with QTermWidget's native selection mechanism.
    // Handle mouse events
    if (obj == m_terminalDisplay && event->type() >= QEvent::MouseButtonPress && event->type() <= QEvent::MouseMove) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        
        switch (event->type()) {
            case QEvent::MouseButtonPress:
                return handleMousePress(mouseEvent);
            case QEvent::MouseMove:
                if (m_isSelecting) {
                    handleMouseMove(mouseEvent);
                    return true; // Consume the event
                }
                break;
            case QEvent::MouseButtonRelease:
                handleMouseRelease(mouseEvent);
                break;
            default:
                break;
        }
    }
    #endif
    return QTermWidget::eventFilter(obj, event);
}

void EnhancedQTermWidget::selectAll() {
    if (!m_terminalDisplay || !findChild<QScrollBar*>()) return;
    
    QScrollBar* scrollBar = findChild<QScrollBar*>();
    int originalScrollValue = scrollBar->value();
    
    // Temporarily remove event filter to prevent recursion
    m_terminalDisplay->removeEventFilter(this);
    
    // More aggressive update blocking
    setUpdatesEnabled(false);
    m_terminalDisplay->setUpdatesEnabled(false);
    setAttribute(Qt::WA_UpdatesDisabled, true);
    m_terminalDisplay->setAttribute(Qt::WA_UpdatesDisabled, true);
    
    // Perform selection operations
    scrollBar->setValue(scrollBar->minimum());
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    
    QPoint startPos(5, 5);
    QMouseEvent press(QEvent::MouseButtonPress, startPos, 
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(m_terminalDisplay, &press);
    
    scrollBar->setValue(scrollBar->maximum());
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    
    QRect displayRect = m_terminalDisplay->geometry();
    QPoint endPos(displayRect.width() - 10, displayRect.height() - 10);
    
    QMouseEvent move(QEvent::MouseMove, endPos, 
                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(m_terminalDisplay, &move);
    
    QMouseEvent release(QEvent::MouseButtonRelease, endPos, 
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(m_terminalDisplay, &release);
    
    // Restore scroll position before re-enabling updates
    scrollBar->setValue(originalScrollValue);

    // Re-enable everything at once
    setAttribute(Qt::WA_UpdatesDisabled, false);
    m_terminalDisplay->setAttribute(Qt::WA_UpdatesDisabled, false);
    setUpdatesEnabled(true);
    m_terminalDisplay->setUpdatesEnabled(true);
    
    // Reinstall event filter
    m_terminalDisplay->installEventFilter(this);
    
    m_terminalDisplay->update();
}