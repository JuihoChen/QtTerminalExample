#ifndef HOSTSCANTAB_H
#define HOSTSCANTAB_H

#include <QWidget>
#include <QHash>
#include <QList>
#include <QSet>
#include <QStringList>

#include "sshconnection.h"

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QLineEdit;
class QProcess;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QThread;
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

class SftpWorker;

// A tab beside the terminals (like mRemoteNG's Port Scan) that finds hosts with SSH open in an IPv4 range, from this computer or through
// a saved SSH connection, and imports them as new connections. Both run the
// same shell script (ping sweep, TCP check of the SSH port, then the ARP
// table for MAC addresses); it needs only standard Linux tools.
class HostScanTab : public QWidget
{
    Q_OBJECT

public:
    HostScanTab(const QList<SSHConnection> &connections, const QStringList &folders,
                   QWidget *parent = nullptr);
    ~HostScanTab() override;

    // Preselect where to scan from (an index into connections, or -1 for
    // this computer) and which folder to import into
    void setScanFrom(int connectionIndex);
    void setImportFolder(const QString &folder);
    // Keeps "Scan from" and the saved-host marks in step with the connection list
    void setConnections(const QList<SSHConnection> &connections, const QStringList &folders);
    bool isScanning() const { return m_scanning; }

    // Builds the scan script; public so it can be tested on its own
    static QString scanScript(quint32 start, quint32 end, int port, int timeoutSeconds);
    static bool parseIPv4(const QString &text, quint32 *address);
    static QString formatIPv4(quint32 address);

signals:
    // The new connections to save; hosts that already have one are left out
    void importConnections(const QList<SSHConnection> &connections);

private slots:
    void onScanFromChanged();
    void startScan();
    void stopScan();
    void importChecked();
    void updateImportButton();

private:
    void setDefaultRange();
    void handleOutput(const QByteArray &data);
    void handleLine(const QString &line);
    void finishScan(const QString &error);
    QTreeWidgetItem *rowFor(quint32 address);
    void setRowSsh(QTreeWidgetItem *row, bool open);
    void setScanning(bool scanning);
    void stopRemoteWorker();
    const SSHConnection *scanFromConnection() const;

    QList<SSHConnection> m_connections;
    QSet<QString> m_savedHosts;

    QLineEdit *m_startEdit;
    QLineEdit *m_endEdit;
    QSpinBox *m_portSpin;
    QSpinBox *m_timeoutSpin;
    QComboBox *m_scanFromCombo;
    QLabel *m_scanFromNote;
    QPushButton *m_scanButton;
    QPushButton *m_stopButton;
    QProgressBar *m_progress;
    QLabel *m_statusLabel;
    QTreeWidget *m_results;
    QComboBox *m_folderCombo;
    QLineEdit *m_usernameEdit;
    QPushButton *m_importButton;
    bool m_rangeEdited;

    // Scan in progress
    QProcess *m_process;
    QThread *m_workerThread;
    SftpWorker *m_worker;
    bool m_scanning;
    bool m_stopRequested;
    QByteArray m_pending;      // Output after the last full line
    enum Section { Probes, Arp, Self } m_section;
    quint32 m_rangeStart;
    quint32 m_rangeEnd;
    int m_scanPort;
    int m_done;
    bool m_noPortCheck;
    bool m_noPing;
    QHash<quint32, QTreeWidgetItem *> m_rows;
    QHash<quint32, QString> m_macs;   // From the ARP table and the scanning host's own addresses
};

#endif // HOSTSCANTAB_H
