// Per-connection SSH port forwards (-L, -R, -D), edited apart from the
// connection's login details
#include "tunnelsdialog.h"
#include "inwindowdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum TunnelColumn { ColOn, ColName, ColType, ColListen, ColHost, ColPort, ColPath, ColCount };
}

TunnelsDialog::TunnelsDialog(const SSHConnection &connection, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QString("Tunnels: %1").arg(connection.name));
    setModal(true);
    resize(820, 420);

    QWidget *tab = this;
    QVBoxLayout *layout = new QVBoxLayout(this);

    QLabel *intro = new QLabel(QString(
        "Port forwards start with every SSH tab of <b>%1</b> (%2@%3). "
        "The destination can be any host that server can reach, such as a BMC's own IP. "
        "To reach many BMCs, one <b>SOCKS proxy</b> is simpler than a forward for each.")
        .arg(connection.name.toHtmlEscaped(), connection.username.toHtmlEscaped(),
             connection.host.toHtmlEscaped()), tab);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    tunnelTable = new QTableWidget(0, ColCount, tab);
    tunnelTable->setHorizontalHeaderLabels({"On", "Name", "Type", "Listen port", "Destination host", "Port", "Web path"});
    tunnelTable->verticalHeader()->setVisible(false);
    tunnelTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    tunnelTable->setSelectionMode(QAbstractItemView::SingleSelection);
    tunnelTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    QHeaderView *header = tunnelTable->horizontalHeader();
    header->setSectionResizeMode(ColOn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColName, QHeaderView::Interactive);
    tunnelTable->setColumnWidth(ColName, 120);
    header->setSectionResizeMode(ColType, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColListen, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColHost, QHeaderView::Stretch);
    header->setSectionResizeMode(ColPort, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColPath, QHeaderView::Interactive);
    tunnelTable->setColumnWidth(ColPath, 110);
    header->setHighlightSections(false);
    tunnelTable->horizontalHeaderItem(ColName)->setToolTip(
        "Optional: what the tunnel leads to, such as a BMC's host name. Shown in the tunnels menu.");
    tunnelTable->horizontalHeaderItem(ColListen)->setToolTip(
        "Local and SOCKS: the port on this PC. Remote: the port opened on the server.");
    tunnelTable->horizontalHeaderItem(ColHost)->setToolTip(
        "Local: a host the server can reach (localhost is the server itself).\n"
        "Remote: a host this PC can reach (localhost is this PC).");
    tunnelTable->horizontalHeaderItem(ColPath)->setToolTip(
        "Optional, Local only: the page \"Open in browser\" goes to, such as /base-view");
    tunnelTable->setMinimumHeight(150);
    layout->addWidget(tunnelTable, 1);

    QHBoxLayout *buttons = new QHBoxLayout();
    QPushButton *addButton = new QPushButton("➕ Add Forward", tab);
    addButton->setToolTip("Add a port forward, such as localhost:8443 to a BMC's 443");
    QPushButton *addSocksButton = new QPushButton("🧦 Add SOCKS Proxy", tab);
    addSocksButton->setToolTip("Add a SOCKS proxy (-D) to reach a whole network through the server");
    removeTunnelButton = new QPushButton("➖ Remove", tab);
    connect(addButton, &QPushButton::clicked, this, &TunnelsDialog::onAddTunnel);
    connect(addSocksButton, &QPushButton::clicked, this, &TunnelsDialog::onAddSocksTunnel);
    connect(removeTunnelButton, &QPushButton::clicked, this, &TunnelsDialog::onRemoveTunnel);
    buttons->addWidget(addButton);
    buttons->addWidget(addSocksButton);
    buttons->addWidget(removeTunnelButton);
    buttons->addStretch();
    layout->addLayout(buttons);

    tunnelHintLabel = new QLabel(tab);
    tunnelHintLabel->setWordWrap(true);
    tunnelHintLabel->setStyleSheet("QLabel { color: #666; font-style: italic; }");
    layout->addWidget(tunnelHintLabel);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &TunnelsDialog::onAccepted);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);

    connect(tunnelTable, &QTableWidget::itemSelectionChanged, this, &TunnelsDialog::updateTunnelHint);
    for (const SSHTunnel &tunnel : connection.tunnels) addTunnelRow(tunnel);
    updateTunnelHint();
}

void TunnelsDialog::addTunnelRow(const SSHTunnel &tunnel)
{
    int row = tunnelTable->rowCount();
    tunnelTable->insertRow(row);

    QTableWidgetItem *onItem = new QTableWidgetItem();
    onItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    onItem->setCheckState(tunnel.enabled ? Qt::Checked : Qt::Unchecked);
    onItem->setToolTip("Start this forward with the SSH tab");
    tunnelTable->setItem(row, ColOn, onItem);

    QLineEdit *nameEdit = new QLineEdit(tunnelTable);
    nameEdit->setText(tunnel.name);
    nameEdit->setPlaceholderText("(optional)");
    tunnelTable->setCellWidget(row, ColName, nameEdit);

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

    QLineEdit *pathEdit = new QLineEdit(tunnelTable);
    pathEdit->setText(tunnel.webPath);
    pathEdit->setPlaceholderText("/ (optional)");
    tunnelTable->setCellWidget(row, ColPath, pathEdit);

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
    for (QWidget *w : QList<QWidget*>{nameEdit, typeCombo, listenSpin, hostEdit, portSpin, pathEdit}) {
        w->installEventFilter(this);
    }
    connect(listenSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, selectRow]() { selectRow(); updateTunnelHint(); });
    connect(portSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, selectRow]() { selectRow(); updateTunnelHint(); });
    connect(hostEdit, &QLineEdit::textEdited, this, [this, selectRow]() { selectRow(); updateTunnelHint(); });
    connect(pathEdit, &QLineEdit::textEdited, this, [this, selectRow]() { selectRow(); updateTunnelHint(); });

    updateTunnelRow(row);
    tunnelTable->resizeRowToContents(row);
}

bool TunnelsDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FocusIn) {
        int row = tunnelRowOf(qobject_cast<QWidget*>(watched));
        if (row >= 0 && tunnelTable->currentRow() != row) tunnelTable->selectRow(row);
    }
    return QDialog::eventFilter(watched, event);
}

int TunnelsDialog::tunnelRowOf(QWidget *cellWidget) const
{
    for (int row = 0; row < tunnelTable->rowCount(); ++row) {
        for (int col = ColName; col < ColCount; ++col) {
            if (tunnelTable->cellWidget(row, col) == cellWidget) return row;
        }
    }
    return -1;
}

SSHTunnel TunnelsDialog::tunnelFromRow(int row) const
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
    if (QLineEdit *nameEdit = qobject_cast<QLineEdit*>(tunnelTable->cellWidget(row, ColName))) {
        tunnel.name = nameEdit->text().trimmed();
    }
    QLineEdit *pathEdit = qobject_cast<QLineEdit*>(tunnelTable->cellWidget(row, ColPath));
    if (pathEdit && tunnel.type == SSHTunnel::Local) tunnel.webPath = pathEdit->text().trimmed();
    return tunnel;
}

QList<SSHTunnel> TunnelsDialog::tunnels() const
{
    QList<SSHTunnel> list;
    for (int row = 0; row < tunnelTable->rowCount(); ++row) list << tunnelFromRow(row);
    return list;
}

// A SOCKS proxy has no destination, so its host and port fields are disabled
void TunnelsDialog::updateTunnelRow(int row)
{
    if (row < 0) return;
    QComboBox *typeCombo = qobject_cast<QComboBox*>(tunnelTable->cellWidget(row, ColType));
    QLineEdit *hostEdit = qobject_cast<QLineEdit*>(tunnelTable->cellWidget(row, ColHost));
    QSpinBox *portSpin = qobject_cast<QSpinBox*>(tunnelTable->cellWidget(row, ColPort));
    if (!typeCombo || !hostEdit || !portSpin) return;

    bool dynamic = typeCombo->currentData().toInt() == SSHTunnel::Dynamic;
    hostEdit->setEnabled(!dynamic);
    portSpin->setEnabled(!dynamic);
    // Only a local forward opens in this PC's browser, so only it has a web path
    if (QWidget *pathEdit = tunnelTable->cellWidget(row, ColPath)) {
        pathEdit->setEnabled(typeCombo->currentData().toInt() == SSHTunnel::Local);
    }
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

void TunnelsDialog::updateTunnelHint()
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
        hint = QString("ssh -L %1: open %2://localhost:%3%4 on this PC to reach %5:%6 from the server.")
                   .arg(t.spec(), t.looksLikeHttps() ? "https" : "http")
                   .arg(t.listenPort).arg(t.urlPath() == "/" ? QString() : t.urlPath())
                   .arg(t.targetHost).arg(t.targetPort);
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

// The next free-looking port: one past the highest port already listed
static int nextListenPort(const QList<SSHTunnel> &tunnels, int fallback)
{
    int port = fallback;
    for (const SSHTunnel &t : tunnels) {
        if (t.listensLocally() && t.listenPort >= port) port = t.listenPort + 1;
    }
    return qMin(port, 65535);
}

void TunnelsDialog::onAddTunnel()
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

void TunnelsDialog::onAddSocksTunnel()
{
    SSHTunnel tunnel;
    tunnel.type = SSHTunnel::Dynamic;
    tunnel.listenPort = nextListenPort(tunnels(), 1080);
    tunnel.targetHost.clear();
    tunnel.targetPort = 0;
    addTunnelRow(tunnel);
    tunnelTable->selectRow(tunnelTable->rowCount() - 1);
}

void TunnelsDialog::onRemoveTunnel()
{
    int row = tunnelTable->currentRow();
    if (row < 0) return;
    tunnelTable->removeRow(row);
    updateTunnelHint();
}

// Checks the tunnels before saving, so a typo doesn't surface only at login
void TunnelsDialog::onAccepted()
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
        InWindowDialog::warning(this, "Tunnels",
            "Please fix these tunnels before saving:\n\n" + problems.join("\n"));
        return;
    }
    accept();
}
