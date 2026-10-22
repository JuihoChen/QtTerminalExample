#ifndef INWINDOWDIALOG_H
#define INWINDOWDIALOG_H

#include <QLineEdit>
#include <QMessageBox>

class QDialog;

// Modal dialogs that stay usable under WSLg.
//
// WSLg turns each Wayland window into a separate Windows window and doesn't
// keep a dialog above the window that opened it, so clicking the main window
// hides the dialog behind it while the main window still takes input. There
// these functions show the dialog as a panel inside the main window instead.
// Everywhere else they behave exactly like QDialog::exec() and the
// QMessageBox / QInputDialog functions of the same name.
namespace InWindowDialog {

// Like dialog->exec(). Without showTitle the panel has no title row, for a
// dialog that already names itself in its content.
int exec(QDialog *dialog, bool showTitle = true);

QMessageBox::StandardButton question(QWidget *parent, const QString &title, const QString &text,
                                     QMessageBox::StandardButtons buttons = QMessageBox::Yes | QMessageBox::No,
                                     QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);
QMessageBox::StandardButton information(QWidget *parent, const QString &title, const QString &text,
                                        QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                        QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);
QMessageBox::StandardButton warning(QWidget *parent, const QString &title, const QString &text,
                                    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);
QMessageBox::StandardButton critical(QWidget *parent, const QString &title, const QString &text,
                                     QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                     QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);

QString getText(QWidget *parent, const QString &title, const QString &label,
                QLineEdit::EchoMode mode = QLineEdit::Normal,
                const QString &text = QString(), bool *ok = nullptr);

} // namespace InWindowDialog

#endif // INWINDOWDIALOG_H
