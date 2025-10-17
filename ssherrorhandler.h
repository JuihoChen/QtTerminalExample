#ifndef SSHERRORHANDLER_H
#define SSHERRORHANDLER_H

#include <QString>

// Enhanced Error Handling for SSH Operations
class SSHErrorHandler {
public:
    enum ErrorType {
        Success = 0,
        GeneralError = 1,
        AuthenticationFailed = 255,
        ConnectionRefused = 61,
        HostUnreachable = 113,
        TimeoutError = 124
    };
    
    static QString getErrorDescription(int exitCode) {
        switch (exitCode) {
        case Success:
            return "Operation completed successfully";
        case AuthenticationFailed:
            return "Authentication failed - check username/password";
        case ConnectionRefused:
            return "Connection refused - check host and port";
        case HostUnreachable:
            return "Host unreachable - check network connection";
        case TimeoutError:
            return "Operation timed out";
        default:
            return QString("Unknown error (code: %1)").arg(exitCode);
        }
    }
};

#endif // SSHERRORHANDLER_H