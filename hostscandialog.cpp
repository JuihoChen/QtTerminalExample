// SSH host scanner: find hosts with SSH open in an IPv4 range and import them
#include "hostscandialog.h"
#include "commandsafety.h"
#include "sftpworker.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

const quint32 kMaxAddresses = 4096;  // A /20; larger ranges take too long
const int kParallelProbes = 64;

enum Column { IpColumn, MacColumn, SshColumn };

// Sorts the IP column by address rather than as text
class ResultItem : public QTreeWidgetItem
{
public:
    explicit ResultItem(QTreeWidget *parent) : QTreeWidgetItem(parent) {}
    bool operator<(const QTreeWidgetItem &other) const override
    {
        int column = treeWidget() ? treeWidget()->sortColumn() : IpColumn;
        if (column == IpColumn) {
            return data(IpColumn, Qt::UserRole).toUInt() < other.data(IpColumn, Qt::UserRole).toUInt();
        }
        return QTreeWidgetItem::operator<(other);
    }
};

} // namespace

HostScanDialog::HostScanDialog(const QList<SSHConnection> &connections, const QStringList &folders,
                               QWidget *parent)
    : QDialog(parent), m_connections(connections), m_rangeEdited(false),
      m_process(nullptr), m_workerThread(nullptr), m_worker(nullptr),
      m_scanning(false), m_stopRequested(false), m_section(Probes),
      m_rangeStart(0), m_rangeEnd(0), m_scanPort(22), m_done(0),
      m_noPortCheck(false), m_noPing(false)
{
    setWindowTitle("Scan for SSH Hosts");
    resize(680, 700);

    for (const SSHConnection &conn : connections) {
        m_savedHosts.insert(conn.host.trimmed().toLower());
    }

    QVBoxLayout *layout = new QVBoxLayout(this);

    // Range and options
    QGroupBox *rangeGroup = new QGroupBox("Scan", this);
    QFormLayout *form = new QFormLayout(rangeGroup);

    QRegularExpression ipPattern("^[0-9.]{0,15}$");
    m_startEdit = new QLineEdit(this);
    m_startEdit->setValidator(new QRegularExpressionValidator(ipPattern, this));
    m_startEdit->setPlaceholderText("192.168.1.1");
    m_endEdit = new QLineEdit(this);
    m_endEdit->setValidator(new QRegularExpressionValidator(ipPattern, this));
    m_endEdit->setPlaceholderText("192.168.1.254");
    connect(m_startEdit, &QLineEdit::textEdited, this, [this]() { m_rangeEdited = true; });
    connect(m_endEdit, &QLineEdit::textEdited, this, [this]() { m_rangeEdited = true; });
    QHBoxLayout *rangeRow = new QHBoxLayout();
    rangeRow->addWidget(m_startEdit);
    rangeRow->addWidget(new QLabel("to", this));
    rangeRow->addWidget(m_endEdit);
    form->addRow("IP range:", rangeRow);

    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(22);
    m_timeoutSpin = new QSpinBox(this);
    m_timeoutSpin->setRange(1, 10);
    m_timeoutSpin->setValue(1);
    m_timeoutSpin->setSuffix(" s");
    QHBoxLayout *optionsRow = new QHBoxLayout();
    optionsRow->addWidget(m_portSpin);
    optionsRow->addSpacing(16);
    optionsRow->addWidget(new QLabel("Timeout:", this));
    optionsRow->addWidget(m_timeoutSpin);
    optionsRow->addStretch();
    form->addRow("SSH port:", optionsRow);

    m_scanFromCombo = new QComboBox(this);
    m_scanFromCombo->addItem("This computer", -1);
    for (int i = 0; i < connections.size(); ++i) {
        const SSHConnection &conn = connections[i];
        QString label = QString("%1 (%2@%3)").arg(conn.name, conn.username, conn.host);
        if (!conn.folder.isEmpty()) label = conn.folder + " / " + label;
        m_scanFromCombo->addItem("Through " + label, i);
    }
    connect(m_scanFromCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &HostScanDialog::onScanFromChanged);
    form->addRow("Scan from:", m_scanFromCombo);

    m_scanFromNote = new QLabel(this);
    m_scanFromNote->setWordWrap(true);
    m_scanFromNote->setStyleSheet("QLabel { color: #666; }");
    form->addRow("", m_scanFromNote);

    QHBoxLayout *scanRow = new QHBoxLayout();
    m_progress = new QProgressBar(this);
    m_progress->setTextVisible(false);
    m_scanButton = new QPushButton("🔍 Scan", this);
    m_scanButton->setDefault(true);
    m_stopButton = new QPushButton("Stop", this);
    m_stopButton->setEnabled(false);
    connect(m_scanButton, &QPushButton::clicked, this, &HostScanDialog::startScan);
    connect(m_stopButton, &QPushButton::clicked, this, &HostScanDialog::stopScan);
    scanRow->addWidget(m_progress, 1);
    scanRow->addWidget(m_scanButton);
    scanRow->addWidget(m_stopButton);
    form->addRow(scanRow);
    layout->addWidget(rangeGroup);

    // Results
    m_results = new QTreeWidget(this);
    m_results->setHeaderLabels(QStringList() << "IP address" << "MAC address" << "SSH open");
    m_results->setRootIsDecorated(false);
    m_results->setSortingEnabled(true);
    m_results->sortByColumn(IpColumn, Qt::AscendingOrder);
    m_results->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_results->header()->setSectionResizeMode(IpColumn, QHeaderView::ResizeToContents);
    m_results->header()->setSectionResizeMode(MacColumn, QHeaderView::ResizeToContents);
    m_results->setColumnWidth(IpColumn, 160);
    connect(m_results, &QTreeWidget::itemChanged, this, &HostScanDialog::updateImportButton);
    layout->addWidget(m_results, 1);

    m_statusLabel = new QLabel("Hosts that answer a ping, have the port open, or appear in the "
                               "ARP table are listed.", this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    // Import
    QGroupBox *importGroup = new QGroupBox("Import checked hosts as SSH connections", this);
    QFormLayout *importForm = new QFormLayout(importGroup);
    m_folderCombo = new QComboBox(this);
    m_folderCombo->setEditable(true);
    m_folderCombo->addItems(folders);
    importForm->addRow("Folder:", m_folderCombo);
    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setPlaceholderText("SSH username for the new connections");
    QByteArray user = qgetenv("USER");
    if (user.isEmpty()) user = qgetenv("LOGNAME");
    m_usernameEdit->setText(QString::fromLocal8Bit(user));
    connect(m_usernameEdit, &QLineEdit::textChanged, this, &HostScanDialog::updateImportButton);
    importForm->addRow("Username:", m_usernameEdit);
    m_importButton = new QPushButton("Import", this);
    m_importButton->setEnabled(false);
    connect(m_importButton, &QPushButton::clicked, this, &HostScanDialog::importChecked);
    importForm->addRow("", m_importButton);
    layout->addWidget(importGroup);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &HostScanDialog::reject);
    layout->addWidget(buttons);

    onScanFromChanged();
}

HostScanDialog::~HostScanDialog()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    stopRemoteWorker();
}

void HostScanDialog::setScanFrom(int connectionIndex)
{
    int comboIndex = m_scanFromCombo->findData(connectionIndex);
    if (comboIndex >= 0) m_scanFromCombo->setCurrentIndex(comboIndex);
}

void HostScanDialog::setImportFolder(const QString &folder)
{
    if (folder.isEmpty()) return;
    if (m_folderCombo->findText(folder) < 0) m_folderCombo->addItem(folder);
    m_folderCombo->setCurrentText(folder);
}

const SSHConnection *HostScanDialog::scanFromConnection() const
{
    int index = m_scanFromCombo->currentData().toInt();
    return index >= 0 && index < m_connections.size() ? &m_connections[index] : nullptr;
}

void HostScanDialog::onScanFromChanged()
{
    const SSHConnection *via = scanFromConnection();
    if (via) {
        m_scanFromNote->setText(QString("Runs ping, a port check and the ARP table on %1, so it "
                                        "finds hosts that only %1 can reach. MAC addresses show "
                                        "for hosts on its own subnet. Imported connections still "
                                        "connect directly from this computer.").arg(via->host));
        if (!via->username.isEmpty()) m_usernameEdit->setText(via->username);
    } else {
        m_scanFromNote->setText("MAC addresses show for hosts on this computer's own subnet.");
    }
    if (!m_rangeEdited) setDefaultRange();
}

// Suggests the /24 around the scanning host
void HostScanDialog::setDefaultRange()
{
    quint32 base = 0;
    const SSHConnection *via = scanFromConnection();
    if (via) {
        if (!parseIPv4(via->host.trimmed(), &base)) return;  // A host name: leave it to the user
    } else {
        for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
            if (!(iface.flags() & QNetworkInterface::IsUp) || (iface.flags() & QNetworkInterface::IsLoopBack)) {
                continue;
            }
            for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                    base = entry.ip().toIPv4Address();
                    break;
                }
            }
            if (base) break;
        }
        if (!base) return;
    }
    base &= 0xFFFFFF00u;
    m_startEdit->setText(formatIPv4(base | 1));
    m_endEdit->setText(formatIPv4(base | 254));
}

bool HostScanDialog::parseIPv4(const QString &text, quint32 *address)
{
    QStringList parts = text.split('.');
    if (parts.size() != 4) return false;
    quint32 value = 0;
    for (const QString &part : parts) {
        bool ok = false;
        uint octet = part.toUInt(&ok);
        if (!ok || part.isEmpty() || part.size() > 3 || octet > 255) return false;
        value = (value << 8) | octet;
    }
    *address = value;
    return true;
}

QString HostScanDialog::formatIPv4(quint32 address)
{
    return QString("%1.%2.%3.%4").arg(address >> 24).arg((address >> 16) & 255)
                                  .arg((address >> 8) & 255).arg(address & 255);
}

// Prints "UP <ip>" for a ping reply, "OPEN <ip>" when the port accepts a TCP
// connection and "DONE <ip>" when a host is finished, then the ARP table
// after "ARP" and the scanning host's own addresses after "SELF". Probing
// fills the ARP table, so it is read last. Uses bash's /dev/tcp with
// coreutils timeout for the port check, or nc when those are missing.
QString HostScanDialog::scanScript(quint32 start, quint32 end, int port, int timeoutSeconds)
{
    QString script = QString(
        "S=%1 E=%2 P=%3 T=%4 J=%5\n"
        "B=$(command -v bash 2>/dev/null); command -v timeout >/dev/null 2>&1 || B=\n"
        "NC=$(command -v nc 2>/dev/null)\n"
        "[ -n \"$B$NC\" ] || echo NOPORTCHECK\n"
        "command -v ping >/dev/null 2>&1 || echo NOPING\n"
        "probe() {\n"
        "  ping -c 1 -W \"$T\" \"$1\" >/dev/null 2>&1 && echo \"UP $1\"\n"
        "  if [ -n \"$B\" ]; then\n"
        "    timeout \"$T\" \"$B\" -c 'exec 3<>\"/dev/tcp/$0/$1\"' \"$1\" \"$P\" >/dev/null 2>&1 && echo \"OPEN $1\"\n"
        "  elif [ -n \"$NC\" ]; then\n"
        "    nc -z -w \"$T\" \"$1\" \"$P\" >/dev/null 2>&1 && echo \"OPEN $1\"\n"
        "  fi\n"
        "  echo \"DONE $1\"\n"
        "}\n"
        "i=$S; n=0\n"
        "while [ \"$i\" -le \"$E\" ]; do\n"
        "  probe \"$((i>>24&255)).$((i>>16&255)).$((i>>8&255)).$((i&255))\" &\n"
        "  n=$((n+1)); [ $((n%J)) -eq 0 ] && wait\n"
        "  i=$((i+1))\n"
        "done\n"
        "wait\n"
        "echo ARP\n"
        "cat /proc/net/arp 2>/dev/null\n"
        "echo SELF\n"
        "if command -v ip >/dev/null 2>&1; then\n"
        "  ip -o -4 addr show 2>/dev/null | while read -r _ dev _ addr _; do\n"
        "    echo \"${addr%/*} $(cat \"/sys/class/net/$dev/address\" 2>/dev/null)\"\n"
        "  done\n"
        "fi\n")
        .arg(start).arg(end).arg(port).arg(timeoutSeconds).arg(kParallelProbes);
    return script;
}

void HostScanDialog::startScan()
{
    quint32 start = 0, end = 0;
    if (!parseIPv4(m_startEdit->text().trimmed(), &start)) {
        QMessageBox::warning(this, "Scan", "Enter a start IP address such as 192.168.1.1.");
        m_startEdit->setFocus();
        return;
    }
    if (!parseIPv4(m_endEdit->text().trimmed(), &end)) {
        QMessageBox::warning(this, "Scan", "Enter an end IP address such as 192.168.1.254.");
        m_endEdit->setFocus();
        return;
    }
    if (end < start) qSwap(start, end);
    if (end - start >= kMaxAddresses) {
        QMessageBox::warning(this, "Scan",
            QString("That range has %1 addresses. Scan at most %2 at a time (a /20).")
                .arg(qulonglong(end) - start + 1).arg(kMaxAddresses));
        return;
    }

    m_results->clear();
    m_rows.clear();
    m_macs.clear();
    m_pending.clear();
    m_section = Probes;
    m_rangeStart = start;
    m_rangeEnd = end;
    m_scanPort = m_portSpin->value();
    m_done = 0;
    m_noPortCheck = false;
    m_noPing = false;
    m_stopRequested = false;
    m_progress->setRange(0, int(end - start + 1));
    m_progress->setValue(0);
    m_results->headerItem()->setText(SshColumn, QString("SSH open (port %1)").arg(m_scanPort));
    setScanning(true);

    QString script = scanScript(start, end, m_scanPort, m_timeoutSpin->value());
    const SSHConnection *via = scanFromConnection();

    if (!via) {
        m_statusLabel->setText(QString("Scanning %1 addresses...").arg(end - start + 1));
        m_process = new QProcess(this);
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_process, &QProcess::readyRead, this, [this]() {
            handleOutput(m_process->readAll());
        });
        connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this](int, QProcess::ExitStatus status) {
            handleOutput(m_process->readAll());
            QString error;
            if (status != QProcess::NormalExit && !m_stopRequested) error = "The scan stopped unexpectedly.";
            m_process->deleteLater();
            m_process = nullptr;
            finishScan(error);
        });
        connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart || !m_process) return;
            m_process->deleteLater();
            m_process = nullptr;
            finishScan("Could not run /bin/sh on this computer.");
        });
        m_process->start("/bin/sh", QStringList() << "-c" << script);
        return;
    }

    // Through a saved connection: run the script there over SSH. Wrapped in
    // sh -c so it works whatever the login shell is.
    m_statusLabel->setText(QString("Connecting to %1...").arg(via->host));
    m_workerThread = new QThread(this);
    m_worker = new SftpWorker(*via);
    m_worker->moveToThread(m_workerThread);
    connect(m_worker, &SftpWorker::step, this, [this, via](const QString &step) {
        if (step == "Running command") {
            m_statusLabel->setText(QString("Scanning %1 addresses from %2...")
                                       .arg(m_rangeEnd - m_rangeStart + 1).arg(via->host));
        } else {
            m_statusLabel->setText(step + "...");
        }
    });
    connect(m_worker, &SftpWorker::commandOutput, this, &HostScanDialog::handleOutput);
    connect(m_worker, &SftpWorker::commandFinished, this, [this](int, const QString &error) {
        stopRemoteWorker();
        finishScan(m_stopRequested ? QString() : error);
    });
    m_workerThread->start();
    QMetaObject::invokeMethod(m_worker, "runCommand", Qt::QueuedConnection,
                              Q_ARG(QString, "sh -c " + CommandSafetyHelper::escapeShellArgument(script)));
}

void HostScanDialog::stopScan()
{
    if (!m_scanning) return;
    m_stopRequested = true;
    m_statusLabel->setText("Stopping...");
    if (m_process) m_process->kill();
    if (m_worker) m_worker->abortConnection();
}

void HostScanDialog::stopRemoteWorker()
{
    if (!m_worker) return;
    m_worker->abortConnection();
    m_workerThread->quit();
    m_workerThread->wait();
    delete m_worker;
    m_worker = nullptr;
    m_workerThread->deleteLater();
    m_workerThread = nullptr;
}

void HostScanDialog::handleOutput(const QByteArray &data)
{
    m_pending += data;
    int newline;
    while ((newline = m_pending.indexOf('\n')) >= 0) {
        QString line = QString::fromUtf8(m_pending.left(newline)).trimmed();
        m_pending.remove(0, newline + 1);
        if (!line.isEmpty()) handleLine(line);
    }
}

void HostScanDialog::handleLine(const QString &line)
{
    if (line == "ARP") { m_section = Arp; return; }
    if (line == "SELF") { m_section = Self; return; }

    QStringList fields = line.simplified().split(' ');
    quint32 address = 0;

    switch (m_section) {
    case Probes:
        if (line == "NOPORTCHECK") { m_noPortCheck = true; return; }
        if (line == "NOPING") { m_noPing = true; return; }
        if (fields.size() != 2 || !parseIPv4(fields[1], &address)) return;
        if (fields[0] == "UP") {
            rowFor(address);
        } else if (fields[0] == "OPEN") {
            setRowSsh(rowFor(address), true);
        } else if (fields[0] == "DONE") {
            m_progress->setValue(++m_done);
        }
        break;
    case Arp:
        // IP address  HW type  Flags  HW address  Mask  Device
        // Flags 0x0 is an incomplete entry: the host never answered
        if (fields.size() >= 4 && parseIPv4(fields[0], &address) && fields[2] != "0x0"
            && fields[3] != "00:00:00:00:00:00") {
            m_macs.insert(address, fields[3].toLower());
        }
        break;
    case Self:
        if (fields.size() >= 2 && parseIPv4(fields[0], &address)
            && fields[1] != "00:00:00:00:00:00") {
            m_macs.insert(address, fields[1].toLower());
        }
        break;
    }
}

QTreeWidgetItem *HostScanDialog::rowFor(quint32 address)
{
    QTreeWidgetItem *row = m_rows.value(address);
    if (row) return row;

    QSignalBlocker blocker(m_results);
    row = new ResultItem(m_results);
    row->setText(IpColumn, formatIPv4(address));
    row->setData(IpColumn, Qt::UserRole, address);
    row->setCheckState(IpColumn, Qt::Unchecked);
    setRowSsh(row, false);
    m_rows.insert(address, row);
    return row;
}

void HostScanDialog::setRowSsh(QTreeWidgetItem *row, bool open)
{
    QSignalBlocker blocker(m_results);
    if (m_noPortCheck) {
        row->setText(SshColumn, "Unknown");
    } else {
        row->setText(SshColumn, open ? "Yes" : "No");
        row->setForeground(SshColumn, open ? QColor("#008000") : QColor("#888888"));
    }
    if (open) row->setCheckState(IpColumn, Qt::Checked);
    row->setData(SshColumn, Qt::UserRole, open);
}

void HostScanDialog::finishScan(const QString &error)
{
    handleOutput("\n");

    // Hosts that answer ARP but block ping and the port still exist
    for (auto it = m_macs.constBegin(); it != m_macs.constEnd(); ++it) {
        if (it.key() >= m_rangeStart && it.key() <= m_rangeEnd && m_section != Probes) {
            rowFor(it.key());
        }
    }
    {
        QSignalBlocker blocker(m_results);
        for (auto it = m_rows.constBegin(); it != m_rows.constEnd(); ++it) {
            QString mac = m_macs.value(it.key());
            it.value()->setText(MacColumn, mac);
            if (m_savedHosts.contains(formatIPv4(it.key()))) {
                it.value()->setCheckState(IpColumn, Qt::Unchecked);
                it.value()->setToolTip(IpColumn, "Already saved as a connection");
                it.value()->setText(IpColumn, formatIPv4(it.key()) + "  (saved)");
            }
        }
    }
    m_results->sortByColumn(m_results->sortColumn(), m_results->header()->sortIndicatorOrder());
    setScanning(false);

    int open = 0;
    for (QTreeWidgetItem *row : m_rows) {
        if (row->data(SshColumn, Qt::UserRole).toBool()) ++open;
    }
    QString summary = QString("Found %1 host(s), %2 with port %3 open.")
                          .arg(m_rows.size()).arg(open).arg(m_scanPort);
    if (m_stopRequested) summary = "Stopped. " + summary;
    if (m_noPortCheck) summary += " The scanning host has neither bash with timeout nor nc, so the port was not checked.";
    if (m_noPing) summary += " The scanning host has no ping command.";
    if (!error.isEmpty()) summary = error;
    m_statusLabel->setText(summary);
    if (!m_stopRequested) m_progress->setValue(m_progress->maximum());
    updateImportButton();
}

void HostScanDialog::setScanning(bool scanning)
{
    m_scanning = scanning;
    m_scanButton->setEnabled(!scanning);
    m_stopButton->setEnabled(scanning);
    m_startEdit->setEnabled(!scanning);
    m_endEdit->setEnabled(!scanning);
    m_portSpin->setEnabled(!scanning);
    m_timeoutSpin->setEnabled(!scanning);
    m_scanFromCombo->setEnabled(!scanning);
    updateImportButton();
}

void HostScanDialog::updateImportButton()
{
    int checked = 0;
    for (QTreeWidgetItem *row : m_rows) {
        if (row->checkState(IpColumn) == Qt::Checked) ++checked;
    }
    m_importButton->setText(checked ? QString("Import %1 host(s)").arg(checked) : QString("Import"));
    m_importButton->setEnabled(!m_scanning && checked > 0 && !m_usernameEdit->text().trimmed().isEmpty());
}

void HostScanDialog::importChecked()
{
    QString folder = m_folderCombo->currentText().trimmed();
    QString username = m_usernameEdit->text().trimmed();
    QList<SSHConnection> added;
    int skipped = 0;

    QList<quint32> addresses = m_rows.keys();
    std::sort(addresses.begin(), addresses.end());
    QSignalBlocker blocker(m_results);
    for (quint32 address : addresses) {
        QTreeWidgetItem *row = m_rows.value(address);
        if (row->checkState(IpColumn) != Qt::Checked) continue;
        row->setCheckState(IpColumn, Qt::Unchecked);

        QString ip = formatIPv4(address);
        if (m_savedHosts.contains(ip)) {
            ++skipped;
            continue;
        }
        added.append(SSHConnection(ip, ip, username, m_scanPort, folder));
        m_savedHosts.insert(ip);
        row->setText(IpColumn, ip + "  (saved)");
        row->setToolTip(IpColumn, "Already saved as a connection");
    }
    blocker.unblock();

    if (!added.isEmpty()) emit importConnections(added);
    QString message = QString("Imported %1 connection(s)").arg(added.size());
    if (!folder.isEmpty()) message += " into " + folder;
    message += ".";
    if (skipped) message += QString(" Skipped %1 already saved.").arg(skipped);
    m_statusLabel->setText(message);
    updateImportButton();
}

void HostScanDialog::reject()
{
    stopScan();
    QDialog::reject();
}
