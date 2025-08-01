// ============ connectiondialog.h ============
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

private:
    void setupUI();
    void populateFields(const SSHConnection &connection);
    
    QLineEdit *nameEdit;
    QLineEdit *hostEdit;
    QLineEdit *usernameEdit;
    QSpinBox *portSpinBox;
    QComboBox *folderCombo;
    QPushButton *testButton;
    QDialogButtonBox *buttonBox;
    
    bool editMode;
};

#endif // CONNECTIONDIALOG_H