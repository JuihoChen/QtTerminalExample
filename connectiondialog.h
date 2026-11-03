// ============ Updated connectiondialog.h with Password Field ============
#ifndef CONNECTIONDIALOG_H
#define CONNECTIONDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QGroupBox>
#include <QCheckBox>
#include <QTabWidget>
#include <QTableWidget>

#include "sshtunnel.h"

struct SSHConnection; // Forward declaration

class ConnectionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectionDialog(QWidget *parent = nullptr);
    ConnectionDialog(const SSHConnection &connection, QWidget *parent = nullptr);

    SSHConnection getConnection() const;
    void setAvailableFolders(const QStringList &folders);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void validateInput();
    void onTestConnection();
    void onShowPasswordChanged(bool show);
    void onBrowseIdentityFile();
    void onCheckIdentityFile();
    void updateKeyAuthState();
    void onAddTunnel();
    void onAddSocksTunnel();
    void onRemoveTunnel();
    void updateTunnelHint();
    void onAccepted();

private:
    void setupUI();
    QWidget *createGeneralTab();
    QWidget *createSSHKeyTab();
    QWidget *createTunnelsTab();
    void addTunnelRow(const SSHTunnel &tunnel);
    SSHTunnel tunnelFromRow(int row) const;
    int tunnelRowOf(QWidget *cellWidget) const;
    void updateTunnelRow(int row);
    QList<SSHTunnel> tunnels() const;
    void updateTunnelsTabTitle();
    void populateFields(const SSHConnection &connection);
    
    QLineEdit *nameEdit;
    QLineEdit *hostEdit;
    QLineEdit *usernameEdit;
    QLineEdit *passwordEdit;  // Added password field
    QCheckBox *showPasswordCheck;  // Added show password checkbox
    QSpinBox *portSpinBox;
    QComboBox *folderCombo;
    QTabWidget *detailsTabs;

    // SSH Key tab
    QCheckBox *useKeyAuthCheck;
    QLineEdit *identityFileEdit;
    QPushButton *browseIdentityButton;
    QPushButton *checkIdentityButton;
    QLabel *identityStatusLabel;
    QCheckBox *useAgentCheck;
    QLabel *agentStatusLabel;

    // Tunnels tab
    QWidget *tunnelsTab;
    QTableWidget *tunnelTable;
    QPushButton *removeTunnelButton;
    QLabel *tunnelHintLabel;

    QPushButton *testButton;
    QDialogButtonBox *buttonBox;
    
    bool editMode;
};

#endif // CONNECTIONDIALOG_H