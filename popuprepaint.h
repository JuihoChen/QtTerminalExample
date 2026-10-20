#ifndef POPUPREPAINT_H
#define POPUPREPAINT_H

#include <QApplication>
#include <QEvent>
#include <QTimer>
#include <QWidget>

// On WSLg the desktop can leave a closed menu's picture on the window under
// it until something else happens to redraw there. When a menu or other
// popup closes, redraw the windows below it: right away, and again after
// the close animation, since a redraw during it can be covered up again.
class PopupRepaintFilter : public QObject
{
public:
    explicit PopupRepaintFilter(QObject *parent = nullptr) : QObject(parent) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() == QEvent::Hide && obj->isWidgetType()
                && static_cast<QWidget *>(obj)->windowType() == Qt::Popup) {
            for (int delay : {0, 150, 500}) {
                QTimer::singleShot(delay, this, &PopupRepaintFilter::repaintWindows);
            }
        }
        return QObject::eventFilter(obj, event);
    }

private:
    static void repaintWindows() {
        for (QWidget *window : QApplication::topLevelWidgets()) {
            if (window->isVisible() && window->windowType() != Qt::Popup) {
                window->update();
            }
        }
    }
};

#endif
