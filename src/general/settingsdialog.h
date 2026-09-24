#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

public slots:
    void accept() override;

private:
    void saveConfig();
    void toggleLogin();
    void updateStatus();
    void updateRedirectUri();

    QLineEdit *m_clientId;
    QSpinBox *m_port;
    QLineEdit *m_redirectUri;
    QLabel *m_status;
    QPushButton *m_loginButton;
};

#endif
