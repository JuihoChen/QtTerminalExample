// File transfer (SCP) functionality
#include "terminalwindow.h"
#include "commandsafety.h"
#include "ssherrorhandler.h"
#include "connectionvalidator.h"

#include <QFileDialog>
#include <QInputDialog>
#include <QProgressDialog>
#include <QProcess>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QStatusBar>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QRegExp>

void TerminalWindow::uploadFileToSSH(const SSHConnection &connection)
{
    // File selection dialog
    QString localFile = QFileDialog::getOpenFileName(this, 
        QString("Upload File to %1").arg(connection.name),
        QDir::homePath(),
        "All Files (*)");
    
    if (localFile.isEmpty()) return;
    
    // Show progress while detecting remote directory
    QProgressDialog *detectDialog = new QProgressDialog("Detecting remote directory...", "Cancel", 0, 0, this);
    detectDialog->setModal(true);
    detectDialog->show();
    
    // Detect the actual current remote working directory
    detectRemoteWorkingDirectory(connection, [this, connection, localFile, detectDialog](const QString &remotePath) {
        detectDialog->hide();
        detectDialog->deleteLater();
        
        // Build default remote file path
        QString defaultRemotePath = remotePath;
        if (!defaultRemotePath.endsWith("/")) {
            defaultRemotePath += "/";
        }
        defaultRemotePath += QFileInfo(localFile).fileName();
        
        // Remote path dialog with actual remote directory info
        bool ok;
        QString dialogTitle = QString("Upload to %1").arg(connection.name);
        QString dialogLabel = QString("Remote path (current directory: %1):")
                             .arg(remotePath);
        
        QString finalRemotePath = QInputDialog::getText(this, dialogTitle, dialogLabel,
            QLineEdit::Normal, defaultRemotePath, &ok);
        
        if (!ok || finalRemotePath.isEmpty()) return;
        
        // Perform SCP upload
        performSCPUpload(connection, localFile, finalRemotePath);
    });
}

void TerminalWindow::performSCPUpload(const SSHConnection &connection, 
                                     const QString &localFile, 
                                     const QString &remotePath)
{
    // Validate connection first
    auto validation = ConnectionValidator::validateConnection(connection);
    if (!validation.isValid) {
        QMessageBox::warning(this, "Invalid Connection", validation.errorMessage);
        return;
    }
    
    // Show warnings if any
    if (!validation.warnings.isEmpty()) {
        QString warningText = "Warnings:\n" + validation.warnings.join("\n");
        int ret = QMessageBox::question(this, "Connection Warnings", 
                                       warningText + "\n\nContinue anyway?",
                                       QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::No) return;
    }
    
    // Create progress dialog
    scpProgressDialog = new QProgressDialog("Uploading file...", "Cancel", 0, 0, this);
    scpProgressDialog->setWindowTitle("SCP Upload");
    scpProgressDialog->setModal(true);
    scpProgressDialog->show();

    // Build safe SCP command for UPLOAD
    QString scpCommand = CommandSafetyHelper::buildSafeSCPCommand(connection, localFile, remotePath, true);
    
    // Execute with improved error handling
    QProcess *scpProcess = new QProcess(this);
    
    connect(scpProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
            [this, scpProcess, connection, localFile](int exitCode, QProcess::ExitStatus exitStatus) {
        Q_UNUSED(exitStatus)
        scpProgressDialog->hide();
        scpProgressDialog->deleteLater();
        
        if (exitCode == 0) {
            statusBar()->showMessage(QString("✅ File uploaded successfully to %1").arg(connection.name), 5000);
            QMessageBox::information(this, "Upload Complete", 
                QString("File '%1' uploaded successfully to %2")
                .arg(QFileInfo(localFile).fileName(), connection.name));
        } else {
            QString error = scpProcess->readAllStandardError();
            QString friendlyError = SSHErrorHandler::getErrorDescription(exitCode);
            statusBar()->showMessage("❌ Upload failed", 5000);
            QMessageBox::warning(this, "Upload Failed", 
                QString("Failed to upload file to %1:\n%2\n\nTechnical details:\n%3")
                .arg(connection.name, friendlyError, error));
        }
        
        scpProcess->deleteLater();
    });
    
    connect(scpProgressDialog, &QProgressDialog::canceled, [scpProcess]() {
        scpProcess->kill();
    });
    
    CommandSafetyHelper::startWithPassword(scpProcess, connection, scpCommand);
}

QString TerminalWindow::getDefaultRemotePath(const SSHConnection &connection)
{
    // Always use the user's home directory as default, not the organizational folder
    return QString("/home/%1").arg(connection.username);
}

// Add this new method to detect the actual current remote directory
void TerminalWindow::detectRemoteWorkingDirectory(const SSHConnection &connection, 
                                                std::function<void(const QString&)> callback)
{
    // Build safe SSH pwd command using the helper
    QString pwdCommand = CommandSafetyHelper::buildSafeSSHCommand(connection, "pwd");
    
    // Execute command to get current directory
    QProcess *pwdProcess = new QProcess(this);
    connect(pwdProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
            [this, pwdProcess, connection, callback](int exitCode, QProcess::ExitStatus exitStatus) {
        Q_UNUSED(exitStatus)
        
        QString remotePath;
        if (exitCode == 0) {
            // Successfully got the remote directory
            remotePath = pwdProcess->readAllStandardOutput().trimmed();
            if (remotePath.isEmpty()) {
                remotePath = getDefaultRemotePath(connection);
            }
        } else {
            // Fallback to default if command failed
            remotePath = getDefaultRemotePath(connection);
        }
        
        callback(remotePath);
        pwdProcess->deleteLater();
    });
    
    CommandSafetyHelper::startWithPassword(pwdProcess, connection, pwdCommand);
}

// Alternative approach: Track terminal working directories
// Add this method to track the current directory of active SSH terminals
QString TerminalWindow::getCurrentRemoteDirectory(QTermWidget *terminal)
{
    if (!terminal) return QString();

    // Check if this is an SSH terminal
    bool isSSH = terminal->property("isSSHTerminal").toBool();
    if (!isSSH) return QString();

    // Get the connection info
    QVariant connectionData = terminal->property("sshConnection");
    if (!connectionData.canConvert<SSHConnection>()) return QString();
    
    SSHConnection connection = qvariant_cast<SSHConnection>(connectionData);

    // For now, return default path - you could enhance this by:
    // 1. Parsing terminal output to track 'cd' commands
    // 2. Sending 'pwd' command and capturing output
    // 3. Using a more sophisticated terminal state tracking

    return getDefaultRemotePath(connection);
}
