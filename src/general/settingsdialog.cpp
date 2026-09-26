#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include "settingsdialog.h"
#include "spotifyapi.h"
#include "spotifyauth.h"
#include "spotifyclient.h"
#include "spotifypoller.h"

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Spotify Plugin Settings"));
    SpotifyClient::instance()->poller()->pollOnInteraction();
    SpotifyClient *client = SpotifyClient::instance();

    m_clientId = new QLineEdit(client->clientId(), this);
    m_port = new QSpinBox(this);
    m_port->setRange(1024, 65535);
    m_port->setValue(client->port());
    m_redirectUri = new QLineEdit(this);
    m_redirectUri->setReadOnly(true);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_loginButton = new QPushButton(this);

    auto *help = new QLabel(tr("Create an app at <a href=\"https://developer.spotify.com/dashboard\">developer.spotify.com/dashboard</a> "
                               "(Web API), add the redirect URI below to it and paste its client ID here."), this);
    help->setOpenExternalLinks(true);
    help->setWordWrap(true);

    auto *form = new QFormLayout;
    form->addRow(tr("Client ID:"), m_clientId);
    form->addRow(tr("Redirect port:"), m_port);
    form->addRow(tr("Redirect URI:"), m_redirectUri);
    form->addRow(m_status, m_loginButton);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(help);
    layout->addLayout(form);
    layout->addWidget(buttons);

    connect(m_port, &QSpinBox::valueChanged, this, &SettingsDialog::updateRedirectUri);
    connect(m_loginButton, &QPushButton::clicked, this, &SettingsDialog::toggleLogin);
    connect(client->auth(), &SpotifyAuth::loggedIn, this, &SettingsDialog::updateStatus);
    connect(client->auth(), &SpotifyAuth::loggedOut, this, &SettingsDialog::updateStatus);
    connect(client->auth(), &SpotifyAuth::loginFailed, this, [this](const QString &message) {
        m_status->setText(message);
        m_loginButton->setEnabled(true);
    });

    updateRedirectUri();
    updateStatus();
}

void SettingsDialog::accept()
{
    saveConfig();
    QDialog::accept();
}

void SettingsDialog::saveConfig()
{
    SpotifyClient::instance()->setCredentialsConfig(m_clientId->text().trimmed(), quint16(m_port->value()));
}

void SettingsDialog::toggleLogin()
{
    SpotifyAuth *auth = SpotifyClient::instance()->auth();
    saveConfig();
    if(auth->hasCredentials())
    {
        auth->logout();
        return;
    }
    m_status->setText(tr("Waiting for the browser…"));
    m_loginButton->setEnabled(false);
    auth->login();
}

void SettingsDialog::updateStatus()
{
    SpotifyClient *client = SpotifyClient::instance();
    m_loginButton->setEnabled(true);
    if(!client->auth()->hasCredentials())
    {
        m_status->setText(tr("Not logged in"));
        m_loginButton->setText(tr("Log In"));
        return;
    }
    m_status->setText(tr("Logged in"));
    m_loginButton->setText(tr("Log Out"));
    QPointer<SettingsDialog> self(this);
    client->api()->displayName([self](const ApiResult &result, const QString &name) {
        if(self && result.ok)
            self->m_status->setText(tr("Logged in as %1").arg(name));
    });
}

void SettingsDialog::updateRedirectUri()
{
    m_redirectUri->setText(SpotifyAuth::redirectUri(quint16(m_port->value())));
}
