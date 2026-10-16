// SSH host scanner: find hosts with SSH open in an IPv4 range and import them
#include "hostscantab.h"
#include "commandsafety.h"
#include "sftpworker.h"

#include <QComboBox>
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

HostScanTab::HostScanTab(const QList<SSHConnection> &connections, const QStringList &folders,
                               QWidget *parent)
    : QWidget(parent), m_rangeEdited(false),
      m_process(nullptr), m_workerThread(nullptr), m_worker(nullptr),
      m_scanning(false), m_stopRequested(false), m_section(Probes),
      m_rangeStart(0), m_rangeEnd(0), m_scanPort(22), m_done(0),
      m_noPortCheck(false), m_noPing(false), m_sawEnd(false), m_commandStarted(false)
{
    QVBoxLayout *layout = new QVBoxLayout(this);

    // Range and options
    QGroupBox *rangeGroup = new QGroupBox("Scan", this);
    rangeGroup->setMaximumWidth(820);
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
    connect(m_startEdit, &QLineEdit::returnPressed, this, &HostScanTab::startScan);
    connect(m_endEdit, &QLineEdit::returnPressed, this, &HostScanTab::startScan);

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
    connect(m_scanFromCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &HostScanTab::onScanFromChanged);
    form->addRow("Scan from:", m_scanFromCombo);

    m_scanFromNote = new QLabel(this);
    m_scanFromNote->setWordWrap(true);
    m_scanFromNote->setStyleSheet("QLabel { color: #666; }");
    form->addRow("", m_scanFromNote);

    QHBoxLayout *scanRow = new QHBoxLayout();
    m_progress = new QProgressBar(this);
    m_progress->setTextVisible(false);
    m_scanButton = new QPushButton("🔍 Scan", this);
    m_stopButton = new QPushButton("Stop", this);
    m_stopButton->setEnabled(false);
    connect(m_scanButton, &QPushButton::clicked, this, &HostScanTab::startScan);
    connect(m_stopButton, &QPushButton::clicked, this, &HostScanTab::stopScan);
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
    connect(m_results, &QTreeWidget::itemChanged, this, &HostScanTab::updateImportButton);
    layout->addWidget(m_results, 1);

    m_statusLabel = new QLabel("Hosts that answer a ping, have the port open, or appear in the "
                               "ARP table are listed.", this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    // Import
    QGroupBox *importGroup = new QGroupBox("Import checked hosts as SSH connections", this);
    importGroup->setMaximumWidth(820);
    QHBoxLayout *importRow = new QHBoxLayout(importGroup);
    m_folderCombo = new QComboBox(this);
    m_folderCombo->setEditable(true);
    m_folderCombo->setMinimumWidth(160);
    importRow->addWidget(new QLabel("Folder:", this));
    importRow->addWidget(m_folderCombo, 1);
    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setPlaceholderText("SSH username for the new connections");
    QByteArray user = qgetenv("USER");
    if (user.isEmpty()) user = qgetenv("LOGNAME");
    m_usernameEdit->setText(QString::fromLocal8Bit(user));
    connect(m_usernameEdit, &QLineEdit::textChanged, this, &HostScanTab::updateImportButton);
    importRow->addSpacing(12);
    importRow->addWidget(new QLabel("Username:", this));
    importRow->addWidget(m_usernameEdit, 1);
    m_importButton = new QPushButton("Import", this);
    m_importButton->setEnabled(false);
    connect(m_importButton, &QPushButton::clicked, this, &HostScanTab::importChecked);
    importRow->addSpacing(12);
    importRow->addWidget(m_importButton);
    layout->addWidget(importGroup);

    setConnections(connections, folders);
    onScanFromChanged();
}

void HostScanTab::setConnections(const QList<SSHConnection> &connections, const QStringList &folders)
{
    m_connections = connections;
    m_savedHosts.clear();
    for (const SSHConnection &conn : connections) {
        m_savedHosts.insert(conn.host.trimmed().toLower());
    }

    // Rebuild "Scan from", keeping the current choice if it still exists
    const SSHConnection *current = scanFromConnection();
    QString currentKey = current ? current->folder + '\n' + current->name : QString();
    {
        QSignalBlocker blocker(m_scanFromCombo);
        m_scanFromCombo->clear();
        m_scanFromCombo->addItem("This computer", -1);
        for (int i = 0; i < connections.size(); ++i) {
            const SSHConnection &conn = connections[i];
            QString label = QString("%1 (%2@%3)").arg(conn.name, conn.username, conn.host);
            if (!conn.folder.isEmpty()) label = conn.folder + " / " + label;
            m_scanFromCombo->addItem("Through " + label, i);
            if (conn.folder + '\n' + conn.name == currentKey) {
                m_scanFromCombo->setCurrentIndex(m_scanFromCombo->count() - 1);
            }
        }
    }

    QString folder = m_folderCombo->currentText();
    m_folderCombo->clear();
    m_folderCombo->addItems(folders);
    if (!folder.isEmpty()) setImportFolder(folder);

    // Refresh the "(saved)" marks on the results
    QSignalBlocker blocker(m_results);
    for (auto it = m_rows.constBegin(); it != m_rows.constEnd(); ++it) {
        QString ip = formatIPv4(it.key());
        bool saved = m_savedHosts.contains(ip);
        it.value()->setText(IpColumn, saved ? ip + "  (saved)" : ip);
        it.value()->setToolTip(IpColumn, saved ? QStringLiteral("Already saved as a connection") : QString());
    }
}

HostScanTab::~HostScanTab()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    stopRemoteWorker();
}

void HostScanTab::setScanFrom(int connectionIndex)
{
    int comboIndex = m_scanFromCombo->findData(connectionIndex);
    if (comboIndex >= 0) m_scanFromCombo->setCurrentIndex(comboIndex);
}

void HostScanTab::setImportFolder(const QString &folder)
{
    if (folder.isEmpty()) return;
    if (m_folderCombo->findText(folder) < 0) m_folderCombo->addItem(folder);
    m_folderCombo->setCurrentText(folder);
}

const SSHConnection *HostScanTab::scanFromConnection() const
{
    int index = m_scanFromCombo->currentData().toInt();
    return index >= 0 && index < m_connections.size() ? &m_connections[index] : nullptr;
}

void HostScanTab::onScanFromChanged()
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
void HostScanTab::setDefaultRange()
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

bool HostScanTab::parseIPv4(const QString &text, quint32 *address)
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

QString HostScanTab::formatIPv4(quint32 address)
{
    return QString("%1.%2.%3.%4").arg(address >> 24).arg((address >> 16) & 255)
                                  .arg((address >> 8) & 255).arg(address & 255);
}

// Prints "UP <ip>" for a ping reply, then the port check's result: "OPEN",
// "REFUSED" (the host answered, nothing listens), "TIMEOUT" (no answer in
// time, often a firewall dropping it), "UNREACH" or "CLOSED" (any other
// failure), each followed by the IP, and "DONE <ip>" when a host is finished.
// Then the ARP table after "ARP", the scanning host's own addresses after
// "SELF", and "END". Probing fills the ARP table, so it is read last. Uses
// bash's /dev/tcp with coreutils timeout for the port check (timeout exits
// 124 when time runs out), or nc when those are missing. Error messages are
// matched in English, hence LC_ALL=C.
QString HostScanTab::scanScript(quint32 start, quint32 end, int port, int timeoutSeconds)
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
        "    r=$(LC_ALL=C timeout \"$T\" \"$B\" -c 'exec 3<>\"/dev/tcp/$0/$1\"' \"$1\" \"$P\" 2>&1 >/dev/null); c=$?\n"
        "  elif [ -n \"$NC\" ]; then\n"
        "    r=$(LC_ALL=C nc -v -z -w \"$T\" \"$1\" \"$P\" 2>&1 >/dev/null); c=$?\n"
        "  fi\n"
        "  if [ -n \"$B$NC\" ]; then\n"
        "    if [ \"$c\" -eq 0 ]; then s=OPEN\n"
        "    elif [ \"$c\" -eq 124 ]; then s=TIMEOUT\n"
        "    else case \"$r\" in\n"
        "      *efused*) s=REFUSED;;\n"
        "      *'timed out'*|*'in progress'*) s=TIMEOUT;;\n"
        "      *'No route'*|*nreachable*) s=UNREACH;;\n"
        "      *) s=CLOSED;;\n"
        "    esac; fi\n"
        "    echo \"$s $1\"\n"
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
        "fi\n"
        "echo END\n")
        .arg(start).arg(end).arg(port).arg(timeoutSeconds).arg(kParallelProbes);
    return script;
}

void HostScanTab::startScan()
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
    m_sawEnd = false;
    m_commandStarted = false;
    m_stderr.clear();
    m_ports.clear();
    m_stopRequested = false;
    m_progress->setRange(0, int(end - start + 1));
    m_progress->setValue(0);
    m_results->headerItem()->setText(SshColumn, QString("SSH open (port %1)").arg(m_scanPort));
    setScanning(true);

    QString script = scanScript(start, end, m_scanPort, m_timeoutSpin->value());
    const SSHConnection *via = scanFromConnection();
    m_scanFromName = via ? QString("%1 (%2)").arg(via->name, via->host) : QStringLiteral("this computer");

    if (!via) {
        showStatus(QString("Scanning %1 addresses...").arg(end - start + 1));
        m_process = new QProcess(this);
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
            handleOutput(m_process->readAllStandardOutput());
        });
        connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
            m_stderr = (m_stderr + m_process->readAllStandardError()).right(4096);
        });
        connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this](int exitCode, QProcess::ExitStatus status) {
            handleOutput(m_process->readAllStandardOutput());
            m_stderr = (m_stderr + m_process->readAllStandardError()).right(4096);
            m_process->deleteLater();
            m_process = nullptr;
            scriptEnded(exitCode, status != QProcess::NormalExit);
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
    showStatus(QString("Connecting to %1...").arg(via->host));
    m_workerThread = new QThread(this);
    m_worker = new SftpWorker(*via);
    m_worker->moveToThread(m_workerThread);
    QString viaHost = via->host;
    connect(m_worker, &SftpWorker::step, this, [this, viaHost](const QString &step) {
        if (step == "Running command") {
            showStatus(QString("Scanning %1 addresses from %2...")
                           .arg(m_rangeEnd - m_rangeStart + 1).arg(viaHost));
        } else {
            showStatus(step + "...");
        }
    });
    connect(m_worker, &SftpWorker::commandOutput, this, &HostScanTab::handleOutput);
    connect(m_worker, &SftpWorker::commandErrorOutput, this, [this](const QByteArray &data) {
        m_stderr = (m_stderr + data).right(4096);
    });
    connect(m_worker, &SftpWorker::commandFinished, this, [this](int exitStatus, const QString &error) {
        bool ran = m_commandStarted;
        stopRemoteWorker();
        if (m_stopRequested) {
            finishScan(QString());
        } else if (!error.isEmpty() && !ran) {
            // Never got as far as running the script: the login failed
            finishScan(QString("Could not scan from %1. Logging in to it failed:\n%2")
                           .arg(m_scanFromName, error));
        } else if (!error.isEmpty()) {
            finishScan(QString("The scan on %1 was cut off: %2").arg(m_scanFromName, error));
        } else {
            scriptEnded(exitStatus, exitStatus < 0);
        }
    });
    connect(m_worker, &SftpWorker::step, this, [this](const QString &step) {
        if (step == "Running command") m_commandStarted = true;
    });
    m_workerThread->start();
    QMetaObject::invokeMethod(m_worker, "runCommand", Qt::QueuedConnection,
                              Q_ARG(QString, "sh -c " + CommandSafetyHelper::escapeShellArgument(script)));
}

void HostScanTab::stopScan()
{
    if (!m_scanning) return;
    m_stopRequested = true;
    showStatus("Stopping...");
    if (m_process) m_process->kill();
    if (m_worker) m_worker->abortConnection();
}

void HostScanTab::stopRemoteWorker()
{
    if (!m_worker) return;
    SftpWorker::retire(m_workerThread, m_worker);
    m_worker = nullptr;
    m_workerThread = nullptr;
}

void HostScanTab::handleOutput(const QByteArray &data)
{
    m_pending += data;
    int newline;
    while ((newline = m_pending.indexOf('\n')) >= 0) {
        QString line = QString::fromUtf8(m_pending.left(newline)).trimmed();
        m_pending.remove(0, newline + 1);
        if (!line.isEmpty()) handleLine(line);
    }
}

void HostScanTab::handleLine(const QString &line)
{
    if (line == "ARP") { m_section = Arp; return; }
    if (line == "SELF") { m_section = Self; return; }
    if (line == "END") { m_sawEnd = true; return; }

    QStringList fields = line.simplified().split(' ');
    quint32 address = 0;

    switch (m_section) {
    case Probes:
        if (line == "NOPORTCHECK") { m_noPortCheck = true; return; }
        if (line == "NOPING") { m_noPing = true; return; }
        if (fields.size() != 2 || !parseIPv4(fields[1], &address)) {
            // Not from the script (a login message, say); kept for error reports
            m_stderr = (m_stderr + line.toUtf8() + '\n').right(4096);
            return;
        }
        if (fields[0] == "UP") {
            rowFor(address);
        } else if (fields[0] == "OPEN" || fields[0] == "REFUSED") {
            // A refusal comes from the host itself, so it is up
            m_ports.insert(address, fields[0] == "OPEN" ? Open : Refused);
            setRowSsh(rowFor(address), m_ports.value(address));
        } else if (fields[0] == "TIMEOUT" || fields[0] == "UNREACH" || fields[0] == "CLOSED") {
            PortState state = fields[0] == "TIMEOUT" ? TimedOut
                            : fields[0] == "UNREACH" ? Unreachable : Closed;
            m_ports.insert(address, state);
            if (QTreeWidgetItem *row = m_rows.value(address)) setRowSsh(row, state);
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

QTreeWidgetItem *HostScanTab::rowFor(quint32 address)
{
    QTreeWidgetItem *row = m_rows.value(address);
    if (row) return row;

    QSignalBlocker blocker(m_results);
    row = new ResultItem(m_results);
    row->setText(IpColumn, formatIPv4(address));
    row->setData(IpColumn, Qt::UserRole, address);
    row->setCheckState(IpColumn, Qt::Unchecked);
    m_rows.insert(address, row);
    setRowSsh(row, m_ports.value(address, NotChecked));
    return row;
}

void HostScanTab::setRowSsh(QTreeWidgetItem *row, PortState state)
{
    QSignalBlocker blocker(m_results);
    QString text, tip;
    QColor color("#888888");
    switch (state) {
    case Open:
        text = "Yes";
        color = QColor("#008000");
        tip = QString("Port %1 accepted a connection.").arg(m_scanPort);
        break;
    case Refused:
        text = "No (refused)";
        tip = QString("The host answered, but nothing is listening on port %1.").arg(m_scanPort);
        break;
    case TimedOut:
        text = "No (timed out)";
        color = QColor("#b36b00");
        tip = QString("No answer on port %1 within the timeout. A firewall may be dropping "
                      "the connection, or the host is slow; try a longer timeout.").arg(m_scanPort);
        break;
    case Unreachable:
        text = "No (unreachable)";
        tip = "The network reported the host as unreachable.";
        break;
    case Closed:
        text = "No";
        tip = QString("Could not connect to port %1.").arg(m_scanPort);
        break;
    case NotChecked:
        text = m_noPortCheck ? QStringLiteral("Unknown") : m_scanning ? QStringLiteral("...")
                                                                      : QStringLiteral("Not checked");
        tip = m_noPortCheck ? QStringLiteral("The scanning host has neither bash with timeout nor nc.")
                            : QString();
        break;
    }
    row->setText(SshColumn, text);
    row->setToolTip(SshColumn, tip);
    row->setForeground(SshColumn, color);
    if (state == Open) row->setCheckState(IpColumn, Qt::Checked);
    row->setData(SshColumn, Qt::UserRole, state == Open);
}

void HostScanTab::showStatus(const QString &text, bool isError)
{
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(isError ? "QLabel { color: #c00000; font-weight: bold; }" : QString());
}

// The last few lines the script wrote to stderr, for error messages
QString HostScanTab::stderrTail() const
{
    QStringList lines = QString::fromUtf8(m_stderr).trimmed().split('\n');
    while (lines.size() > 5) lines.removeFirst();
    return lines.join('\n').trimmed();
}

// The script ran; reports it when it failed or stopped before the end
void HostScanTab::scriptEnded(int exitCode, bool crashed)
{
    if (m_stopRequested || (m_sawEnd && exitCode == 0 && !crashed)) {
        finishScan(QString());
        return;
    }
    QString message;
    if (crashed) {
        message = QString("The scan on %1 was killed before it finished.").arg(m_scanFromName);
    } else if (exitCode != 0) {
        message = QString("The scan on %1 failed (exit status %2).").arg(m_scanFromName).arg(exitCode);
    } else {
        message = QString("The scan on %1 ended before it finished.").arg(m_scanFromName);
    }
    QString details = stderrTail();
    if (!details.isEmpty()) message += "\n" + details;
    if (!m_rows.isEmpty()) message += "\nThe hosts found before that are listed.";
    finishScan(message);
}

void HostScanTab::finishScan(const QString &error)
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

    int open = 0, refused = 0, timedOut = 0;
    for (auto it = m_rows.constBegin(); it != m_rows.constEnd(); ++it) {
        PortState state = m_ports.value(it.key(), NotChecked);
        if (state == Open) ++open;
        else if (state == Refused) ++refused;
        else if (state == TimedOut) ++timedOut;
        setRowSsh(it.value(), state);  // Settles rows still showing "..."
    }
    QString summary = QString("Found %1 host(s): port %2 open on %3").arg(m_rows.size()).arg(m_scanPort).arg(open);
    if (refused) summary += QString(", refused on %1").arg(refused);
    if (timedOut) summary += QString(", timed out on %1").arg(timedOut);
    summary += ".";
    if (m_stopRequested) summary = "Stopped. " + summary;
    if (m_noPortCheck) summary += " The scanning host has neither bash with timeout nor nc, so the port was not checked.";
    if (m_noPing) summary += " The scanning host has no ping command.";
    if (!error.isEmpty()) summary = error;
    showStatus(summary, !error.isEmpty());
    if (!m_stopRequested && error.isEmpty()) m_progress->setValue(m_progress->maximum());
    updateImportButton();
}

void HostScanTab::setScanning(bool scanning)
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

void HostScanTab::updateImportButton()
{
    int checked = 0;
    for (QTreeWidgetItem *row : m_rows) {
        if (row->checkState(IpColumn) == Qt::Checked) ++checked;
    }
    m_importButton->setText(checked ? QString("Import %1 host(s)").arg(checked) : QString("Import"));
    m_importButton->setEnabled(!m_scanning && checked > 0 && !m_usernameEdit->text().trimmed().isEmpty());
}

void HostScanTab::importChecked()
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
    showStatus(message);
    updateImportButton();
}
