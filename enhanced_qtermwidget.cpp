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

    // Get scroll bar reference
    QScrollBar* scrollBar = findChild<QScrollBar*>();
    int originalValue = 0;
    bool needsScrollRestore = false;

    if (scrollBar) {
        originalValue = scrollBar->value();
        needsScrollRestore = (originalValue != scrollBar->maximum());
        qDebug() << "Original scroll position:" << originalValue << "max:" << scrollBar->maximum();
    }

    // Minimal anti-flicker - only disable terminal display updates
    if (needsScrollRestore && m_terminalDisplay) {
        m_terminalDisplay->setUpdatesEnabled(false);

        // Scroll to bottom for proper selection
        scrollBar->setValue(scrollBar->maximum());
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        qDebug() << "Silently scrolled to bottom, new position:" << scrollBar->value();
    }

    // Get terminal dimensions
    int screenLines = screenLinesCount();
    int screenColumns = screenColumnsCount();
    int historyLines = historyLinesCount();

    if (screenLines <= 0 || screenColumns <= 0) {
        qDebug() << "Invalid terminal dimensions";
        // Re-enable updates before returning
        if (needsScrollRestore && m_terminalDisplay) {
            m_terminalDisplay->setUpdatesEnabled(true);
        }
        return;
    }

    qDebug() << "Dimensions - Screen:" << screenLines << "x" << screenColumns 
             << "History:" << historyLines;

    int startRow = historyLines > 0 ? -historyLines : 0;
    int endRow = screenLines - 1;

    qDebug() << "Setting selection from (" << startRow << ",0) to (" << endRow << "," << (screenColumns-1) << ")";

    setSelectionStart(startRow, 0);
    setSelectionEnd(endRow, screenColumns - 1);

    // Process selection events
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QString selectedText = this->selectedText(true);
    qDebug() << "Selection result - length:" << selectedText.length();

    // Restore scroll position and re-enable updates
    if (needsScrollRestore && scrollBar && m_terminalDisplay) {
        scrollBar->setValue(originalValue);

        m_terminalDisplay->setUpdatesEnabled(true);
        m_terminalDisplay->update();

        qDebug() << "Restored scroll position to:" << scrollBar->value();
    }

    if (selectedText.length() > 0) {
        qDebug() << "Selection successful! Length:" << selectedText.length();
    } else {
        qDebug() << "ERROR: Selection failed even after scrolling to bottom!";
    }
}