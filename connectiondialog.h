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

struct SSHConnection; // Forward declaration

class ConnectionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectionDialog(QWidget *parent = nullptr);
    ConnectionDialog(const SSHConnection &connection, QWidget *parent = nullptr);

    SSHConnection getConnection() const;
    void setAvailableFolders(const QStringList &folders);

private slots:
    void validateInput();
    void onTestConnection();
    void onShowPasswordChanged(bool show);
    void onBrowseIdentityFile();
    void onCheckIdentityFile();
    void updateKeyAuthState();

private:
    void setupUI();
    QWidget *createGeneralTab();
    QWidget *createSSHKeyTab();
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
    QPushButton *testButton;
    QDialogButtonBox *buttonBox;
    
    bool editMode;
};

#endif // CONNECTIONDIALOG_H