#include "AboutDialog.h"

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("About Qt Terminal Example");
    setFixedSize(400, 300);
    setupUI();
}

void AboutDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // App icon (optional) - Only add if icon exists
    QPixmap iconPixmap(":/icons/app_icon.png");
    if (!iconPixmap.isNull()) {
        QLabel* iconLabel = new QLabel;
        iconLabel->setPixmap(iconPixmap.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        iconLabel->setAlignment(Qt::AlignCenter);
        mainLayout->addWidget(iconLabel);
    } else {
        // Alternative: Use a simple text-based icon or skip icon entirely
        QLabel* iconLabel = new QLabel("🖥️📡🌐");  // Terminal emoji as placeholder
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setStyleSheet("font-size: 48px; margin: 10px;");
        mainLayout->addWidget(iconLabel);
    }

    // App name
    QLabel* nameLabel = new QLabel("Qt Terminal Example");
    nameLabel->setAlignment(Qt::AlignCenter);
    nameLabel->setStyleSheet("font-size: 18px; font-weight: bold; margin: 10px;");
    mainLayout->addWidget(nameLabel);

    // Version info
    QLabel* versionLabel = new QLabel("Version 0.6.0");
    versionLabel->setAlignment(Qt::AlignCenter);
    versionLabel->setStyleSheet("font-size: 14px; color: #666;");
    mainLayout->addWidget(versionLabel);

    // Build info
    QLabel* buildLabel = new QLabel(QString("Built on %1").arg(__DATE__));
    buildLabel->setAlignment(Qt::AlignCenter);
    buildLabel->setStyleSheet("font-size: 12px; color: #888;");
    mainLayout->addWidget(buildLabel);

    // Description
    QLabel* descLabel = new QLabel("A terminal emulator example using Qt framework");
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet("margin: 15px; color: #555;");
    mainLayout->addWidget(descLabel);

    mainLayout->addStretch();

    // Close button
    QPushButton* closeButton = new QPushButton("Close");
    closeButton->setFixedWidth(80);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    QHBoxLayout* buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);
    buttonLayout->addStretch();

    mainLayout->addLayout(buttonLayout);
}
