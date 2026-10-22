#include "inwindowdialog.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFrame>
#include <QInputDialog>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

bool isWslg() {
    static const bool wslg = QGuiApplication::platformName() == "wayland"
                             && !qEnvironmentVariableIsEmpty("WSL_DISTRO_NAME");
    return wslg;
}

// Dims the window under a panel and takes the clicks meant for it
class Backdrop : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter(this).fillRect(rect(), QColor(0, 0, 0, 90));
    }
};

// Runs one dialog as a panel over a dimmed backdrop inside its main window,
// blocking the rest of the window like a modal dialog would
class Panel : public QObject {
public:
    Panel(QDialog *dialog, QWidget *host, bool showTitle)
        : m_dialog(dialog), m_host(host), m_showTitle(showTitle) {}

    int run();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool isInPanel(QObject *object) const;
    QWidget *initialFocus(QWidget *requested) const;
    void center();

    QDialog *m_dialog;
    QWidget *m_host;
    bool m_showTitle;
    QWidget *m_backdrop = nullptr;
    QFrame *m_panel = nullptr;
    QPointer<QWidget> m_lastFocus;

    // A dialog opened from a panel gets its own panel on top; only the
    // topmost one handles input
    static QList<Panel*> s_stack;
};

QList<Panel*> Panel::s_stack;

int Panel::run() {
    QPointer<QWidget> previousFocus = m_host->focusWidget();

    Backdrop backdrop(m_host);
    backdrop.setGeometry(m_host->rect());

    QFrame panel(&backdrop);
    panel.setObjectName("inWindowPanel");
    panel.setStyleSheet("#inWindowPanel { background: palette(window);"
                        " border: 1px solid rgba(0, 0, 0, 0.35); border-radius: 8px; }"
                        "#inWindowTitle { font-weight: bold; padding: 10px 16px 8px 16px;"
                        " border-bottom: 1px solid rgba(0, 0, 0, 0.12); }");
    QVBoxLayout layout(&panel);
    layout.setSizeConstraint(QLayout::SetFixedSize);
    layout.setContentsMargins(1, 1, 1, 1);
    layout.setSpacing(0);

    // The panel has no title bar, so it shows the dialog's title itself
    QLabel title(m_dialog->windowTitle());
    title.setObjectName("inWindowTitle");
    if (m_showTitle && !m_dialog->windowTitle().isEmpty())
        layout.addWidget(&title);

    // Reparenting forgets the widget the dialog focused for when it shows
    QPointer<QWidget> requestedFocus = m_dialog->focusWidget();
    QWidget *originalParent = m_dialog->parentWidget();
    Qt::WindowFlags originalFlags = m_dialog->windowFlags();
    // A little more room around the dialog than its window frame would give
    QWidget body;
    QVBoxLayout bodyLayout(&body);
    bodyLayout.setContentsMargins(8, 4, 8, 6);
    layout.addWidget(&body);
    m_dialog->setParent(&body, Qt::Widget);
    bodyLayout.addWidget(m_dialog);

    m_backdrop = &backdrop;
    m_panel = &panel;
    m_dialog->setResult(0);
    backdrop.show();
    backdrop.raise();
    m_dialog->show();
    panel.adjustSize();
    center();
    m_lastFocus = initialFocus(requestedFocus);
    if (m_lastFocus)
        m_lastFocus->setFocus(Qt::OtherFocusReason);

    // Keys and shortcuts meant for the main window are dropped while open
    s_stack.append(this);
    qApp->installEventFilter(this);
    QEventLoop loop;
    QObject::connect(m_dialog, &QDialog::finished, &loop, &QEventLoop::quit);
    loop.exec(QEventLoop::DialogExec);
    qApp->removeEventFilter(this);
    s_stack.removeOne(this);

    // The panel is about to be destroyed; it must not take the dialog along
    m_dialog->hide();
    bodyLayout.removeWidget(m_dialog);
    m_dialog->setParent(originalParent, originalFlags);
    m_backdrop = nullptr;
    m_panel = nullptr;
    if (previousFocus)
        previousFocus->setFocus(Qt::OtherFocusReason);
    return m_dialog->result();
}

// Where a dialog window would put the focus when shown: the widget the
// dialog focused itself, else its default button, else the first button
// that can be the default (as QDialog does), else its first focusable widget
QWidget *Panel::initialFocus(QWidget *requested) const {
    auto usable = [this](QWidget *w) {
        return m_dialog->isAncestorOf(w) && w->isVisible() && w->isEnabled();
    };
    // A dialog may focus a widget as it shows, as QInputDialog does
    QWidget *shown = m_dialog->focusWidget();
    if (shown && usable(shown))
        return shown;
    if (requested && usable(requested))
        return requested;
    const QList<QPushButton*> buttons = m_dialog->findChildren<QPushButton*>();
    for (QPushButton *button : buttons) {
        if (button->isDefault() && usable(button))
            return button;
    }
    for (QWidget *w = m_dialog->nextInFocusChain(); w != m_dialog; w = w->nextInFocusChain()) {
        QPushButton *button = qobject_cast<QPushButton*>(w);
        if (button && button->autoDefault() && button->focusPolicy() != Qt::NoFocus
            && usable(button)) {
            button->setDefault(true);
            return button;
        }
    }
    for (QWidget *w = m_dialog->nextInFocusChain(); w != m_dialog; w = w->nextInFocusChain()) {
        if ((w->focusPolicy() & Qt::TabFocus) && usable(w))
            return w;
    }
    return m_dialog;
}

void Panel::center() {
    m_panel->move((m_backdrop->width() - m_panel->width()) / 2,
                  (m_backdrop->height() - m_panel->height()) / 2);
}

bool Panel::isInPanel(QObject *object) const {
    QWidget *widget = qobject_cast<QWidget*>(object);
    return widget && (widget == m_panel || m_panel->isAncestorOf(widget));
}

bool Panel::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Resize) {
        if (watched == m_host)
            m_backdrop->setGeometry(m_host->rect());
        if (watched == m_host || watched == m_panel)
            center();
        return false;
    }
    if (s_stack.isEmpty() || s_stack.last() != this)
        return false;

    // Only widgets in the main window are blocked; other windows, such as a
    // file dialog opened from this panel, work as usual
    QWidget *widget = qobject_cast<QWidget*>(watched);
    bool blocked = widget && widget->window() == m_host && !isInPanel(widget);

    switch (event->type()) {
    case QEvent::Close:
        // Like a window blocked by a modal dialog, the main window stays
        // open; closing it here would end the app under this panel's loop
        if (watched == m_host) {
            event->ignore();
            return true;
        }
        break;
    case QEvent::Shortcut:
        // Menu actions and other shortcuts of the main window; button
        // mnemonics inside the panel still work
        return !isInPanel(watched);
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::ShortcutOverride:
        // Key events reach the QWindow first and then the focused widget;
        // only drop the ones addressed to widgets outside this panel
        if (blocked)
            return true;
        break;
    case QEvent::FocusIn:
        if (isInPanel(watched))
            m_lastFocus = widget;
        else if (blocked && m_lastFocus)
            m_lastFocus->setFocus(Qt::OtherFocusReason);
        break;
    default:
        break;
    }
    return false;
}

QMessageBox::StandardButton showMessageBox(QWidget *parent, QMessageBox::Icon icon,
                                           const QString &title, const QString &text,
                                           QMessageBox::StandardButtons buttons,
                                           QMessageBox::StandardButton defaultButton) {
    // Set up like the QMessageBox functions do, so the same button is default
    QMessageBox box(icon, title, text, QMessageBox::NoButton, parent);
    QDialogButtonBox *buttonBox = box.findChild<QDialogButtonBox*>();
    for (uint mask = QMessageBox::FirstButton; mask <= QMessageBox::LastButton; mask <<= 1) {
        if (!(buttons & mask))
            continue;
        QPushButton *button = box.addButton(QMessageBox::StandardButton(mask));
        if (box.defaultButton())
            continue;
        if ((defaultButton == QMessageBox::NoButton
             && buttonBox && buttonBox->buttonRole(button) == QDialogButtonBox::AcceptRole)
            || (defaultButton != QMessageBox::NoButton && mask == uint(defaultButton)))
            box.setDefaultButton(button);
    }
    if (InWindowDialog::exec(&box) == -1)
        return QMessageBox::Cancel;
    return box.standardButton(box.clickedButton());
}

} // namespace

namespace InWindowDialog {

int exec(QDialog *dialog, bool showTitle) {
    QWidget *host = dialog->parentWidget() ? dialog->parentWidget()->window() : nullptr;
    // A minimized or not yet shown window would hide the panel, so a dialog
    // window is the better choice then
    if (!isWslg() || !host || !host->isVisible() || host->isMinimized())
        return dialog->exec();
    return Panel(dialog, host, showTitle).run();
}

QMessageBox::StandardButton question(QWidget *parent, const QString &title, const QString &text,
                                     QMessageBox::StandardButtons buttons,
                                     QMessageBox::StandardButton defaultButton) {
    if (!isWslg())
        return QMessageBox::question(parent, title, text, buttons, defaultButton);
    return showMessageBox(parent, QMessageBox::Question, title, text, buttons, defaultButton);
}

QMessageBox::StandardButton information(QWidget *parent, const QString &title, const QString &text,
                                        QMessageBox::StandardButtons buttons,
                                        QMessageBox::StandardButton defaultButton) {
    if (!isWslg())
        return QMessageBox::information(parent, title, text, buttons, defaultButton);
    return showMessageBox(parent, QMessageBox::Information, title, text, buttons, defaultButton);
}

QMessageBox::StandardButton warning(QWidget *parent, const QString &title, const QString &text,
                                    QMessageBox::StandardButtons buttons,
                                    QMessageBox::StandardButton defaultButton) {
    if (!isWslg())
        return QMessageBox::warning(parent, title, text, buttons, defaultButton);
    return showMessageBox(parent, QMessageBox::Warning, title, text, buttons, defaultButton);
}

QMessageBox::StandardButton critical(QWidget *parent, const QString &title, const QString &text,
                                     QMessageBox::StandardButtons buttons,
                                     QMessageBox::StandardButton defaultButton) {
    if (!isWslg())
        return QMessageBox::critical(parent, title, text, buttons, defaultButton);
    return showMessageBox(parent, QMessageBox::Critical, title, text, buttons, defaultButton);
}

QString getText(QWidget *parent, const QString &title, const QString &label,
                QLineEdit::EchoMode mode, const QString &text, bool *ok) {
    if (!isWslg())
        return QInputDialog::getText(parent, title, label, mode, text, ok);
    QInputDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setLabelText(label);
    dialog.setTextValue(text);
    dialog.setTextEchoMode(mode);
    bool accepted = exec(&dialog) == QDialog::Accepted;
    if (ok)
        *ok = accepted;
    return accepted ? dialog.textValue() : QString();
}

} // namespace InWindowDialog
