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
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QProcessEnvironment>
#include "commandsafety.h"

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
    resize(440, 380);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    // Connection details group, split into General and SSH Key tabs
    QGroupBox *detailsGroup = new QGroupBox("Connection Details", this);
    QVBoxLayout *detailsLayout = new QVBoxLayout(detailsGroup);
    
    detailsTabs = new QTabWidget(detailsGroup);
    detailsTabs->addTab(createGeneralTab(), "General");
    detailsTabs->addTab(createSSHKeyTab(), "🔑 SSH Key");
    detailsLayout->addWidget(detailsTabs);
    
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

QWidget *ConnectionDialog::createGeneralTab()
{
    QWidget *tab = new QWidget(this);
    QFormLayout *formLayout = new QFormLayout(tab);
    
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
    
    return tab;
}

QWidget *ConnectionDialog::createSSHKeyTab()
{
    QWidget *tab = new QWidget(this);
    QFormLayout *formLayout = new QFormLayout(tab);
    
    useKeyAuthCheck = new QCheckBox("Use SSH key authentication", this);
    useKeyAuthCheck->setToolTip("Log in with a private key file and/or keys held by ssh-agent");
    formLayout->addRow(useKeyAuthCheck);
    
    // Private key file with browse button
    identityFileEdit = new QLineEdit(this);
    identityFileEdit->setPlaceholderText("~/.ssh/id_ed25519 (optional)");
    identityFileEdit->setToolTip("Private key passed to ssh/scp with -i");
    browseIdentityButton = new QPushButton("Browse...", this);
    connect(browseIdentityButton, &QPushButton::clicked, this, &ConnectionDialog::onBrowseIdentityFile);
    
    QHBoxLayout *keyLayout = new QHBoxLayout();
    keyLayout->addWidget(identityFileEdit);
    keyLayout->addWidget(browseIdentityButton);
    formLayout->addRow("Private key:", keyLayout);
    
    identityStatusLabel = new QLabel(this);
    identityStatusLabel->setWordWrap(true);
    formLayout->addRow("", identityStatusLabel);
    
    checkIdentityButton = new QPushButton("🔍 Check Key", this);
    checkIdentityButton->setToolTip("Show the key's type and fingerprint (ssh-keygen -l)");
    connect(checkIdentityButton, &QPushButton::clicked, this, &ConnectionDialog::onCheckIdentityFile);
    QHBoxLayout *checkLayout = new QHBoxLayout();
    checkLayout->addWidget(checkIdentityButton);
    checkLayout->addStretch();
    formLayout->addRow("", checkLayout);
    
    // SSH agent
    useAgentCheck = new QCheckBox("Use SSH agent (ssh-agent)", this);
    useAgentCheck->setChecked(true);
    useAgentCheck->setToolTip("When unchecked, keys held by ssh-agent are ignored (IdentityAgent=none)");
    formLayout->addRow(useAgentCheck);
    
    agentStatusLabel = new QLabel(this);
    agentStatusLabel->setWordWrap(true);
    formLayout->addRow("", agentStatusLabel);
    
    QLabel *noteLabel = new QLabel("A saved password is still tried if the key is not accepted.", this);
    noteLabel->setWordWrap(true);
    noteLabel->setStyleSheet("QLabel { color: #666; font-style: italic; }");
    formLayout->addRow(noteLabel);
    
    connect(useKeyAuthCheck, &QCheckBox::toggled, this, &ConnectionDialog::updateKeyAuthState);
    connect(useAgentCheck, &QCheckBox::toggled, this, &ConnectionDialog::updateKeyAuthState);
    connect(identityFileEdit, &QLineEdit::textChanged, this, &ConnectionDialog::updateKeyAuthState);
    
    updateKeyAuthState();
    return tab;
}

void ConnectionDialog::updateKeyAuthState()
{
    bool enabled = useKeyAuthCheck->isChecked();
    identityFileEdit->setEnabled(enabled);
    browseIdentityButton->setEnabled(enabled);
    useAgentCheck->setEnabled(enabled);
    
    QString keyFile = CommandSafetyHelper::expandHomePath(identityFileEdit->text());
    checkIdentityButton->setEnabled(enabled && !keyFile.isEmpty());
    
    // Key file status
    QString keyStatus;
    QString keyStyle = "QLabel { color: #666; }";
    if (keyFile.isEmpty()) {
        keyStatus = "No key file: ssh uses the agent and its default keys (~/.ssh/id_*).";
    } else {
        QFileInfo info(keyFile);
        if (!info.exists()) {
            keyStatus = "❌ File not found";
            keyStyle = "QLabel { color: #cc0000; }";
        } else if (!info.isFile() || !info.isReadable()) {
            keyStatus = "❌ File is not readable";
            keyStyle = "QLabel { color: #cc0000; }";
        } else if (info.permissions() & (QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                                         QFileDevice::ReadOther | QFileDevice::WriteOther)) {
            // OpenSSH refuses private keys that other users can read
            keyStatus = QString("⚠️ Permissions too open; ssh will ignore this key. Run: chmod 600 %1")
                        .arg(CommandSafetyHelper::escapeShellArgument(keyFile));
            keyStyle = "QLabel { color: #b36b00; }";
        } else if (keyFile.endsWith(".pub")) {
            keyStatus = "⚠️ This looks like a public key; choose the private key file";
            keyStyle = "QLabel { color: #b36b00; }";
        } else {
            keyStatus = "✅ Key file found";
            keyStyle = "QLabel { color: #2e7d32; }";
        }
    }
    identityStatusLabel->setText(keyStatus);
    identityStatusLabel->setStyleSheet(enabled ? keyStyle : "QLabel { color: #999; }");
    
    // Agent status
    QString agentSocket = QProcessEnvironment::systemEnvironment().value("SSH_AUTH_SOCK");
    QString agentStatus;
    if (!useAgentCheck->isChecked()) {
        agentStatus = keyFile.isEmpty()
            ? "⚠️ Agent disabled and no key file set: only ssh's default key files are offered."
            : "Agent disabled: only the key file above is offered.";
    } else if (agentSocket.isEmpty()) {
        agentStatus = "No running agent detected (SSH_AUTH_SOCK is not set).";
    } else {
        agentStatus = "✅ Agent detected: " + agentSocket;
    }
    agentStatusLabel->setText(agentStatus);
    agentStatusLabel->setStyleSheet(enabled ? "QLabel { color: #666; }" : "QLabel { color: #999; }");
}

void ConnectionDialog::onBrowseIdentityFile()
{
    QString startDir = CommandSafetyHelper::expandHomePath(identityFileEdit->text());
    if (startDir.isEmpty()) {
        startDir = QDir::homePath() + "/.ssh";
    }
    
    QString file = QFileDialog::getOpenFileName(this, "Select Private Key", startDir,
                                                "All files (*)", nullptr,
                                                QFileDialog::DontResolveSymlinks);
    if (file.isEmpty()) return;
    
    // Store paths under the home directory as ~/... so they survive a home move
    QString home = QDir::homePath();
    if (file.startsWith(home + "/")) {
        file = "~" + file.mid(home.length());
    }
    identityFileEdit->setText(file);
}

void ConnectionDialog::onCheckIdentityFile()
{
    QString keyFile = CommandSafetyHelper::expandHomePath(identityFileEdit->text());
    if (keyFile.isEmpty()) return;
    
    QProcess process;
    process.start("ssh-keygen", QStringList() << "-l" << "-f" << keyFile);
    if (!process.waitForStarted(3000)) {
        QMessageBox::warning(this, "Check Key", "Could not run ssh-keygen. Is OpenSSH installed?");
        return;
    }
    process.waitForFinished(5000);
    
    QString output = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    QString errors = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    
    if (process.exitCode() == 0 && !output.isEmpty()) {
        QMessageBox::information(this, "Check Key",
            QString("✅ Valid key\n\n%1").arg(output));
    } else {
        QMessageBox::warning(this, "Check Key",
            QString("❌ Not a usable SSH key:\n\n%1").arg(errors.isEmpty() ? keyFile : errors));
    }
}

void ConnectionDialog::populateFields(const SSHConnection &connection)
{
    nameEdit->setText(connection.name);
    hostEdit->setText(connection.host);
    usernameEdit->setText(connection.username);
    passwordEdit->setText(connection.password);
    portSpinBox->setValue(connection.port);
    useKeyAuthCheck->setChecked(connection.useKeyAuth);
    identityFileEdit->setText(connection.identityFile);
    useAgentCheck->setChecked(connection.useAgent);
    
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
    conn.useKeyAuth = useKeyAuthCheck->isChecked();
    conn.identityFile = identityFileEdit->text().trimmed();
    conn.useAgent = useAgentCheck->isChecked();
    return conn;
}

void ConnectionDialog::setAvailableFolders(const QStringList &folders)
{
    // Repopulating the combo resets its selection, so keep the folder
    // that was already chosen (the edited connection's, or the default).
    const QString current = folderCombo->currentText();
    folderCombo->clear();
    folderCombo->addItems(folders);
    if (!current.isEmpty() && folderCombo->findText(current) == -1) {
        folderCombo->addItem(current);
    }
    folderCombo->setCurrentText(current);
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