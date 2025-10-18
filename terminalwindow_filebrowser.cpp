// Remote file browser functionality
#include "terminalwindow.h"
#include "commandsafety.h"
#include "ssherrorhandler.h"

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressDialog>
#include <QProcess>
#include <QMessageBox>
#include <QFileInfo>
#include <QRegExp>

// Add this new method to show a proper remote file browser with file selection
void TerminalWindow::showRemoteFileBrowser(const SSHConnection &connection, const QString &remotePath, 
                                          std::function<void(const QString&)> callback)
{
    // Build safe SSH ls command using the helper
    QString lsCommand = CommandSafetyHelper::buildSafeSSHCommand(connection, 
        QString("ls -la %1").arg(CommandSafetyHelper::escapeShellArgument(remotePath)));
    
    // Create progress dialog for file listing
    QProgressDialog *listDialog = new QProgressDialog("Loading remote files...", "Cancel", 0, 0, this);
    listDialog->setModal(true);
    listDialog->show();
    
    // Execute command to get file listing
    QProcess *lsProcess = new QProcess(this);
    connect(lsProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
            [this, lsProcess, connection, remotePath, callback, listDialog](int exitCode, QProcess::ExitStatus exitStatus) {
        Q_UNUSED(exitStatus)
        
        listDialog->hide();
        listDialog->deleteLater();
        
        if (exitCode != 0) {
            QString error = lsProcess->readAllStandardError();
            QString friendlyError = SSHErrorHandler::getErrorDescription(exitCode);
            QMessageBox::warning(this, "Browse Failed", 
                QString("Failed to browse directory on %1:\n%2\n\nTechnical details:\n%3")
                .arg(connection.name, friendlyError, error));
            callback(QString());
            lsProcess->deleteLater();
            return;
        }
        
        QString output = lsProcess->readAllStandardOutput();
        
        // Parse the ls output to create a file selection dialog
        QStringList lines = output.split('\n', Qt::SkipEmptyParts);
        QStringList files;
        QStringList fileDetails;
        
        // Skip the first line if it shows total
        int startIndex = (lines.size() > 0 && lines[0].startsWith("total")) ? 1 : 0;
        
        for (int i = startIndex; i < lines.size(); ++i) {
            QString line = lines[i].trimmed();
            if (line.isEmpty()) continue;
            
            // Parse ls -la output: permissions user group size date time filename
            QStringList parts = line.split(QRegExp("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() < 9) continue;
            
            QString permissions = parts[0];
            QString filename = parts.mid(8).join(" "); // Handle filenames with spaces
            
            // Skip . and .. entries
            if (filename == "." || filename == "..") continue;
            
            QString fullPath = remotePath;
            if (!fullPath.endsWith("/")) fullPath += "/";
            fullPath += filename;
            
            // For directories, ensure the path ends with /
            if (permissions.startsWith('d')) {
                if (!fullPath.endsWith("/")) fullPath += "/";
            }
            
            files.append(fullPath);
            
            // Create display text with file info
            QString displayText;
            if (permissions.startsWith('d')) {
                displayText = QString("📁 %1/").arg(filename);
            } else if (permissions.contains('x')) {
                displayText = QString("⚙️ %1").arg(filename);
            } else {
                displayText = QString("📄 %1").arg(filename);
            }
            
            // Add size info for files
            if (!permissions.startsWith('d') && parts.size() >= 5) {
                displayText += QString(" (%1)").arg(parts[4]);
            }
            
            fileDetails.append(displayText);
        }
        
        if (files.isEmpty()) {
            QMessageBox::information(this, "Empty Directory", 
                QString("No files found in %1").arg(remotePath));
            callback(QString());
            lsProcess->deleteLater();
            return;
        }
        
        // Create file selection dialog
        QDialog fileDialog(this);
        fileDialog.setWindowTitle(QString("Select File from %1:%2").arg(connection.host, remotePath));
        fileDialog.resize(600, 400);
        
        QVBoxLayout *layout = new QVBoxLayout(&fileDialog);
        
        // Add path label
        QLabel *pathLabel = new QLabel(QString("Remote path: %1").arg(remotePath), &fileDialog);
        pathLabel->setStyleSheet("QLabel { font-weight: bold; color: #666; }");
        layout->addWidget(pathLabel);
        
        // File list
        QListWidget *fileList = new QListWidget(&fileDialog);
        for (int i = 0; i < files.size(); ++i) {
            QListWidgetItem *item = new QListWidgetItem(fileDetails[i]);
            item->setData(Qt::UserRole, files[i]); // Store full path
            fileList->addItem(item);
        }
        layout->addWidget(fileList);
        
        // Manual path entry
        QHBoxLayout *pathLayout = new QHBoxLayout();
        pathLayout->addWidget(new QLabel("Or enter file path:", &fileDialog));
        QLineEdit *pathEdit = new QLineEdit(&fileDialog);
        pathEdit->setPlaceholderText("Enter full remote file path...");
        pathLayout->addWidget(pathEdit);
        layout->addLayout(pathLayout);
        
        // Buttons
        QHBoxLayout *buttonLayout = new QHBoxLayout();
        QPushButton *selectBtn = new QPushButton("Download Selected", &fileDialog);
        QPushButton *enterBtn = new QPushButton("Enter Directory", &fileDialog);
        QPushButton *upBtn = new QPushButton("📁 Up", &fileDialog);
        QPushButton *manualBtn = new QPushButton("Manual Path", &fileDialog);
        QPushButton *cancelBtn = new QPushButton("Cancel", &fileDialog);
        
        selectBtn->setEnabled(false);
        enterBtn->setEnabled(false);
        selectBtn->setStyleSheet("QPushButton { font-weight: bold; color: #0066cc; }");
        
        buttonLayout->addWidget(selectBtn);
        buttonLayout->addWidget(enterBtn);
        buttonLayout->addWidget(upBtn);
        buttonLayout->addWidget(manualBtn);
        buttonLayout->addStretch();
        buttonLayout->addWidget(cancelBtn);
        layout->addLayout(buttonLayout);
        
        // Connect signals
        connect(fileList, &QListWidget::itemSelectionChanged, [selectBtn, enterBtn, fileList]() {
            QListWidgetItem *current = fileList->currentItem();
            selectBtn->setEnabled(current != nullptr);
            
            if (current) {
                QString selectedPath = current->data(Qt::UserRole).toString();
                enterBtn->setEnabled(selectedPath.endsWith("/"));
            } else {
                enterBtn->setEnabled(false);
            }
        });
        
        connect(fileList, &QListWidget::itemDoubleClicked, [this, &fileDialog, callback, connection](QListWidgetItem *item) {
            QString selectedPath = item->data(Qt::UserRole).toString();
            
            // If it's a directory, navigate into it
            if (selectedPath.endsWith("/")) {
                fileDialog.accept(); // Close current dialog
                // Recursively open browser in the new directory
                showRemoteFileBrowser(connection, selectedPath, callback);
                return;
            }
            
            // If it's a file, select it for download
            callback(selectedPath);
            fileDialog.accept();
        });
        
        connect(selectBtn, &QPushButton::clicked, [&fileDialog, fileList, callback]() {
            QListWidgetItem *currentItem = fileList->currentItem();
            if (!currentItem) return;
            
            QString selectedFile = currentItem->data(Qt::UserRole).toString();
            
            // Check if it's a directory
            if (selectedFile.endsWith("/")) {
                QMessageBox::information(&fileDialog, "Directory Selected", 
                    "Cannot download a directory. Use 'Enter Directory' to browse or select a file.");
                return;
            }
            
            callback(selectedFile);
            fileDialog.accept();
        });
        
        connect(enterBtn, &QPushButton::clicked, [this, &fileDialog, fileList, callback, connection]() {
            QListWidgetItem *currentItem = fileList->currentItem();
            if (!currentItem) return;
            
            QString selectedDir = currentItem->data(Qt::UserRole).toString();
            if (!selectedDir.endsWith("/")) return;
            
            fileDialog.accept(); // Close current dialog
            showRemoteFileBrowser(connection, selectedDir, callback); // Open new browser in directory
        });
        
        connect(upBtn, &QPushButton::clicked, [this, &fileDialog, callback, connection, remotePath]() {
            // Go up one directory
            QString parentPath = remotePath;
            if (parentPath.endsWith("/")) parentPath.chop(1);
            
            int lastSlash = parentPath.lastIndexOf('/');
            if (lastSlash > 0) {
                parentPath = parentPath.left(lastSlash + 1);
            } else {
                parentPath = "/";
            }
            
            fileDialog.accept();
            showRemoteFileBrowser(connection, parentPath, callback);
        });
        
        connect(manualBtn, &QPushButton::clicked, [&fileDialog, pathEdit, callback]() {
            QString manualPath = pathEdit->text().trimmed();
            if (manualPath.isEmpty()) {
                QMessageBox::information(&fileDialog, "Empty Path", "Please enter a file path.");
                return;
            }
            
            callback(manualPath);
            fileDialog.accept();
        });
        
        connect(cancelBtn, &QPushButton::clicked, [&fileDialog, callback]() {
            callback(QString());
            fileDialog.reject();
        });
        
        fileDialog.exec();
        lsProcess->deleteLater();
    });
    
    connect(listDialog, &QProgressDialog::canceled, [lsProcess, callback]() {
        lsProcess->kill();
        callback(QString());
    });
    
    lsProcess->start("/bin/bash", QStringList() << "-c" << lsCommand);
}
