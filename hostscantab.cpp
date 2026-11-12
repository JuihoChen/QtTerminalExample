// SSH host scanner: find hosts with SSH open in an IPv4 range and import them
#include "hostscantab.h"
#include "proxybrowser.h"
#include "commandsafety.h"
#include "sftpworker.h"
#include "inwindowdialog.h"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QTcpServer>
#include "portcombobox.h"
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

const quint32 kMaxAddresses = 4096;  // A /20; larger ranges take too long
const int kParallelProbes = 64;

enum Column { IpColumn, MacColumn, SshColumn, NameColumn, BmcColumn };

// Whether this PC can listen on the port, as ssh -L would (on loopback)
bool canBindLocally(int port)
{
    QTcpServer probe;
    probe.setProxy(QNetworkProxy::NoProxy);
    if (!probe.listen(QHostAddress::LocalHost, static_cast<quint16>(port))) return false;
    probe.close();
    return true;
}

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
    : QWidget(parent), m_rangeEdited(false), m_endEdited(false),
      m_process(nullptr), m_workerThread(nullptr), m_worker(nullptr),
      m_scanning(false), m_stopRequested(false), m_section(Probes),
      m_rangeStart(0), m_rangeEnd(0), m_scanPort(22), m_done(0),
      m_noPortCheck(false), m_noPing(false), m_sawEnd(false), m_commandStarted(false),
      m_findBmcs(false), m_noCurl(false)
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    m_layout = layout;

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
    connect(m_startEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        m_rangeEdited = true;
        followStartAddress(text);
    });
    connect(m_endEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        m_rangeEdited = true;
        m_endEdited = !text.isEmpty();  // Clearing it lets it follow the start again
    });
    QHBoxLayout *rangeRow = new QHBoxLayout();
    rangeRow->addWidget(m_startEdit);
    rangeRow->addWidget(new QLabel("to", this));
    rangeRow->addWidget(m_endEdit);
    form->addRow("IP range:", rangeRow);
    connect(m_startEdit, &QLineEdit::returnPressed, this, &HostScanTab::startScan);
    connect(m_endEdit, &QLineEdit::returnPressed, this, &HostScanTab::startScan);

    m_portCombo = new PortComboBox(this);
    m_timeoutSpin = new QSpinBox(this);
    m_timeoutSpin->setRange(1, 10);
    m_timeoutSpin->setValue(1);
    m_timeoutSpin->setSuffix(" s");
    QHBoxLayout *optionsRow = new QHBoxLayout();
    optionsRow->addWidget(m_portCombo);
    optionsRow->addSpacing(16);
    optionsRow->addWidget(new QLabel("Timeout:", this));
    optionsRow->addWidget(m_timeoutSpin);
    optionsRow->addSpacing(16);
    m_bmcCheck = new QCheckBox("Find BMCs", this);
    m_bmcCheck->setToolTip("Also check port 443 and ask each web server for Redfish, the management "
                           "API of BMCs (iDRAC, iLO, XClarity, OpenBMC, NVIDIA and others).\n"
                           "Scanning through a head node, the BMCs found can then be added to "
                           "its tunnels, to open their web pages from this PC.");
    optionsRow->addWidget(m_bmcCheck);
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
    m_results->setHeaderLabels(QStringList() << "IP address" << "MAC address" << "SSH open" << "Name" << "BMC");
    m_results->setRootIsDecorated(false);
    m_results->setSortingEnabled(true);
    m_results->sortByColumn(IpColumn, Qt::AscendingOrder);
    m_results->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_results->header()->setSectionResizeMode(IpColumn, QHeaderView::ResizeToContents);
    m_results->header()->setSectionResizeMode(MacColumn, QHeaderView::ResizeToContents);
    m_results->setColumnWidth(IpColumn, 160);
    m_results->setColumnWidth(SshColumn, 150);
    m_results->setColumnWidth(NameColumn, 150);
    m_results->setColumnHidden(NameColumn, true);
    m_results->setColumnHidden(BmcColumn, true);
    connect(m_results, &QTreeWidget::itemChanged, this, &HostScanTab::updateImportButton);
    layout->addWidget(m_results, 1);

    m_statusLabel = new QLabel("Hosts that answer a ping, have the port open, or appear in the "
                               "ARP table are listed.", this);
    m_statusLabel->setWordWrap(true);
    m_forwardButton = new QPushButton("🔀 Forward BMCs...", this);
    m_forwardButton->setToolTip("Add the BMCs found to the tunnels of the connection the scan ran "
                                "through, to open their web pages from this PC");
    m_forwardButton->hide();
    connect(m_forwardButton, &QPushButton::clicked, this, &HostScanTab::forwardBmcs);
    QHBoxLayout *statusRow = new QHBoxLayout();
    statusRow->addWidget(m_statusLabel, 1);
    statusRow->addWidget(m_forwardButton, 0, Qt::AlignTop);
    layout->addLayout(statusRow);

    // Import
    QGroupBox *importGroup = new QGroupBox("Import checked hosts as SSH connections", this);
    importGroup->setMaximumWidth(820);
    m_importGroup = importGroup;
    QHBoxLayout *importRow = new QHBoxLayout(importGroup);
    m_importRow = importRow;
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

    updateForwardButton();  // The connection scanned through may be gone

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
    // Lent to the window's side panel: not deleted along with this tab otherwise
    if (m_importGroup && m_importGroup->parent() != this) delete m_importGroup;

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

void HostScanTab::lendImportSection(QWidget *container)
{
    if (m_importGroup->parentWidget() == container) return;
    m_importRow->setDirection(QBoxLayout::TopToBottom);
    for (int i = 0; i < m_importRow->count(); ++i) m_importRow->setStretch(i, 0);
    // At the top of the container, above its trailing stretch
    static_cast<QBoxLayout *>(container->layout())->insertWidget(0, m_importGroup);
    m_importGroup->show();
}

void HostScanTab::takeBackImportSection()
{
    if (m_importGroup->parentWidget() == this) return;
    m_importRow->setDirection(QBoxLayout::LeftToRight);
    m_importRow->setStretchFactor(m_folderCombo, 1);
    m_importRow->setStretchFactor(m_usernameEdit, 1);
    m_layout->addWidget(m_importGroup);
    m_importGroup->show();
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

// The saved connection the last scan ran through, if it still exists
const SSHConnection *HostScanTab::scannedThrough() const
{
    if (m_scanFromKey.isEmpty()) return nullptr;
    for (const SSHConnection &conn : m_connections) {
        if (conn.folder + '\n' + conn.name == m_scanFromKey) return &conn;
    }
    return nullptr;
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

// Suggests the subnet around the scanning host
void HostScanTab::setDefaultRange()
{
    quint32 address = 0;
    int prefixLength = 24;
    const SSHConnection *via = scanFromConnection();
    if (via) {
        // The remote host's netmask isn't known here, so assume a /24
        if (!parseIPv4(via->host.trimmed(), &address)) return;  // A host name: leave it to the user
    } else {
        QNetworkAddressEntry entry;
        if (!localScanAddress(&entry)) return;
        address = entry.ip().toIPv4Address();
        prefixLength = entry.prefixLength();
    }
    // Use the real subnet when it is scannable; otherwise fall back to the /24
    // around the address (point-to-point links, or subnets too big to scan)
    if (prefixLength < 1 || prefixLength > 30 || (quint64(1) << (32 - prefixLength)) > kMaxAddresses) {
        prefixLength = 24;
    }
    const quint32 mask = 0xFFFFFFFFu << (32 - prefixLength);
    const quint32 network = address & mask;
    const quint32 broadcast = network | ~mask;
    m_startEdit->setText(formatIPv4(network + 1));
    m_endEdit->setText(formatIPv4(broadcast - 1));
}

// Picks the IPv4 address of the network this computer is really on: an
// interface that is up with a link, not a container/VM bridge or VPN, with
// wired Ethernet preferred over Wi-Fi
bool HostScanTab::localScanAddress(QNetworkAddressEntry *result)
{
    static const QStringList virtualPrefixes = {
        "docker", "br-", "veth", "virbr", "vmnet", "vboxnet", "lxcbr", "lxdbr", "cni",
        "flannel", "cali", "podman", "tun", "tap", "wg", "tailscale", "zt", "utun",
        "vethernet", "virtualbox", "vmware", "hyper-v"};
    auto isVirtual = [](const QNetworkInterface &iface) {
        if (iface.type() == QNetworkInterface::Virtual) return true;
        const QString names[] = {iface.name().toLower(), iface.humanReadableName().toLower()};
        for (const QString &name : names) {
            for (const QString &prefix : virtualPrefixes) {
                if (name.startsWith(prefix)) return true;
            }
        }
        return false;
    };

    int bestScore = -1;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || (flags & QNetworkInterface::IsLoopBack)) continue;

        int score;
        if (!(flags & QNetworkInterface::IsRunning)) {
            score = 0;  // Up but no link (e.g. docker0 with no containers): last resort
        } else if (isVirtual(iface) || (flags & QNetworkInterface::IsPointToPoint)) {
            score = 1;
        } else if (iface.type() == QNetworkInterface::Wifi) {
            score = 2;
        } else {
            score = 3;  // Wired Ethernet, or a type Qt can't tell
        }
        if (score <= bestScore) continue;

        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            if ((entry.ip().toIPv4Address() >> 16) == 0xA9FE) continue;  // 169.254 link-local
            *result = entry;
            bestScore = score;
            break;
        }
    }
    return bestScore >= 0;
}

// While the start address is typed, keeps the end at .254 of the same /24,
// until the user types an end address of their own
void HostScanTab::followStartAddress(const QString &text)
{
    if (m_endEdited) return;
    const QStringList parts = text.split('.');
    if (parts.size() < 4) return;  // The first three numbers aren't finished yet
    for (int i = 0; i < 3; ++i) {
        bool ok = false;
        const int value = parts[i].toInt(&ok);
        if (!ok || parts[i].size() > 3 || value > 255) return;
    }
    m_endEdit->setText(QStringList(parts.mid(0, 3)).join('.') + ".254");
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
//
// With findBmcs, a host with port 443 open also prints "WEB <ip>", its name
// from /etc/hosts or DNS as "NAME <ip> <name>", and, when it answers a Redfish
// request (the standard BMC management API), "REDFISH <ip> <service root JSON>"
// on one line. "NOCURL" means Redfish could not be asked.
QString HostScanTab::scanScript(quint32 start, quint32 end, int port, int timeoutSeconds, bool findBmcs)
{
    QString script = QString(
        "S=%1 E=%2 P=%3 T=%4 J=%5 W=%6\n"
        "B=$(command -v bash 2>/dev/null); command -v timeout >/dev/null 2>&1 || B=\n"
        "NC=$(command -v nc 2>/dev/null)\n"
        "[ -n \"$B$NC\" ] || echo NOPORTCHECK\n"
        "command -v ping >/dev/null 2>&1 || echo NOPING\n"
        "CU=$(command -v curl 2>/dev/null); [ -n \"$W\" ] && [ -z \"$CU\" ] && echo NOCURL\n"
        "GE=$(command -v getent 2>/dev/null)\n"
        "chk() {\n"
        "  if [ -n \"$B\" ]; then\n"
        "    r=$(LC_ALL=C timeout \"$T\" \"$B\" -c 'exec 3<>\"/dev/tcp/$0/$1\"' \"$1\" \"$2\" 2>&1 >/dev/null); c=$?\n"
        "  else\n"
        "    r=$(LC_ALL=C nc -v -z -w \"$T\" \"$1\" \"$2\" 2>&1 >/dev/null); c=$?\n"
        "  fi\n"
        "  if [ \"$c\" -eq 0 ]; then s=OPEN\n"
        "  elif [ \"$c\" -eq 124 ]; then s=TIMEOUT\n"
        "  else case \"$r\" in\n"
        "    *efused*) s=REFUSED;;\n"
        "    *'timed out'*|*'in progress'*) s=TIMEOUT;;\n"
        "    *'No route'*|*nreachable*) s=UNREACH;;\n"
        "    *) s=CLOSED;;\n"
        "  esac; fi\n"
        "}\n"
        "bmc() {\n"
        "  echo \"WEB $1\"\n"
        "  [ -n \"$GE\" ] && \"$GE\" hosts \"$1\" | { read -r _ n _; [ -n \"$n\" ] && echo \"NAME $1 $n\"; }\n"
        "  if [ -n \"$CU\" ]; then\n"
        "    j=$(\"$CU\" -sk -m $((T+3)) \"https://$1/redfish/v1/\" 2>/dev/null | tr -d '\\r\\n')\n"
        "    case \"$j\" in *RedfishVersion*|*'\"@odata.id\"'*) echo \"REDFISH $1 $j\";; esac\n"
        "  fi\n"
        "}\n"
        "probe() {\n"
        "  ping -c 1 -W \"$T\" \"$1\" >/dev/null 2>&1 && echo \"UP $1\"\n"
        "  if [ -n \"$B$NC\" ]; then\n"
        "    chk \"$1\" \"$P\"; echo \"$s $1\"\n"
        "    if [ -n \"$W\" ]; then\n"
        "      [ \"$P\" = 443 ] || chk \"$1\" 443\n"
        "      [ \"$s\" = OPEN ] && bmc \"$1\"\n"
        "    fi\n"
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
        .arg(start).arg(end).arg(port).arg(timeoutSeconds).arg(kParallelProbes)
        .arg(findBmcs ? "1" : "");
    return script;
}

// The vendor and product a Redfish service root names, e.g. "NVIDIA GB300 BMC",
// or "Redfish 1.11.0" when it names neither
QString HostScanTab::redfishSummary(const QByteArray &serviceRoot)
{
    QJsonObject root = QJsonDocument::fromJson(serviceRoot).object();
    if (root.isEmpty()) return QString();
    QString vendor = root.value("Vendor").toString().trimmed();
    QString product = root.value("Product").toString().trimmed();
    QString text = product.startsWith(vendor, Qt::CaseInsensitive) ? product
                 : QString(vendor + ' ' + product).trimmed();
    if (text.isEmpty()) {
        QString version = root.value("RedfishVersion").toString().trimmed();
        text = version.isEmpty() ? QStringLiteral("Redfish") : "Redfish " + version;
    }
    return text;
}

void HostScanTab::startScan()
{
    quint32 start = 0, end = 0;
    if (!parseIPv4(m_startEdit->text().trimmed(), &start)) {
        InWindowDialog::warning(this, "Scan", "Enter a start IP address such as 192.168.1.1.");
        m_startEdit->setFocus();
        return;
    }
    if (!parseIPv4(m_endEdit->text().trimmed(), &end)) {
        InWindowDialog::warning(this, "Scan", "Enter an end IP address such as 192.168.1.254.");
        m_endEdit->setFocus();
        return;
    }
    if (end < start) qSwap(start, end);
    if (end - start >= kMaxAddresses) {
        InWindowDialog::warning(this, "Scan",
            QString("That range has %1 addresses. Scan at most %2 at a time (a /20).")
                .arg(qulonglong(end) - start + 1).arg(kMaxAddresses));
        return;
    }

    if (m_portCombo->value() == 0) {
        InWindowDialog::warning(this, "Scan", "Enter a port between 1 and 65535.");
        return;
    }

    m_results->clear();
    m_rows.clear();
    m_macs.clear();
    m_pending.clear();
    m_section = Probes;
    m_rangeStart = start;
    m_rangeEnd = end;
    m_scanPort = m_portCombo->value();
    m_done = 0;
    m_noPortCheck = false;
    m_noPing = false;
    m_sawEnd = false;
    m_commandStarted = false;
    m_stderr.clear();
    m_ports.clear();
    m_web.clear();
    m_redfish.clear();
    m_names.clear();
    m_noCurl = false;
    m_findBmcs = m_bmcCheck->isChecked();
    m_results->setColumnHidden(NameColumn, !m_findBmcs);
    m_results->setColumnHidden(BmcColumn, !m_findBmcs);
    m_stopRequested = false;
    m_progress->setRange(0, int(end - start + 1));
    m_progress->setValue(0);
    m_results->headerItem()->setText(SshColumn, QString("SSH open (port %1)").arg(m_scanPort));
    setScanning(true);

    QString script = scanScript(start, end, m_scanPort, m_timeoutSpin->value(), m_findBmcs);
    const SSHConnection *via = scanFromConnection();
    m_scanFromKey = via ? via->folder + '\n' + via->name : QString();
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

    // A Redfish reply is JSON with spaces of its own: "REDFISH <ip> {...}"
    if (m_section == Probes && fields.size() >= 3 && fields[0] == "REDFISH"
        && parseIPv4(fields[1], &address)) {
        QString summary = redfishSummary(line.section(' ', 2).toUtf8());
        if (!summary.isEmpty()) {
            m_redfish.insert(address, summary);
            setRowBmc(address);
        }
        return;
    }
    if (m_section == Probes && fields.size() == 3 && fields[0] == "NAME" && parseIPv4(fields[1], &address)) {
        m_names.insert(address, fields[2]);
        setRowBmc(address);
        return;
    }

    switch (m_section) {
    case Probes:
        if (line == "NOPORTCHECK") { m_noPortCheck = true; return; }
        if (line == "NOPING") { m_noPing = true; return; }
        if (line == "NOCURL") { m_noCurl = true; return; }
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
        } else if (fields[0] == "WEB") {
            m_web.insert(address);
            setRowBmc(address);
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

// The Name and BMC columns, as far as the scan has got for this host
void HostScanTab::setRowBmc(quint32 address)
{
    QTreeWidgetItem *row = rowFor(address);
    QSignalBlocker blocker(m_results);
    row->setText(NameColumn, m_names.value(address));
    QString bmc, tip;
    QColor color("#888888");
    if (m_redfish.contains(address)) {
        bmc = "✔ " + m_redfish.value(address);
        color = QColor("#008000");
        tip = "Answers Redfish at https://" + formatIPv4(address) + "/redfish/v1/, so it is a BMC "
              "(or another managed device)";
    } else if (m_web.contains(address)) {
        bmc = m_noCurl ? QStringLiteral("Web (443)") : QStringLiteral("Web only (443)");
        tip = m_noCurl ? QStringLiteral("Port 443 is open. The scanning host has no curl, so Redfish "
                                        "was not checked; it may still be a BMC.")
                       : QStringLiteral("Port 443 is open, but it didn't answer Redfish, so it is "
                                        "probably not a BMC");
    }
    row->setText(BmcColumn, bmc);
    row->setToolTip(BmcColumn, tip);
    row->setForeground(BmcColumn, color);
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
    if (m_findBmcs) {
        int web = 0;
        for (quint32 address : m_web) if (!m_redfish.contains(address)) ++web;
        summary += QString(" BMCs (Redfish): %1").arg(m_redfish.size());
        if (web) summary += QString(", other web servers on 443: %1").arg(web);
        summary += ".";
        if (m_noCurl) summary += " The scanning host has no curl, so Redfish was not checked.";
    }
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
    m_portCombo->setEnabled(!scanning);
    m_timeoutSpin->setEnabled(!scanning);
    m_scanFromCombo->setEnabled(!scanning);
    m_bmcCheck->setEnabled(!scanning);
    updateImportButton();
    updateForwardButton();
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

void HostScanTab::updateForwardButton()
{
    const SSHConnection *via = scannedThrough();
    // From this computer the hosts are reachable directly: no tunnels needed
    m_forwardButton->setVisible(m_findBmcs && !m_scanFromKey.isEmpty());
    m_forwardButton->setEnabled(!m_scanning && via && !m_web.isEmpty());
    if (via) {
        m_forwardButton->setToolTip(QString("Add the BMCs found to the tunnels of %1, to open their "
                                            "web pages from this PC").arg(via->name));
    } else if (!m_scanFromKey.isEmpty()) {
        m_forwardButton->setToolTip("The connection this scan ran through is no longer saved");
    }
}

QList<int> HostScanTab::pickLocalPorts(int count, int first, const QSet<int> &taken,
                                       bool (*canBind)(int port))
{
    QList<int> ports;
    for (int port = qMax(first, 1024); port <= 65535 && ports.size() < count; ++port) {
        if (taken.contains(port) || (canBind && !canBind(port))) continue;
        ports << port;
    }
    return ports;
}

// Turns the BMCs found through a head node into port forwards on that head
// node's connection: one local port per BMC, or one SOCKS proxy for them all
void HostScanTab::forwardBmcs()
{
    const SSHConnection *viaPtr = scannedThrough();
    if (!viaPtr || m_web.isEmpty()) return;
    const SSHConnection via = *viaPtr;

    // What the connection forwards already, and which local ports any saved tunnel uses
    QHash<quint32, int> forwarded;  // BMC address -> local port
    const SSHTunnel *socks = nullptr;
    for (const SSHTunnel &t : via.tunnels) {
        quint32 address = 0;
        if (t.type == SSHTunnel::Local && t.targetPort == 443 && parseIPv4(t.targetHost.trimmed(), &address)) {
            forwarded.insert(address, t.listenPort);
        } else if (t.type == SSHTunnel::Dynamic && !socks) {
            socks = &t;
        }
    }
    QSet<int> taken;
    for (const SSHConnection &conn : m_connections) {
        for (const SSHTunnel &t : conn.tunnels) {
            if (t.listensLocally()) taken.insert(t.listenPort);
        }
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QString("Forward BMCs through %1").arg(via.name));
    dialog.resize(640, 460);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *intro = new QLabel(QString(
        "These hosts answered on port 443 when scanned from <b>%1</b> (%2). Adding them to its "
        "tunnels lets this PC open their web pages through it. Tunnels start with each SSH tab "
        "of %1.").arg(via.name.toHtmlEscaped(), via.host.toHtmlEscaped()), &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    QTreeWidget *list = new QTreeWidget(&dialog);
    list->setHeaderLabels(QStringList() << "IP address" << "Name" << "BMC");
    list->setRootIsDecorated(false);
    list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    QList<quint32> addresses = m_web.values();
    std::sort(addresses.begin(), addresses.end());
    for (quint32 address : addresses) {
        QTreeWidgetItem *item = new QTreeWidgetItem(list);
        item->setText(0, formatIPv4(address));
        item->setData(0, Qt::UserRole, address);
        item->setText(1, m_names.value(address));
        bool isBmc = m_redfish.contains(address);
        item->setText(2, isBmc ? m_redfish.value(address) : QStringLiteral("Web only, not Redfish"));
        if (forwarded.contains(address)) {
            item->setText(2, QString("Forwarded already, on localhost:%1").arg(forwarded.value(address)));
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            item->setCheckState(0, Qt::Unchecked);
        } else {
            item->setCheckState(0, isBmc || m_noCurl ? Qt::Checked : Qt::Unchecked);
        }
    }
    layout->addWidget(list, 1);

    QRadioButton *perBmc = new QRadioButton("A local port for each checked BMC, from port", &dialog);
    QSpinBox *firstPort = new QSpinBox(&dialog);
    firstPort->setRange(1024, 65535);
    firstPort->setValue(18443);
    QHBoxLayout *perBmcRow = new QHBoxLayout();
    perBmcRow->addWidget(perBmc);
    perBmcRow->addWidget(firstPort);
    perBmcRow->addStretch();
    layout->addLayout(perBmcRow);
    QLabel *perBmcNote = new QLabel("Each opens from the 🔀 tunnels button of the SSH tab, "
                                    "e.g. https://localhost:18443.", &dialog);
    perBmcNote->setStyleSheet("QLabel { color: #666; margin-left: 22px; }");
    layout->addWidget(perBmcNote);

    QRadioButton *useSocks = new QRadioButton(&dialog);
    QSpinBox *socksPort = new QSpinBox(&dialog);
    socksPort->setRange(1024, 65535);
    QList<int> freeSocks = pickLocalPorts(1, 1080, taken, canBindLocally);
    socksPort->setValue(freeSocks.isEmpty() ? 1080 : freeSocks.first());
    if (socks) {
        useSocks->setText(QString("Its SOCKS proxy on port %1, for all of them").arg(socks->listenPort));
        socksPort->hide();
    } else {
        useSocks->setText("One SOCKS proxy for all of them, on port");
    }
    QHBoxLayout *socksRow = new QHBoxLayout();
    socksRow->addWidget(useSocks);
    socksRow->addWidget(socksPort);
    socksRow->addStretch();
    QPushButton *copyButton = new QPushButton("📋 Copy BMC addresses", &dialog);
    copyButton->setToolTip("Copies https://<IP>/ of each checked or already forwarded BMC, "
                           "to open through the proxy");
    socksRow->addWidget(copyButton);
    layout->addLayout(socksRow);
    QLabel *socksNote = new QLabel("Simplest for a whole rack: one port. Tunnels > \"Open browser through "
                                   "this proxy\" then opens each BMC by its own IP.", &dialog);
    socksNote->setWordWrap(true);
    socksNote->setStyleSheet("QLabel { color: #666; margin-left: 22px; }");
    layout->addWidget(socksNote);

    QButtonGroup *mode = new QButtonGroup(&dialog);
    mode->addButton(perBmc);
    mode->addButton(useSocks);
    (socks ? useSocks : perBmc)->setChecked(true);

    QCheckBox *connectNow = new QCheckBox(QString("Open an SSH tab to %1 now, to start them").arg(via.name), &dialog);
    connectNow->setChecked(true);
    layout->addWidget(connectNow);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QPushButton *okButton = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    auto checkedAddresses = [list]() {
        QList<quint32> result;
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = list->topLevelItem(i);
            if (item->checkState(0) == Qt::Checked) result << item->data(0, Qt::UserRole).toUInt();
        }
        return result;
    };
    // The BMCs to open through the proxy: the checked ones and those already forwarded
    auto copyableAddresses = [list]() {
        QList<quint32> result;
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = list->topLevelItem(i);
            if (item->checkState(0) == Qt::Checked || !(item->flags() & Qt::ItemIsEnabled)) {
                result << item->data(0, Qt::UserRole).toUInt();
            }
        }
        return result;
    };
    auto update = [=]() {
        int checked = checkedAddresses().size();
        firstPort->setEnabled(perBmc->isChecked());
        // Picking the SOCKS proxy the connection already has adds nothing to start
        connectNow->setVisible(perBmc->isChecked() || !socks);
        socksPort->setEnabled(useSocks->isChecked());
        copyButton->setEnabled(!copyableAddresses().isEmpty());
        if (perBmc->isChecked()) {
            okButton->setText(checked == 1 ? QString("Add 1 Tunnel") : QString("Add %1 Tunnels").arg(checked));
            okButton->setEnabled(checked > 0);
        } else {
            okButton->setText(socks ? QString("OK") : QString("Add SOCKS Proxy"));
            okButton->setEnabled(true);
        }
    };
    connect(list, &QTreeWidget::itemChanged, &dialog, update);
    connect(perBmc, &QRadioButton::toggled, &dialog, update);
    connect(copyButton, &QPushButton::clicked, &dialog, [copyableAddresses]() {
        QStringList urls;
        for (quint32 address : copyableAddresses()) urls << "https://" + formatIPv4(address) + "/";
        QApplication::clipboard()->setText(urls.join('\n'));
    });
    update();

    if (InWindowDialog::exec(&dialog) != QDialog::Accepted) return;

    QList<SSHTunnel> added;
    if (perBmc->isChecked()) {
        QList<quint32> chosen = checkedAddresses();
        QList<int> ports = pickLocalPorts(chosen.size(), firstPort->value(), taken, canBindLocally);
        for (int i = 0; i < chosen.size() && i < ports.size(); ++i) {
            SSHTunnel t;
            t.type = SSHTunnel::Local;
            t.listenPort = ports[i];
            t.targetHost = formatIPv4(chosen[i]);
            t.targetPort = 443;
            t.name = m_names.value(chosen[i]);
            if (t.name.isEmpty()) t.name = "BMC " + t.targetHost;
            added << t;
        }
        showStatus(QString("Added %1 BMC tunnel(s) to %2, on local ports %3%4.")
                       .arg(added.size()).arg(via.name)
                       .arg(added.isEmpty() ? 0 : added.first().listenPort)
                       .arg(added.size() > 1 ? QString("-%1").arg(added.last().listenPort) : QString()));
    } else {
        // The proxy browser offers these BMCs as its start page, last-checked first
        int port = socks ? socks->listenPort : socksPort->value();
        QList<quint32> chosen = checkedAddresses();
        for (int i = chosen.size() - 1; i >= 0; --i)
            ProxyBrowser::remember(port, "https://" + formatIPv4(chosen[i]) + "/");
    }
    if (!perBmc->isChecked() && !socks) {
        SSHTunnel t;
        t.type = SSHTunnel::Dynamic;
        t.listenPort = socksPort->value();
        t.targetHost.clear();
        t.targetPort = 0;
        t.name = "BMCs";
        added << t;
        showStatus(QString("Added a SOCKS proxy on port %1 to %2. Once its SSH tab is logged in, "
                           "Tunnels > \"Open browser through this proxy\" opens the BMCs by IP.")
                       .arg(t.listenPort).arg(via.name));
    }
    emit addTunnels(via, added, !connectNow->isHidden() && connectNow->isChecked());
}
