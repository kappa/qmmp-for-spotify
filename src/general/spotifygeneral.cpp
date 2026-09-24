#include <QApplication>
#include <QMenu>
#include <qmmpui/uihelper.h>
#include "devicedialog.h"
#include "spotifyauth.h"
#include "spotifyclient.h"
#include "spotifygeneral.h"

SpotifyGeneral::SpotifyGeneral(QObject *parent) : QObject(parent)
{
    SpotifyClient *client = SpotifyClient::instance();
    connect(client, &SpotifyClient::notice, this, &SpotifyGeneral::showNotice);

    m_menu = new QMenu(tr("Spotify"));
    m_menu->addAction(tr("Device…"), this, &SpotifyGeneral::chooseDevice);
    UiHelper::instance()->addAction(m_menu->menuAction(), UiHelper::TOOLS_MENU);
}

SpotifyGeneral::~SpotifyGeneral()
{
    UiHelper::instance()->removeAction(m_menu->menuAction());
    delete m_menu;
}

bool SpotifyGeneral::requireLogin()
{
    if(SpotifyClient::instance()->auth()->hasCredentials())
        return true;
    showNotice(tr("Spotify: log in first (Settings → Plugins → Spotify Plugin → Preferences)"));
    return false;
}

void SpotifyGeneral::chooseDevice()
{
    if(!requireLogin())
        return;
    DeviceDialog dialog(QApplication::activeWindow());
    dialog.exec();
}

void SpotifyGeneral::showNotice(const QString &message)
{
    if(!m_notice)
    {
        m_notice = new QMessageBox(QMessageBox::Information, tr("Spotify"), QString(), QMessageBox::Ok);
        m_notice->setModal(false);
        m_notice->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_notice->setText(message);
    m_notice->show();
    m_notice->raise();
}
