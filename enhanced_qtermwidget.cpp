#include "enhanced_qtermwidget.h"
#include <QApplication>
#include <QScrollBar>
#include <QStyle>
#include <QAbstractButton>
#include <QTimer>
#include <QDebug>

TerminalPositionManager::TerminalPositionManager(QTermWidget* terminal, QWidget* terminalDisplay)
    : m_terminal(terminal)
    , m_terminalDisplay(terminalDisplay ? terminalDisplay : terminal)
    , m_scrollBar(nullptr)
{
    if (m_terminal) {
        m_scrollBar = m_terminal->findChild<QScrollBar*>();
    }
}

std::pair<int, int> TerminalPositionManager::getPositionFromPixels(int x, int y)
{
    if (!m_terminal) return std::make_pair(0, 0);
    
    int screenCols = m_terminal->screenColumnsCount();
    int screenLines = m_terminal->screenLinesCount();
    
    if (screenCols <= 0 || screenLines <= 0) {
        return std::make_pair(0, 0);
    }
    
    std::pair<double, double> charDim = getCharacterDimensions();
    double charWidth = charDim.first;
    double charHeight = charDim.second;
    
    // Convert pixels to character grid coordinates
    int col = static_cast<int>(x / charWidth);
    int displayRow = static_cast<int>(y / charHeight);
    
    // Apply scroll compensation
    int terminalRow = displayRow;
    if (m_scrollBar) {
        int scrollValue = m_scrollBar->value();
        if (scrollValue == 0) {
            terminalRow = displayRow;
        } else {
            terminalRow = displayRow + scrollValue;
        }
    }
    
    return std::make_pair(terminalRow, col);
}

QPoint TerminalPositionManager::getPixelsFromPosition(int row, int col)
{
    if (!m_terminal) return QPoint(0, 0);
    
    std::pair<double, double> charDim = getCharacterDimensions();
    double charWidth = charDim.first;
    double charHeight = charDim.second;
    
    // Convert terminal row to display row
    int displayRow = row;
    if (m_scrollBar) {
        int scrollValue = m_scrollBar->value();
        if (scrollValue == 0) {
            displayRow = row;
        } else {
            displayRow = row - scrollValue;
        }
    }
    
    // Convert to pixel coordinates
    int x = static_cast<int>((col + 0.5) * charWidth);
    int y = static_cast<int>((displayRow + 0.5) * charHeight);
    
    return QPoint(x, y);
}

std::pair<double, double> TerminalPositionManager::getCharacterDimensions() {
    QFont terminalFont = m_terminalDisplay->font();
    QFontMetrics metrics(terminalFont);
    
    double charWidth = metrics.horizontalAdvance("M");
    // Use ascent + descent instead of lineSpacing to get actual character cell height
    // lineSpacing includes extra spacing between lines which causes positioning drift during zoom
    double charHeight = metrics.ascent() + metrics.descent();
    
    return std::make_pair(charWidth, charHeight);
}

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

    // Calculate coordinates explicitly for different coordinate systems
    int scrollableLines = scrollBar ? scrollBar->maximum() : 0;

    // For ScreenWindow::setSelectionStart - uses display coordinates (will be adjusted internally)
    int displayStartRow = scrollableLines > 0 ? -scrollableLines : 0;

    // For Screen::setSelectionEnd - uses buffer coordinates (no internal adjustment)
    int bufferEndRow = screenLines + scrollableLines - 1;

    qDebug() << "Coordinate system fix:";
    qDebug() << "  Display start row (for setSelectionStart):" << displayStartRow;
    qDebug() << "  Buffer end row (for setSelectionEnd):" << bufferEndRow;
    qDebug() << "Setting selection from (" << displayStartRow << ",0) to (" << bufferEndRow << "," << (screenColumns-1) << ")";

    setSelectionStart(displayStartRow, 0);
    setSelectionEnd(bufferEndRow, screenColumns - 1);

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
        // Use the display coordinate for consistency with selection tracking
        m_selectionAnchorRow = displayStartRow;
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

            // Only start dragging if:
            // 1. Left button is pressed
            // 2. Not currently dragging
            // 3. Shift key is NOT pressed (no shift+click operation)
            if (mouseEvent->buttons() & Qt::LeftButton &&
                !m_isDragging &&
                !(mouseEvent->modifiers() & Qt::ShiftModifier)) {
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

                    // Force immediate processing of selection events
                    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

                    // Now get bounds immediately - no timer needed
                    try {
                        int actualStartRow, actualStartCol, actualEndRow, actualEndCol;
                        getSelectionStart(actualStartRow, actualStartCol);
                        getSelectionEnd(actualEndRow, actualEndCol);

                        qDebug() << "ACTUAL selection after drag: start(" << actualStartRow << "," << actualStartCol 
                                 << ") end(" << actualEndRow << "," << actualEndCol << ")";
                        qDebug() << "Original click position: (" << m_clickRow << "," << m_clickCol << ")";

                        // Determine drag direction by comparing click position with actual selection bounds
                        // For downward drag: click position will be closer to the start (drag started there)
                        // For upward drag: click position will be closer to the end (drag started there)

                        // Calculate distance from click to start vs end
                        int distToStart = abs((m_clickRow - actualStartRow) * 10000 + (m_clickCol - actualStartCol));
                        int distToEnd = abs((m_clickRow - actualEndRow) * 10000 + (m_clickCol - actualEndCol));

                        if (distToStart <= distToEnd) {
                            // Click was closer to start - this was a downward drag
                            // Anchor should be at START so shift+click can extend upward
                            m_selectionAnchorRow = actualStartRow;
                            m_selectionAnchorCol = actualStartCol;
                            qDebug() << "Downward drag detected - anchor set to START: (" << m_selectionAnchorRow << "," << m_selectionAnchorCol << ")";
                        } else {
                            // Click was closer to end - this was an upward drag
                            // Anchor should be at END so shift+click can extend downward
                            m_selectionAnchorRow = actualEndRow;
                            m_selectionAnchorCol = actualEndCol;
                            qDebug() << "Upward drag detected - anchor set to END: (" << m_selectionAnchorRow << "," << m_selectionAnchorCol << ")";
                        }

                        updateSelectionState(true); // Skip anchor update since we just set it

                    } catch (...) {
                        qDebug() << "Could not get actual selection bounds, using click-based anchor";
                        // Fallback: use the stored click position as anchor
                        m_selectionAnchorRow = m_clickRow;
                        m_selectionAnchorCol = m_clickCol;
                        updateSelectionState(false);
                    }
                } else {
                    // Simple click - may clear selection
                    updateSelectionState(false);
                }
            }
        }
    }

    return QTermWidget::eventFilter(obj, event);
}

void EnhancedQTermWidget::handleShiftClick(QMouseEvent *mouseEvent) {
    qDebug() << "Shift+Click detected for selection extension";
    
    // Get click position
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

// Apply coordinate system adjustments for modified setSelection functions
    //
    // Background: We have asymmetric coordinate handling due to selective modifications:
    // - setSelectionStart() was modified to call ScreenWindow::setSelectionStart() 
    //   which applies scroll offset internally: qMin(line + currentLine(), endWindowLine())
    // - setSelectionEnd() kept original implementation (no scroll handling)
    //
    // Therefore:
    // - setSelectionStart() expects display coordinates (will add scroll internally)
    // - setSelectionEnd() expects terminal coordinates (no scroll adjustment)

    bool applyScrollAdjustment = true;  // Set to false to disable scroll compensation

    if (applyScrollAdjustment) {
        QScrollBar* scrollBar = findChild<QScrollBar*>();
        if (scrollBar) {
            int scrollValue = scrollBar->value();
            qDebug() << "Applying scroll adjustment:" << scrollValue;

            // Only adjust start row since setSelectionStart() expects display coordinates
            newStartRow -= scrollValue;

            // Don't adjust end row since setSelectionEnd() expects terminal coordinates
            // newEndRow -= scrollValue;  // Commented out - causes double adjustment
        }
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
        // Don't update anchor here - it should remain at the original anchor point
    } else {
        qDebug() << "Selection extension failed";
    }
}

void EnhancedQTermWidget::updateSelectionState(bool skipAnchorUpdate) {
    QString currentSelection = selectedText(true);
    bool hadSelection = m_hasActiveSelection;
    m_hasActiveSelection = !currentSelection.isEmpty();
    
    if (m_hasActiveSelection && !hadSelection) {
        qDebug() << "New selection detected, length:" << currentSelection.length();

        // Only update anchor if we're not skipping it (i.e., for non-drag selections)
        if (!skipAnchorUpdate) {
            try {
                int startRow, startCol, endRow, endCol;
                getSelectionStart(startRow, startCol);
                getSelectionEnd(endRow, endCol);

                // For non-drag selections, anchor at start
                m_selectionAnchorRow = startRow;
                m_selectionAnchorCol = startCol;

                qDebug() << "Updated selection anchor to START: (" << m_selectionAnchorRow << "," << m_selectionAnchorCol << ")";
            } catch (...) {
                qDebug() << "Could not update selection anchor";
            }
        } else {
            qDebug() << "Skipped anchor update (drag selection)";
        }
    } else if (!m_hasActiveSelection && hadSelection) {
        qDebug() << "Selection cleared";
        m_selectionAnchorRow = 0;
        m_selectionAnchorCol = 0;
    }
}