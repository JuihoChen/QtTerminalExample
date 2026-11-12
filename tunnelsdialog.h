#ifndef TUNNELSDIALOG_H
#define TUNNELSDIALOG_H

#include <QDialog>
#include <QList>

#include "sshconnection.h"

class QLabel;
class QPushButton;
class QTableWidget;

// Edits the port forwards that start with a saved connection's SSH tabs
class TunnelsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TunnelsDialog(const SSHConnection &connection, QWidget *parent = nullptr);

    QList<SSHTunnel> tunnels() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onAddTunnel();
    void onAddSocksTunnel();
    void onRemoveTunnel();
    void updateTunnelHint();
    void onAccepted();

private:
    void addTunnelRow(const SSHTunnel &tunnel);
    SSHTunnel tunnelFromRow(int row) const;
    int tunnelRowOf(QWidget *cellWidget) const;
    void updateTunnelRow(int row);

    QTableWidget *tunnelTable;
    QPushButton *removeTunnelButton;
    QLabel *tunnelHintLabel;
};

#endif // TUNNELSDIALOG_H
