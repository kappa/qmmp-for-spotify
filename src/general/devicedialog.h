#ifndef DEVICEDIALOG_H
#define DEVICEDIALOG_H

#include <QDialog>

class QLabel;
class QListWidget;

class DeviceDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DeviceDialog(QWidget *parent = nullptr);

public slots:
    void accept() override;

private:
    void refresh();

    QListWidget *m_list;
    QLabel *m_status;
};

#endif
