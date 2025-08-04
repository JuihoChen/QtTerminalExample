// ============ Updated connectiondialog.cpp with Password Field ============
#include "connectiondialog.h"
#include "terminalwindow.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QTimer>
#include <QApplication>
#include <QProgressDialog>
#include <QCheckBox>

ConnectionDialog::ConnectionDialog(QWidget *parent)
    : QDialog(parent), editMode(false)
{
    setWindowTitle("New SSH Connection");
    setupUI();
    
    // Set default values for new connection
    portSpinBox->setValue(22);
    folderCombo->setCurrentText("Personal");
}

ConnectionDialog::ConnectionDialog(const SSHConnection &connection, QWidget *parent)
    : QDialog(parent), editMode(true)
{
    setWindowTitle("Edit SSH Connection");
    setupUI();
    populateFields(connection);
}

void ConnectionDialog::setupUI()
{
    setModal(true);
    resize(400, 350); // Increased height for password field
    
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    // Connection details group
    QGroupBox *detailsGroup = new QGroupBox("Connection Details", this);
    QFormLayout *formLayout = new QFormLayout(detailsGroup);
    
    // Name field
    nameEdit = new QLineEdit(this);
    nameEdit->setPlaceholderText("e.g., Web Server, Database, etc.");
    formLayout->addRow("Name:", nameEdit);
    
    // Host field
    hostEdit = new QLineEdit(this);
    hostEdit->setPlaceholderText("hostname or IP address");
    formLayout->addRow("Host:", hostEdit);
    
    // Username field
    usernameEdit = new QLineEdit(this);
    usernameEdit->setPlaceholderText("SSH username");
    formLayout->addRow("Username:", usernameEdit);
    
    // Password field
    passwordEdit = new QLineEdit(this);
    passwordEdit->setPlaceholderText("SSH password (optional)");
    passwordEdit->setEchoMode(QLineEdit::Password);
    formLayout->addRow("Password:", passwordEdit);
    
    // Show password checkbox
    showPasswordCheck = new QCheckBox("Show password", this);
    connect(showPasswordCheck, &QCheckBox::toggled, this, &ConnectionDialog::onShowPasswordChanged);
    formLayout->addRow("", showPasswordCheck);
    
    // Port field
    portSpinBox = new QSpinBox(this);
    portSpinBox->setRange(1, 65535);
    portSpinBox->setValue(22);
    formLayout->addRow("Port:", portSpinBox);
    
    // Folder field
    folderCombo = new QComboBox(this);
    folderCombo->setEditable(true);
    folderCombo->addItems({"Production", "Development", "Personal", "Testing", "Staging"});
    formLayout->addRow("Folder:", folderCombo);
    
    mainLayout->addWidget(detailsGroup);
    
    // Test connection button
    QHBoxLayout *testLayout = new QHBoxLayout();
    testButton = new QPushButton("🔍 Test Connection", this);
    testButton->setToolTip("Test if the host is reachable (ping test)");
    connect(testButton, &QPushButton::clicked, this, &ConnectionDialog::onTestConnection);
    testLayout->addWidget(testButton);
    testLayout->addStretch();
    
    mainLayout->addLayout(testLayout);
    
    // Dialog buttons
    buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    
    mainLayout->addWidget(buttonBox);
    
    // Connect validation
    connect(nameEdit, &QLineEdit::textChanged, this, &ConnectionDialog::validateInput);
    connect(hostEdit, &QLineEdit::textChanged, this, &ConnectionDialog::validateInput);
    connect(usernameEdit, &QLineEdit::textChanged, this, &ConnectionDialog::validateInput);
    // Note: password is optional, so no validation needed
    
    // Initial validation
    validateInput();
    
    // Focus on name field
    nameEdit->setFocus();
}

void ConnectionDialog::populateFields(const SSHConnection &connection)
{
    nameEdit->setText(connection.name);
    hostEdit->setText(connection.host);
    usernameEdit->setText(connection.username);
    passwordEdit->setText(connection.password);
    portSpinBox->setValue(connection.port);
    
    // Set folder, add it if it doesn't exist
    if (folderCombo->findText(connection.folder) == -1 && !connection.folder.isEmpty()) {
        folderCombo->addItem(connection.folder);
    }
    folderCombo->setCurrentText(connection.folder);
}

SSHConnection ConnectionDialog::getConnection() const
{
    SSHConnection conn;
    conn.name = nameEdit->text().trimmed();
    conn.host = hostEdit->text().trimmed();
    conn.username = usernameEdit->text().trimmed();
    conn.password = passwordEdit->text(); // Don't trim password (spaces might be significant)
    conn.port = portSpinBox->value();
    conn.folder = folderCombo->currentText().trimmed();
    return conn;
}

void ConnectionDialog::setAvailableFolders(const QStringList &folders)
{
    folderCombo->clear();
    folderCombo->addItems(folders);
}

void ConnectionDialog::validateInput()
{
    bool valid = !nameEdit->text().trimmed().isEmpty() &&
                 !hostEdit->text().trimmed().isEmpty() &&
                 !usernameEdit->text().trimmed().isEmpty();
    // Note: password is optional for SSH connections (might use keys)
    
    buttonBox->button(QDialogButtonBox::Ok)->setEnabled(valid);
    
    // Update test button state
    testButton->setEnabled(!hostEdit->text().trimmed().isEmpty());
}

void ConnectionDialog::onShowPasswordChanged(bool show)
{
    passwordEdit->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
}

void ConnectionDialog::onTestConnection()
{
    QString host = hostEdit->text().trimmed();
    if (host.isEmpty()) {
        QMessageBox::warning(this, "Test Connection", "Please enter a host first.");
        return;
    }
    
    // Create progress dialog
    QProgressDialog *progress = new QProgressDialog("Testing connection to " + host + "...", "Cancel", 0, 0, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(500);
    progress->show();
    
    // Create ping process
    QProcess *pingProcess = new QProcess(this);
    
    // Connect signals
    connect(pingProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, progress, host](int exitCode, QProcess::ExitStatus exitStatus) {
                Q_UNUSED(exitStatus)
                progress->close();
                progress->deleteLater();
                
                if (exitCode == 0) {
                    QMessageBox::information(this, "Test Connection", 
                        QString("✅ Host %1 is reachable!\n\n"
                                "Note: This only tests network connectivity.\n"
                                "SSH service availability is not verified.").arg(host));
                } else {
                    QMessageBox::warning(this, "Test Connection",
                        QString("❌ Host %1 is not reachable.\n\n"
                                "Please check:\n"
                                "• Host address is correct\n"
                                "• Network connectivity\n"
                                "• Firewall settings").arg(host));
                }
                
                sender()->deleteLater();
            });
    
    connect(progress, &QProgressDialog::canceled, [pingProcess]() {
        pingProcess->kill();
    });
    
    // Start ping (works on both Linux and Windows)
#ifdef Q_OS_WIN
    pingProcess->start("ping", QStringList() << "-n" << "3" << host);
#else
    pingProcess->start("ping", QStringList() << "-c" << "3" << "-W" << "3" << host);
#endif
    
    // Set timeout
    QTimer::singleShot(10000, pingProcess, &QProcess::kill); // 10 second timeout
}