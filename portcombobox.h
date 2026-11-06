#ifndef PORTCOMBOBOX_H
#define PORTCOMBOBOX_H

#include <QComboBox>
#include <QIntValidator>
#include <QLineEdit>

// A port field: type any port, or pick a common one from the list.
// The list shows what each port is usually for ("8080  (HTTP alt)"),
// but the field itself only ever holds the number.
class PortComboBox : public QComboBox
{
public:
    explicit PortComboBox(QWidget *parent = nullptr) : QComboBox(parent) {
        setEditable(true);
        setInsertPolicy(QComboBox::NoInsert);   // typed ports are not added to the list
        setCompleter(nullptr);                  // no inline completion to "22  (SSH)"
        lineEdit()->setValidator(new QIntValidator(1, 65535, this));
        lineEdit()->setPlaceholderText("1-65535");

        static const struct { int port; const char *use; } common[] = {
            {22, "SSH"}, {443, "HTTPS"}, {8080, "HTTP alt"}, {8081, "HTTP alt"},
            {2222, "SSH alt"}, {80, "HTTP"}, {8443, "HTTPS alt"}, {8022, "SSH alt"},
        };
        for (const auto &c : common)
            addItem(QString("%1  (%2)").arg(c.port).arg(c.use), c.port);

        // Picking an item puts its label in the field, so put just the number back.
        // activated also fires when the same item is picked again after typing.
        auto showNumber = [this](int index) {
            if (index >= 0) setEditText(QString::number(itemData(index).toInt()));
        };
        connect(this, QOverload<int>::of(&QComboBox::currentIndexChanged), this, showNumber);
        connect(this, QOverload<int>::of(&QComboBox::activated), this, showNumber);

        setValue(22);
    }

    // The typed port, or 0 when the field is empty or out of range.
    int value() const {
        bool ok = false;
        const int port = currentText().trimmed().toInt(&ok);
        return ok && port >= 1 && port <= 65535 ? port : 0;
    }

    void setValue(int port) {
        setCurrentIndex(findData(port));        // -1 for a port not in the list
        setEditText(QString::number(port));
    }
};

#endif // PORTCOMBOBOX_H
