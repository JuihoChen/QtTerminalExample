#include "AboutDialog.h"
#include "buildinfo.h"
#include "inwindowdialog.h"

#include <QApplication>
#include <QDesktopServices>
#include <QFrame>
#include <QIcon>
#include <QProcess>
#include <QUrl>

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
    // WSL usually has no Linux browser for xdg-open to start, so hand the
    // link to Windows there; elsewhere use the desktop's default browser
    connect(linkLabel, &QLabel::linkActivated, this, [](const QString &link) {
        if (!qEnvironmentVariableIsEmpty("WSL_DISTRO_NAME")
            && QProcess::startDetached("explorer.exe", {link}))
            return;
        QDesktopServices::openUrl(QUrl(link));
    });
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
    // The dialog names itself, so a panel under WSLg needs no title row
    InWindowDialog::exec(&dialog, false);
}
