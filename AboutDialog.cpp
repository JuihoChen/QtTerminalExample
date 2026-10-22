#include "AboutDialog.h"
#include "buildinfo.h"

#include <QApplication>
#include <QEventLoop>
#include <QFrame>
#include <QIcon>
#include <QPointer>

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("About Qt Terminal Example");
    setupUI();
}

void AboutDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    mainLayout->setContentsMargins(28, 24, 28, 18);
    mainLayout->setSpacing(4);

    // App icon, the same one the window and taskbar use (set in main.cpp)
    QLabel* iconLabel = new QLabel;
    iconLabel->setPixmap(QApplication::windowIcon().pixmap(80, 80));
    iconLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(iconLabel);
    mainLayout->addSpacing(6);

    // App name
    QLabel* nameLabel = new QLabel("Qt Terminal Example");
    nameLabel->setAlignment(Qt::AlignCenter);
    nameLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
    mainLayout->addWidget(nameLabel);

    // Version, with the git commit so you can tell exactly which build runs
    QString version = QStringLiteral("Version " APP_VERSION);
    if (*BuildInfo::gitCommit)
        version += QString(" (%1)").arg(BuildInfo::gitCommit);
    QLabel* versionLabel = new QLabel(version);
    versionLabel->setAlignment(Qt::AlignCenter);
    versionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    versionLabel->setStyleSheet("font-size: 14px; color: #555;");
    mainLayout->addWidget(versionLabel);

    // Build time and the Qt version actually loaded
    QLabel* buildLabel = new QLabel(QString("Built %1 · Qt %2")
                                        .arg(BuildInfo::buildTime, qVersion()));
    buildLabel->setAlignment(Qt::AlignCenter);
    buildLabel->setStyleSheet("font-size: 12px; color: #888;");
    mainLayout->addWidget(buildLabel);
    mainLayout->addSpacing(12);

    // Description
    QLabel* descLabel = new QLabel("SSH terminal with file browser and host scanning");
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet("color: #333;");
    mainLayout->addWidget(descLabel);
    mainLayout->addSpacing(12);

    // A few handy shortcuts; the README has the full list
    QFrame* shortcutsFrame = new QFrame;
    shortcutsFrame->setObjectName("shortcuts");
    shortcutsFrame->setStyleSheet("#shortcuts { background: rgba(0, 0, 0, 0.04);"
                                  " border: 1px solid rgba(0, 0, 0, 0.10); border-radius: 6px; }");
    QGridLayout* shortcutsLayout = new QGridLayout(shortcutsFrame);
    shortcutsLayout->setContentsMargins(14, 10, 14, 10);
    shortcutsLayout->setHorizontalSpacing(18);
    shortcutsLayout->setVerticalSpacing(4);
    const QList<QPair<QString, QString>> shortcuts = {
        {"Ctrl+Shift+B", "Show or hide the file browser"},
        {"Ctrl+Alt+H",   "Show or hide hidden files"},
        {"F3 / F4",      "View / edit a file in the browser"},
        {"F5",           "Refresh connections"},
        {"Ctrl+W",       "Close tab"},
    };
    int row = 0;
    for (const auto& shortcut : shortcuts) {
        QLabel* keys = new QLabel(shortcut.first);
        keys->setStyleSheet("font-weight: bold; color: #333;");
        QLabel* action = new QLabel(shortcut.second);
        action->setStyleSheet("color: #555;");
        shortcutsLayout->addWidget(keys, row, 0);
        shortcutsLayout->addWidget(action, row, 1);
        ++row;
    }
    mainLayout->addWidget(shortcutsFrame);
    mainLayout->addSpacing(10);

    // Project link
    QLabel* linkLabel = new QLabel(
        "<a href=\"https://github.com/JuihoChen/QtTerminalExample\">"
        "github.com/JuihoChen/QtTerminalExample</a>");
    linkLabel->setAlignment(Qt::AlignCenter);
    linkLabel->setOpenExternalLinks(true);
    linkLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    mainLayout->addWidget(linkLabel);
    mainLayout->addSpacing(14);

    // Close button
    m_closeButton = new QPushButton("Close");
    m_closeButton->setFixedWidth(80);
    m_closeButton->setDefault(true);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);

    QHBoxLayout* buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_closeButton);
    buttonLayout->addStretch();

    mainLayout->addLayout(buttonLayout);
}

void AboutDialog::showFor(QWidget *window) {
    AboutDialog dialog(window);
    // WSLg turns each Wayland window into a separate Windows window and
    // doesn't keep a dialog above the window that opened it, so clicking the
    // main window hides the dialog behind it. Show it inside the main window
    // there instead; everywhere else it stays a normal dialog window.
    bool wslg = QGuiApplication::platformName() == "wayland"
                && !qEnvironmentVariableIsEmpty("WSL_DISTRO_NAME");
    if (wslg && window)
        dialog.execInside(window->window());
    else
        dialog.exec();
}

void AboutDialog::execInside(QWidget *window) {
    QPointer<QWidget> previousFocus = window->focusWidget();

    // A dimmed backdrop over the whole window takes the clicks meant for it
    QWidget backdrop(window);
    backdrop.setObjectName("aboutBackdrop");
    backdrop.setAttribute(Qt::WA_StyledBackground);
    backdrop.setStyleSheet("#aboutBackdrop { background: rgba(0, 0, 0, 90); }");
    backdrop.setGeometry(window->rect());

    setParent(&backdrop, Qt::Widget);
    setObjectName("aboutPanel");
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet("#aboutPanel { background: palette(window);"
                  " border: 1px solid rgba(0, 0, 0, 0.35); border-radius: 8px; }");
    m_host = window;
    m_backdrop = &backdrop;
    adjustSize();
    centerInBackdrop();

    backdrop.show();
    backdrop.raise();
    show();
    m_closeButton->setFocus();

    // Keys and shortcuts meant for the main window are dropped while open
    qApp->installEventFilter(this);
    QEventLoop loop;
    connect(this, &QDialog::finished, &loop, &QEventLoop::quit);
    loop.exec(QEventLoop::DialogExec);
    qApp->removeEventFilter(this);

    // The backdrop is about to be destroyed; it must not take this dialog along
    hide();
    setParent(nullptr);
    m_host = nullptr;
    m_backdrop = nullptr;
    if (previousFocus)
        previousFocus->setFocus();
}

void AboutDialog::centerInBackdrop() {
    move((m_backdrop->width() - width()) / 2, (m_backdrop->height() - height()) / 2);
}

bool AboutDialog::isOwnWidget(QObject *object) const {
    QWidget *widget = qobject_cast<QWidget*>(object);
    return widget && (widget == this || isAncestorOf(widget));
}

bool AboutDialog::eventFilter(QObject *watched, QEvent *event) {
    switch (event->type()) {
    case QEvent::Resize:
        if (watched == m_host) {
            m_backdrop->setGeometry(m_host->rect());
            centerInBackdrop();
        }
        break;
    case QEvent::Shortcut:
        return true;
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::ShortcutOverride:
        // Key events reach the QWindow first and then the focused widget;
        // only drop the ones addressed to widgets outside this panel
        if (qobject_cast<QWidget*>(watched) && !isOwnWidget(watched))
            return true;
        break;
    case QEvent::FocusIn:
        if (qobject_cast<QWidget*>(watched) && !isOwnWidget(watched))
            m_closeButton->setFocus();
        break;
    default:
        break;
    }
    return QDialog::eventFilter(watched, event);
}
