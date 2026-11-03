// ============ Updated connectiondialog.cpp with Password Field ============
#include "connectiondialog.h"
#include "terminalwindow.h"
#include "inwindowdialog.h"

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
#include <QHeaderView>
#include <QSet>
#include <QEvent>
#include "commandsafety.h"

namespace {
enum TunnelColumn { ColOn, ColType, ColListen, ColHost, ColPort, ColCount };
}

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
    resize(580, 470);
    tunnelsTab = nullptr;
    
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    // Connection details group, split into General and SSH Key tabs
    QGroupBox *detailsGroup = new QGroupBox("Connection Details", this);
    QVBoxLayout *detailsLayout = new QVBoxLayout(detailsGroup);
    
    detailsTabs = new QTabWidget(detailsGroup);
    detailsTabs->addTab(createGeneralTab(), "General");
    detailsTabs->addTab(createSSHKeyTab(), "🔑 SSH Key");
    tunnelsTab = createTunnelsTab();
    detailsTabs->addTab(tunnelsTab, "🔀 Tunnels");
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
    connect(buttonBox, &QDialogButtonBox::accepted, this, &ConnectionDialog::onAccepted);
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
        InWindowDialog::warning(this, "Check Key", "Could not run ssh-keygen. Is OpenSSH installed?");
        return;
    }
    process.waitForFinished(5000);
    
    QString output = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    QString errors = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    
    if (process.exitCode() == 0 && !output.isEmpty()) {
        InWindowDialog::information(this, "Check Key",
            QString("✅ Valid key\n\n%1").arg(output));
    } else {
        InWindowDialog::warning(this, "Check Key",
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
    for (const SSHTunnel &tunnel : connection.tunnels) addTunnelRow(tunnel);
    updateTunnelsTabTitle();
    updateTunnelHint();
    
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
    conn.tunnels = tunnels();
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
        InWindowDialog::warning(this, "Test Connection", "Please enter a host first.");
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
                    InWindowDialog::information(this, "Test Connection", 
                        QString("✅ Host %1 is reachable!\n\n"
                                "Note: This only tests network connectivity.\n"
                                "SSH service availability is not verified.").arg(host));
                } else {
                    InWindowDialog::warning(this, "Test Connection",
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

// ---------------------------------------------------------------- Tunnels tab

QWidget *ConnectionDialog::createTunnelsTab()
{
    QWidget *tab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(tab);

    QLabel *intro = new QLabel("Port forwards start with this connection's SSH tab. "
                               "The destination can be any host the server can reach, "
                               "such as a BMC's own IP.", tab);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    tunnelTable = new QTableWidget(0, ColCount, tab);
    tunnelTable->setHorizontalHeaderLabels({"On", "Type", "Listen port", "Destination host", "Port"});
    tunnelTable->verticalHeader()->setVisible(false);
    tunnelTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    tunnelTable->setSelectionMode(QAbstractItemView::SingleSelection);
    tunnelTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    QHeaderView *header = tunnelTable->horizontalHeader();
    header->setSectionResizeMode(ColOn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColType, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColListen, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColHost, QHeaderView::Stretch);
    header->setSectionResizeMode(ColPort, QHeaderView::ResizeToContents);
    header->setHighlightSections(false);
    tunnelTable->horizontalHeaderItem(ColListen)->setToolTip(
        "Local and SOCKS: the port on this PC. Remote: the port opened on the server.");
    tunnelTable->horizontalHeaderItem(ColHost)->setToolTip(
        "Local: a host the server can reach (localhost is the server itself).\n"
        "Remote: a host this PC can reach (localhost is this PC).");
    tunnelTable->setMinimumHeight(150);
    layout->addWidget(tunnelTable, 1);

    QHBoxLayout *buttons = new QHBoxLayout();
    QPushButton *addButton = new QPushButton("➕ Add Forward", tab);
    addButton->setToolTip("Add a port forward, such as localhost:8443 to a BMC's 443");
    QPushButton *addSocksButton = new QPushButton("🧦 Add SOCKS Proxy", tab);
    addSocksButton->setToolTip("Add a SOCKS proxy (-D) to reach a whole network through the server");
    removeTunnelButton = new QPushButton("➖ Remove", tab);
    connect(addButton, &QPushButton::clicked, this, &ConnectionDialog::onAddTunnel);
    connect(addSocksButton, &QPushButton::clicked, this, &ConnectionDialog::onAddSocksTunnel);
    connect(removeTunnelButton, &QPushButton::clicked, this, &ConnectionDialog::onRemoveTunnel);
    buttons->addWidget(addButton);
    buttons->addWidget(addSocksButton);
    buttons->addWidget(removeTunnelButton);
    buttons->addStretch();
    layout->addLayout(buttons);

    tunnelHintLabel = new QLabel(tab);
    tunnelHintLabel->setWordWrap(true);
    tunnelHintLabel->setStyleSheet("QLabel { color: #666; font-style: italic; }");
    layout->addWidget(tunnelHintLabel);

    connect(tunnelTable, &QTableWidget::itemSelectionChanged, this, &ConnectionDialog::updateTunnelHint);
    connect(tunnelTable, &QTableWidget::itemChanged, this, &ConnectionDialog::updateTunnelsTabTitle);
    updateTunnelHint();
    return tab;
}

void ConnectionDialog::addTunnelRow(const SSHTunnel &tunnel)
{
    int row = tunnelTable->rowCount();
    tunnelTable->insertRow(row);

    QTableWidgetItem *onItem = new QTableWidgetItem();
    onItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    onItem->setCheckState(tunnel.enabled ? Qt::Checked : Qt::Unchecked);
    onItem->setToolTip("Start this forward with the SSH tab");
    tunnelTable->setItem(row, ColOn, onItem);

    QComboBox *typeCombo = new QComboBox(tunnelTable);
    typeCombo->addItem(SSHTunnel::typeName(SSHTunnel::Local), SSHTunnel::Local);
    typeCombo->addItem(SSHTunnel::typeName(SSHTunnel::Remote), SSHTunnel::Remote);
    typeCombo->addItem(SSHTunnel::typeName(SSHTunnel::Dynamic), SSHTunnel::Dynamic);
    typeCombo->setCurrentIndex(typeCombo->findData(tunnel.type));
    tunnelTable->setCellWidget(row, ColType, typeCombo);

    QSpinBox *listenSpin = new QSpinBox(tunnelTable);
    listenSpin->setRange(1, 65535);
    listenSpin->setValue(tunnel.listenPort);
    tunnelTable->setCellWidget(row, ColListen, listenSpin);

    QLineEdit *hostEdit = new QLineEdit(tunnelTable);
    hostEdit->setText(tunnel.targetHost);
    hostEdit->setPlaceholderText("e.g. localhost or 10.254.0.5");
    tunnelTable->setCellWidget(row, ColHost, hostEdit);

    QSpinBox *portSpin = new QSpinBox(tunnelTable);
    portSpin->setRange(0, 65535);
    portSpin->setSpecialValueText("-");  // Shown for a SOCKS proxy, which has no destination port
    portSpin->setValue(tunnel.type == SSHTunnel::Dynamic ? 0 : tunnel.targetPort);
    tunnelTable->setCellWidget(row, ColPort, portSpin);

    // Clicking into a cell editor selects its row, so the hint and Remove follow it
    auto selectRow = [this, typeCombo]() {
        int r = tunnelRowOf(typeCombo);
        if (r >= 0 && tunnelTable->currentRow() != r) tunnelTable->selectRow(r);
    };
    connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, typeCombo, selectRow]() {
        selectRow();
        updateTunnelRow(tunnelRowOf(typeCombo));
        updateTunnelHint();
    });
    for (QWidget *w : QList<QWidget*>{typeCombo, listenSpin, hostEdit, portSpin}) {
        w->installEventFilter(this);
    }
    connect(listenSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, selectRow]() { selectRow(); updateTunnelHint(); });
    connect(portSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, selectRow]() { selectRow(); updateTunnelHint(); });
    connect(hostEdit, &QLineEdit::textEdited, this, [this, selectRow]() { selectRow(); updateTunnelHint(); });

    updateTunnelRow(row);
    tunnelTable->resizeRowToContents(row);
    updateTunnelsTabTitle();
}

bool ConnectionDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FocusIn) {
        int row = tunnelRowOf(qobject_cast<QWidget*>(watched));
        if (row >= 0 && tunnelTable->currentRow() != row) tunnelTable->selectRow(row);
    }
    return QDialog::eventFilter(watched, event);
}

int ConnectionDialog::tunnelRowOf(QWidget *cellWidget) const
{
    for (int row = 0; row < tunnelTable->rowCount(); ++row) {
        for (int col = ColType; col < ColCount; ++col) {
            if (tunnelTable->cellWidget(row, col) == cellWidget) return row;
        }
    }
    return -1;
}

SSHTunnel ConnectionDialog::tunnelFromRow(int row) const
{
    SSHTunnel tunnel;
    QComboBox *typeCombo = qobject_cast<QComboBox*>(tunnelTable->cellWidget(row, ColType));
    QSpinBox *listenSpin = qobject_cast<QSpinBox*>(tunnelTable->cellWidget(row, ColListen));
    QLineEdit *hostEdit = qobject_cast<QLineEdit*>(tunnelTable->cellWidget(row, ColHost));
    QSpinBox *portSpin = qobject_cast<QSpinBox*>(tunnelTable->cellWidget(row, ColPort));
    QTableWidgetItem *onItem = tunnelTable->item(row, ColOn);
    if (!typeCombo || !listenSpin || !hostEdit || !portSpin || !onItem) return tunnel;

    tunnel.type = static_cast<SSHTunnel::Type>(typeCombo->currentData().toInt());
    tunnel.listenPort = listenSpin->value();
    tunnel.enabled = onItem->checkState() == Qt::Checked;
    if (tunnel.type == SSHTunnel::Dynamic) {
        tunnel.targetHost.clear();
        tunnel.targetPort = 0;
    } else {
        tunnel.targetHost = hostEdit->text().trimmed();
        tunnel.targetPort = portSpin->value();
    }
    return tunnel;
}

QList<SSHTunnel> ConnectionDialog::tunnels() const
{
    QList<SSHTunnel> list;
    for (int row = 0; row < tunnelTable->rowCount(); ++row) list << tunnelFromRow(row);
    return list;
}

// A SOCKS proxy has no destination, so its host and port fields are disabled
void ConnectionDialog::updateTunnelRow(int row)
{
    if (row < 0) return;
    QComboBox *typeCombo = qobject_cast<QComboBox*>(tunnelTable->cellWidget(row, ColType));
    QLineEdit *hostEdit = qobject_cast<QLineEdit*>(tunnelTable->cellWidget(row, ColHost));
    QSpinBox *portSpin = qobject_cast<QSpinBox*>(tunnelTable->cellWidget(row, ColPort));
    if (!typeCombo || !hostEdit || !portSpin) return;

    bool dynamic = typeCombo->currentData().toInt() == SSHTunnel::Dynamic;
    hostEdit->setEnabled(!dynamic);
    portSpin->setEnabled(!dynamic);
    if (dynamic) {
        hostEdit->clear();
        hostEdit->setPlaceholderText("(any host, chosen by the browser)");
        portSpin->setValue(0);
    } else {
        hostEdit->setPlaceholderText("e.g. localhost or 10.254.0.5");
        if (hostEdit->text().trimmed().isEmpty()) hostEdit->setText("localhost");
        if (portSpin->value() == 0) portSpin->setValue(80);
    }
}

void ConnectionDialog::updateTunnelHint()
{
    removeTunnelButton->setEnabled(tunnelTable->currentRow() >= 0);

    int row = tunnelTable->currentRow();
    if (row < 0) {
        tunnelHintLabel->setText(tunnelTable->rowCount() == 0
            ? "No tunnels. Add a forward to reach a web page, database or BMC "
              "that only the server can see."
            : "Select a row to see what it does.");
        tunnelHintLabel->setStyleSheet("QLabel { color: #666; font-style: italic; }");
        return;
    }

    SSHTunnel t = tunnelFromRow(row);
    QString problem = t.problem();
    QString hint;
    if (!problem.isEmpty()) {
        hint = "❌ " + problem.left(1).toUpper() + problem.mid(1) + ".";
    } else if (t.type == SSHTunnel::Local) {
        hint = QString("ssh -L %1: open %2://localhost:%3 on this PC to reach %4:%5 from the server.")
                   .arg(t.spec(), t.looksLikeHttps() ? "https" : "http")
                   .arg(t.listenPort).arg(t.targetHost).arg(t.targetPort);
    } else if (t.type == SSHTunnel::Remote) {
        hint = QString("ssh -R %1: programs on the server connect to localhost:%2 there to reach %3:%4 from this PC.")
                   .arg(t.spec()).arg(t.listenPort).arg(t.targetHost).arg(t.targetPort);
    } else {
        hint = QString("ssh -D %1: set a browser's SOCKS5 proxy to localhost:%1, then open any "
                       "address the server can reach, such as each BMC by its own IP.").arg(t.listenPort);
    }
    if (problem.isEmpty() && t.listensLocally() && t.listenPort < 1024) {
        hint += " Ports below 1024 need admin rights on this PC; pick 1024 or above.";
    }
    tunnelHintLabel->setText(hint);
    tunnelHintLabel->setStyleSheet(problem.isEmpty() ? "QLabel { color: #666; font-style: italic; }"
                                                     : "QLabel { color: #cc0000; }");
}

void ConnectionDialog::updateTunnelsTabTitle()
{
    int on = 0;
    for (int row = 0; row < tunnelTable->rowCount(); ++row) {
        QTableWidgetItem *item = tunnelTable->item(row, ColOn);
        if (item && item->checkState() == Qt::Checked) ++on;
    }
    int index = detailsTabs ? detailsTabs->indexOf(tunnelsTab) : -1;
    if (index >= 0) {
        detailsTabs->setTabText(index, on > 0 ? QString("🔀 Tunnels (%1)").arg(on) : QString("🔀 Tunnels"));
    }
}

// The next free-looking port: one past the highest port already listed
static int nextListenPort(const QList<SSHTunnel> &tunnels, int fallback)
{
    int port = fallback;
    for (const SSHTunnel &t : tunnels) {
        if (t.listensLocally() && t.listenPort >= port) port = t.listenPort + 1;
    }
    return qMin(port, 65535);
}

void ConnectionDialog::onAddTunnel()
{
    SSHTunnel tunnel;
    tunnel.type = SSHTunnel::Local;
    tunnel.listenPort = nextListenPort(tunnels(), 8080);
    tunnel.targetHost = "localhost";
    tunnel.targetPort = 80;
    addTunnelRow(tunnel);
    int row = tunnelTable->rowCount() - 1;
    tunnelTable->selectRow(row);
    if (QWidget *host = tunnelTable->cellWidget(row, ColHost)) host->setFocus();
}

void ConnectionDialog::onAddSocksTunnel()
{
    SSHTunnel tunnel;
    tunnel.type = SSHTunnel::Dynamic;
    tunnel.listenPort = nextListenPort(tunnels(), 1080);
    tunnel.targetHost.clear();
    tunnel.targetPort = 0;
    addTunnelRow(tunnel);
    tunnelTable->selectRow(tunnelTable->rowCount() - 1);
}

void ConnectionDialog::onRemoveTunnel()
{
    int row = tunnelTable->currentRow();
    if (row < 0) return;
    tunnelTable->removeRow(row);
    updateTunnelsTabTitle();
    updateTunnelHint();
}

// Checks the tunnels before saving, so a typo doesn't surface only at login
void ConnectionDialog::onAccepted()
{
    QList<SSHTunnel> list = tunnels();
    QStringList problems;
    QSet<int> localPorts;
    QSet<int> remotePorts;
    for (int i = 0; i < list.size(); ++i) {
        const SSHTunnel &t = list.at(i);
        QString problem = t.problem();
        if (!problem.isEmpty()) {
            problems << QString("Row %1: %2").arg(i + 1).arg(problem);
            continue;
        }
        if (!t.enabled) continue;
        QSet<int> &used = t.listensLocally() ? localPorts : remotePorts;
        if (used.contains(t.listenPort)) {
            problems << QString("Row %1: listen port %2 is already used by another tunnel")
                            .arg(i + 1).arg(t.listenPort);
        }
        used.insert(t.listenPort);
    }

    if (!problems.isEmpty()) {
        detailsTabs->setCurrentWidget(tunnelsTab);
        InWindowDialog::warning(this, "Tunnels",
            "Please fix these tunnels before saving:\n\n" + problems.join("\n"));
        return;
    }
    accept();
}
