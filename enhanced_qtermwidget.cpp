#include "enhanced_qtermwidget.h"
#include <QApplication>
#include <QScrollBar>
#include <QStyle>
#include <QAbstractButton>
#include <QTimer>
#include <QDebug>

EnhancedQTermWidget::EnhancedQTermWidget(QWidget *parent) : QTermWidget(parent) {
    // Initialize member variables
    m_hasActiveSelection = false;
    m_isDragging = false;
    m_selectionAnchorRow = 0;
    m_selectionAnchorCol = 0;

    // Find the terminal display widget
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
        
        // Update selection state after selectAll
        m_hasActiveSelection = true;
        // Set anchor to start of selection for future shift-clicks
        m_selectionAnchorRow = startRow;
        m_selectionAnchorCol = 0;
    } else {
        qDebug() << "ERROR: Selection failed even after scrolling to bottom!";
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

        // Handle other control characters
        if (keyEvent->modifiers() & Qt::ControlModifier) {
            switch (keyEvent->key()) {
                case Qt::Key_Z:
                    sendText(QByteArray(1, 0x03));  // Ctrl+C
                    return true;
                case Qt::Key_A:                     // Ctrl+A
                    selectAll();
                    return true;                                    
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

    // Handle mouse events - only for terminal display
    if (obj == m_terminalDisplay) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            
            if (mouseEvent->button() == Qt::LeftButton) {
                bool shiftPressed = (mouseEvent->modifiers() & Qt::ShiftModifier);
                
                if (shiftPressed && m_hasActiveSelection) {
                    // Shift+Click: Extend existing selection
                    handleShiftClick(mouseEvent);
                    return true; // Consume the event
                } else if (!shiftPressed) {
                    // Normal click: Store press position and reset drag state
                    m_isDragging = false;
                    
                    // Store the click position as potential new anchor
                    std::pair<int, int> clickPos = getPositionFromPixels(mouseEvent->pos().x(), mouseEvent->pos().y());
                    m_clickRow = clickPos.first;
                    m_clickCol = clickPos.second;
                    
                    // Let the base widget handle normal click
                }
            }
        }
        else if (event->type() == QEvent::MouseMove) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            
            if (mouseEvent->buttons() & Qt::LeftButton && !m_isDragging) {
                m_isDragging = true;
                qDebug() << "Drag selection started";
            }
            // Let base widget handle the actual dragging
        }
        else if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
    
            if (mouseEvent->button() == Qt::LeftButton) {
                if (m_isDragging) {
                    // End of drag selection - Get the ACTUAL selection bounds
                    m_isDragging = false;
            
                    // Wait for the selection to be processed, then get real bounds
                    QTimer::singleShot(5, this, [this]() {
                        try {
                            int actualStartRow, actualStartCol, actualEndRow, actualEndCol;
                            getSelectionStart(actualStartRow, actualStartCol);
                            getSelectionEnd(actualEndRow, actualEndCol);
                    
                            qDebug() << "ACTUAL selection after drag: start(" << actualStartRow << "," << actualStartCol 
                                     << ") end(" << actualEndRow << "," << actualEndCol << ")";
                    
                            // Set anchor to the ACTUAL start of selection, not the click position
                            m_selectionAnchorRow = actualStartRow;
                            m_selectionAnchorCol = actualStartCol;
                    
                            qDebug() << "Corrected anchor to ACTUAL start: (" << m_selectionAnchorRow << "," << m_selectionAnchorCol << ")";
                    
                        } catch (...) {
                            qDebug() << "Could not get actual selection bounds, keeping click-based anchor";
                            // Fallback: use the stored click position
                            m_selectionAnchorRow = m_clickRow;
                            m_selectionAnchorCol = m_clickCol;
                        }
                
                        updateSelectionState();
                    });
            
                } else {
                    // Simple click - may clear selection
                    QTimer::singleShot(0, this, &EnhancedQTermWidget::updateSelectionState);
                }
            }
        }
    }

    return QTermWidget::eventFilter(obj, event);
}

void EnhancedQTermWidget::handleShiftClick(QMouseEvent *mouseEvent) {
    qDebug() << "Shift+Click detected for selection extension";
    
    // Get click position - handle overflow properly
    std::pair<int, int> clickPos = getPositionFromPixels(mouseEvent->pos().x(), mouseEvent->pos().y());
    int clickRow = clickPos.first;
    int clickCol = clickPos.second;
    
    qDebug() << "Click position: (" << clickRow << "," << clickCol << ")";
    qDebug() << "Current anchor: (" << m_selectionAnchorRow << "," << m_selectionAnchorCol << ")";
    
    QString currentSelection = selectedText(true);
    if (currentSelection.isEmpty()) {
        qDebug() << "No existing selection for shift+click";
        return;
    }
    
    // Always extend from the stored anchor to the click position
    int newStartRow, newStartCol, newEndRow, newEndCol;
    
    // Compare anchor with click position to determine direction
    if (m_selectionAnchorRow < clickRow || 
        (m_selectionAnchorRow == clickRow && m_selectionAnchorCol <= clickCol)) {
        // Click is after anchor - anchor becomes start, click becomes end
        newStartRow = m_selectionAnchorRow;
        newStartCol = m_selectionAnchorCol;
        newEndRow = clickRow;
        newEndCol = clickCol;
    } else {
        // Click is before anchor - click becomes start, anchor becomes end
        newStartRow = clickRow;
        newStartCol = clickCol;
        newEndRow = m_selectionAnchorRow;
        newEndCol = m_selectionAnchorCol;
    }

    QScrollBar* scrollBar = findChild<QScrollBar*>();
    if (scrollBar) {
        int scrollValue = scrollBar->value();
        qDebug() << "Adjusting for scroll value:" << scrollValue;
        // Adjust rows based on scroll position
        newStartRow -= scrollValue;
        newEndRow -= scrollValue;
    }

    qDebug() << "Extending from anchor to click: start(" << newStartRow << "," << newStartCol 
             << ") end(" << newEndRow << "," << newEndCol << ")";
    
    setSelectionStart(newStartRow, newStartCol);
    setSelectionEnd(newEndRow, newEndCol);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    
    QString extendedSelection = selectedText(true);
    if (!extendedSelection.isEmpty()) {
        qDebug() << "Selection extension successful! New length:" << extendedSelection.length();
        m_hasActiveSelection = true;
    } else {
        qDebug() << "Selection extension failed";
    }
}

void EnhancedQTermWidget::updateSelectionState() {
    QString currentSelection = selectedText(true);
    bool hadSelection = m_hasActiveSelection;
    m_hasActiveSelection = !currentSelection.isEmpty();
    
    if (m_hasActiveSelection && !hadSelection) {
        qDebug() << "New selection detected, length:" << currentSelection.length();
        // Update anchor to current selection start for shift+click
        try {
            int startRow, startCol, endRow, endCol;
            getSelectionStart(startRow, startCol);
            getSelectionEnd(endRow, endCol);
            
            // Set anchor to start of selection
            m_selectionAnchorRow = startRow;
            m_selectionAnchorCol = startCol;
            
            qDebug() << "Updated selection anchor to (" << m_selectionAnchorRow << "," << m_selectionAnchorCol << ")";
        } catch (...) {
            qDebug() << "Could not update selection anchor";
        }
    } else if (!m_hasActiveSelection && hadSelection) {
        qDebug() << "Selection cleared";
        m_selectionAnchorRow = 0;
        m_selectionAnchorCol = 0;
    }
}

// Enhanced position conversion that handles overflow and scroll properly
std::pair<int, int> EnhancedQTermWidget::getPositionFromPixels(int x, int y) {
    qDebug() << "getPositionFromPixelsWithOverflow: input (" << x << "," << y << ")";

    int screenCols = screenColumnsCount();
    int screenLines = screenLinesCount();

    if (screenCols <= 0 || screenLines <= 0) {
        qDebug() << "Invalid terminal dimensions";
        return {0, 0};
    }

    // Get character dimensions
    std::pair<double, double> charDim = getCharacterDimensions();
    double charWidth = charDim.first;
    double charHeight = charDim.second;

    // Convert pixels to character grid coordinates
    int col = static_cast<int>(x / charWidth);
    int displayRow = static_cast<int>(y / charHeight);

    // Use empirical correction based on observed pattern from your debug data
    int terminalRow = displayRow;
    QScrollBar* scrollBar = findChild<QScrollBar*>();
    if (scrollBar) {
        int scrollValue = scrollBar->value();
        int historyLines = historyLinesCount();
        
        // Based on your debug patterns:
        // When scroll=0: displayRow should match terminal coordinates directly
        // When scroll>0: we need to account for the scroll position differently
        
        if (scrollValue == 0) {
            // At bottom: use display row directly
            terminalRow = displayRow;
        } else {
            // When scrolled: your data shows the actual selection is higher than calculated
            // Try: terminalRow = displayRow + scrollValue (back to original logic)
            terminalRow = displayRow + scrollValue;
        }
        
        qDebug() << "Scroll value: " << scrollValue << ", display row: " << displayRow 
                 << " -> terminal row: " << terminalRow;
    }

    qDebug() << "Position with empirical correction: (" << terminalRow << "," << col << ")";
    return {terminalRow, col};
}

// Also update the reverse conversion for consistency
QPoint EnhancedQTermWidget::getPixelsFromPosition(int row, int col) {
    qDebug() << "getPixelsFromPositionWithOverflow: input (" << row << "," << col << ")";
    
    int screenCols = screenColumnsCount();
    int screenLines = screenLinesCount();
    
    if (screenCols <= 0 || screenLines <= 0) {
        return QPoint(0, 0);
    }
    
    std::pair<double, double> charDim = getCharacterDimensions();
    int charWidth = charDim.first;
    int charHeight = charDim.second;
    
    // Convert terminal row to display row
    int displayRow = row;
    QScrollBar* scrollBar = findChild<QScrollBar*>();
    if (scrollBar) {
        int scrollValue = scrollBar->value();
        
        if (scrollValue == 0) {
            displayRow = row;
        } else {
            displayRow = row - scrollValue;
        }
        
        qDebug() << "Converting terminal row " << row << " with scroll " << scrollValue 
                 << " -> display row " << displayRow;
    }
    
    // Convert to pixel coordinates
    int x = (int)((col + 0.5) * charWidth);
    int y = (int)((displayRow + 0.5) * charHeight);
    
    qDebug() << "Position to pixels: (" << row << "," << col << ") -> display(" 
             << displayRow << ") -> pixels(" << x << "," << y << ")";
    return QPoint(x, y);
}

// Helper method to get reliable character dimensions
std::pair<double, double> EnhancedQTermWidget::getCharacterDimensions() {
    // Get font metrics from the appropriate widget
    QFont terminalFont = m_terminalDisplay ? m_terminalDisplay->font() : font();
    QFontMetrics metrics(terminalFont);
    
    double charWidth = metrics.horizontalAdvance("M");
    double charHeight = metrics.lineSpacing();
    
    // Fallback to widget-based calculation if font metrics are unreliable
    int widgetWidth = m_terminalDisplay ? m_terminalDisplay->width() : width();
    int widgetHeight = m_terminalDisplay ? m_terminalDisplay->height() : height();
    
    int screenCols = screenColumnsCount();
    int screenLines = screenLinesCount();
    
    if (screenCols > 0 && screenLines > 0) {
        double widgetBasedCharWidth = (double)widgetWidth / screenCols;
        double widgetBasedCharHeight = (double)widgetHeight / screenLines;
        
        // Use widget-based calculation if font metrics seem off
        if (charWidth <= 0 || charWidth > widgetBasedCharWidth * 2 || charWidth < widgetBasedCharWidth * 0.5) {
            charWidth = widgetBasedCharWidth;
        }
        
        if (charHeight <= 0 || charHeight > widgetBasedCharHeight * 2 || charHeight < widgetBasedCharHeight * 0.5) {
            charHeight = widgetBasedCharHeight;
        }
    }
    
    return {charWidth, charHeight};
}