#ifndef CONNECTIONVALIDATOR_H
#define CONNECTIONVALIDATOR_H

#include <QString>
#include <QStringList>
#include "sshconnection.h"

class ConnectionValidator {
public:
    struct ValidationResult {
        bool isValid;
        QString errorMessage;
        QStringList warnings;
    };
    
    static ValidationResult validateConnection(const SSHConnection &connection) {
        ValidationResult result;
        result.isValid = true;
        
        // Basic validation
        if (connection.name.trimmed().isEmpty()) {
            result.isValid = false;
            result.errorMessage = "Connection name cannot be empty";
            return result;
        }
        
        if (connection.host.trimmed().isEmpty()) {
            result.isValid = false;
            result.errorMessage = "Host cannot be empty";
            return result;
        }
        
        if (connection.username.trimmed().isEmpty()) {
            result.isValid = false;
            result.errorMessage = "Username cannot be empty";
            return result;
        }
        
        if (connection.port < 1 || connection.port > 65535) {
            result.isValid = false;
            result.errorMessage = "Port must be between 1 and 65535";
            return result;
        }
        
        // Warnings for common issues
        if (connection.password.isEmpty()) {
            result.warnings.append("No password set - you'll need to enter it manually");
        }
        
        if (connection.port != 22) {
            result.warnings.append(QString("Using non-standard SSH port: %1").arg(connection.port));
        }
        
        return result;
    }
};

#endif // CONNECTIONVALIDATOR_H