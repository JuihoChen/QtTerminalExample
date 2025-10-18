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

void TerminalWindow::downloadFileFromSSH(const SSHConnection &connection)
{
    // First: Detect remote directory
    QProgressDialog *detectDialog = new QProgressDialog("Detecting remote directory...", "Cancel", 0, 0, this);
    detectDialog->setModal(true);
    detectDialog->show();
    
    detectRemoteWorkingDirectory(connection, [this, connection, detectDialog](const QString &remotePath) {
        detectDialog->hide();
        detectDialog->deleteLater();
        
        // Second: Show file browser to select remote file
        showRemoteFileBrowser(connection, remotePath, [this, connection](const QString &selectedRemoteFile) {
            if (selectedRemoteFile.isEmpty()) return;
            
            // Third: Choose local save location
            QString fileName = QFileInfo(selectedRemoteFile).fileName();
            QString localFile = QFileDialog::getSaveFileName(this,
                QString("Save '%1' from %2").arg(fileName, connection.name),
                QDir::homePath() + "/" + fileName,
                "All Files (*)");
            
            if (localFile.isEmpty()) return;
            
            // Fourth: Download the file
            performSCPDownload(connection, selectedRemoteFile, localFile);
        });
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
    QString scpCommand;
    if (!connection.password.isEmpty()) {
        scpCommand = QString("sshpass -p %1 ").arg(CommandSafetyHelper::escapeShellArgument(connection.password));
    }

    if (connection.port == 22) {
        scpCommand += QString("scp -o ServerAliveInterval=60 -o ServerAliveCountMax=3 "
                             "-o StrictHostKeyChecking=accept-new %1 %2@%3:%4")
                     .arg(CommandSafetyHelper::escapeShellArgument(localFile),
                          CommandSafetyHelper::escapeShellArgument(connection.username),
                          CommandSafetyHelper::escapeShellArgument(connection.host),
                          CommandSafetyHelper::escapeShellArgument(remotePath));
    } else {
        scpCommand += QString("scp -o ServerAliveInterval=60 -o ServerAliveCountMax=3 "
                             "-o StrictHostKeyChecking=accept-new -P %1 %2 %3@%4:%5")
                     .arg(connection.port)
                     .arg(CommandSafetyHelper::escapeShellArgument(localFile),
                          CommandSafetyHelper::escapeShellArgument(connection.username),
                          CommandSafetyHelper::escapeShellArgument(connection.host),
                          CommandSafetyHelper::escapeShellArgument(remotePath));
    }
    
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
    
    scpProcess->start("/bin/bash", QStringList() << "-c" << scpCommand);
}

void TerminalWindow::performSCPDownload(const SSHConnection &connection, const QString &remoteFile, const QString &localFile)
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
    scpProgressDialog = new QProgressDialog("Downloading file...", "Cancel", 0, 0, this);
    scpProgressDialog->setWindowTitle("SCP Download");
    scpProgressDialog->setModal(true);
    scpProgressDialog->show();
    
    // Build safe SCP command for DOWNLOAD
    QString scpCommand;
    if (!connection.password.isEmpty()) {
        scpCommand = QString("sshpass -p %1 ").arg(CommandSafetyHelper::escapeShellArgument(connection.password));
    }

    if (connection.port == 22) {
        scpCommand += QString("scp -o ServerAliveInterval=60 -o ServerAliveCountMax=3 "
                             "-o StrictHostKeyChecking=accept-new %1@%2:%3 %4")
                     .arg(CommandSafetyHelper::escapeShellArgument(connection.username),
                          CommandSafetyHelper::escapeShellArgument(connection.host),
                          CommandSafetyHelper::escapeShellArgument(remoteFile),
                          CommandSafetyHelper::escapeShellArgument(localFile));
    } else {
        scpCommand += QString("scp -o ServerAliveInterval=60 -o ServerAliveCountMax=3 "
                             "-o StrictHostKeyChecking=accept-new -P %1 %2@%3:%4 %5")
                     .arg(connection.port)
                     .arg(CommandSafetyHelper::escapeShellArgument(connection.username),
                          CommandSafetyHelper::escapeShellArgument(connection.host),
                          CommandSafetyHelper::escapeShellArgument(remoteFile),
                          CommandSafetyHelper::escapeShellArgument(localFile));
    }

    // Execute with improved error handling
    QProcess *scpProcess = new QProcess(this);
    
    connect(scpProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
            [this, scpProcess, connection, remoteFile](int exitCode, QProcess::ExitStatus exitStatus) {
        Q_UNUSED(exitStatus)
        scpProgressDialog->hide();
        scpProgressDialog->deleteLater();
        
        if (exitCode == 0) {
            statusBar()->showMessage(QString("✅ File downloaded successfully from %1").arg(connection.name), 5000);
            QMessageBox::information(this, "Download Complete", 
                QString("File '%1' downloaded successfully from %2")
                .arg(QFileInfo(remoteFile).fileName(), connection.name));
        } else {
            QString error = scpProcess->readAllStandardError();
            QString friendlyError = SSHErrorHandler::getErrorDescription(exitCode);
            statusBar()->showMessage("❌ Download failed", 5000);
            QMessageBox::warning(this, "Download Failed", 
                QString("Failed to download file from %1:\n%2\n\nTechnical details:\n%3")
                .arg(connection.name, friendlyError, error));
        }
        
        scpProcess->deleteLater();
    });
    
    connect(scpProgressDialog, &QProgressDialog::canceled, [scpProcess]() {
        scpProcess->kill();
    });
    
    scpProcess->start("/bin/bash", QStringList() << "-c" << scpCommand);
}

void TerminalWindow::browseRemoteFiles(const SSHConnection &connection)
{
    // Show progress while detecting remote directory
    QProgressDialog *detectDialog = new QProgressDialog("Detecting remote directory...", "Cancel", 0, 0, this);
    detectDialog->setModal(true);
    detectDialog->show();
    
    // Detect the actual current remote working directory
    detectRemoteWorkingDirectory(connection, [this, connection, detectDialog](const QString &remotePath) {
        detectDialog->hide();
        detectDialog->deleteLater();
        
        // Directory browser dialog with actual current directory
        bool ok;
        QString dialogTitle = QString("Browse Remote Directory on %1").arg(connection.name);
        QString dialogLabel = QString("Directory path (current: %1):").arg(remotePath);
        
        QString browsePath = QInputDialog::getText(this, dialogTitle, dialogLabel,
            QLineEdit::Normal, remotePath, &ok);
        
        if (!ok || browsePath.isEmpty()) return;
        
        // Build SSH ls command
        QString lsCommand;
        if (!connection.password.isEmpty()) {
            lsCommand = QString("sshpass -p '%1' ").arg(connection.password);
        }
        
        if (connection.port == 22) {
            lsCommand += QString("ssh %1@%2 'ls -la \"%3\"'")
                        .arg(connection.username, connection.host, browsePath);
        } else {
            lsCommand += QString("ssh -p %1 %2@%3 'ls -la \"%4\"'")
                        .arg(connection.port)
                        .arg(connection.username, connection.host, browsePath);
        }
        
        // Execute and show results
        QProcess *lsProcess = new QProcess(this);
        connect(lsProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
                [this, lsProcess, connection, browsePath](int exitCode, QProcess::ExitStatus exitStatus) {
            Q_UNUSED(exitStatus)
            if (exitCode == 0) {
                QString output = lsProcess->readAllStandardOutput();
                
                // Create a simple dialog to show file listing
                QDialog dialog(this);
                dialog.setWindowTitle(QString("Remote Files: %1:%2").arg(connection.host, browsePath));
                dialog.resize(600, 400);
                
                QVBoxLayout *layout = new QVBoxLayout(&dialog);
                QTextEdit *textEdit = new QTextEdit(&dialog);
                textEdit->setReadOnly(true);
                textEdit->setFont(QFont("Monospace", 10));
                textEdit->setPlainText(output);
                layout->addWidget(textEdit);
                
                QPushButton *closeBtn = new QPushButton("Close", &dialog);
                connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
                layout->addWidget(closeBtn);
                
                dialog.exec();
            } else {
                QString error = lsProcess->readAllStandardError();
                QMessageBox::warning(this, "Browse Failed", 
                    QString("Failed to browse directory on %1:\n%2")
                    .arg(connection.name, error));
            }
            lsProcess->deleteLater();
        });
        
        lsProcess->start("/bin/bash", QStringList() << "-c" << lsCommand);
    });
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
    
    pwdProcess->start("/bin/bash", QStringList() << "-c" << pwdCommand);
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
