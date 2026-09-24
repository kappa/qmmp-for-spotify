#include <QApplication>
#include <QMenu>
#include <qmmpui/playlistmanager.h>
#include <qmmpui/playlistmodel.h>
#include <qmmpui/uihelper.h>
#include "devicedialog.h"
#include "follower.h"
#include "linkexpander.h"
#include "playlistdialog.h"
#include "spotifyapi.h"
#include "spotifyauth.h"
#include "spotifyclient.h"
#include "spotifygeneral.h"
#include "trackbuilder.h"

SpotifyGeneral::SpotifyGeneral(QObject *parent) : QObject(parent)
{
    SpotifyClient *client = SpotifyClient::instance();
    connect(client, &SpotifyClient::notice, this, &SpotifyGeneral::showNotice);

    m_menu = new QMenu(tr("Spotify"));
    m_menu->addAction(tr("Device…"), this, &SpotifyGeneral::chooseDevice);
    m_menu->addAction(tr("Load Playlist…"), this, &SpotifyGeneral::loadPlaylist);
    m_menu->addAction(tr("Load Liked Songs"), this, &SpotifyGeneral::loadLikedSongs);
    UiHelper::instance()->addAction(m_menu->menuAction(), UiHelper::TOOLS_MENU);

    new Follower(this);
    new LinkExpander(this);
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

void SpotifyGeneral::loadPlaylist()
{
    if(!requireLogin())
        return;
    PlayListDialog dialog(QApplication::activeWindow());
    if(dialog.exec() != QDialog::Accepted || !dialog.selected().isValid())
        return;
    const SpotifyPlaylist playlist = dialog.selected();
    QPointer<SpotifyGeneral> self(this);
    SpotifyClient::instance()->api()->playlistTracks(playlist.id, [self, name = playlist.name](const ApiResult &result, const QList<SpotifyTrack> &tracks) {
        if(!self)
            return;
        if(!result.ok)
            SpotifyClient::instance()->notify(result.message);
        else
            self->fillPlayList(name, tracks);
    });
}

void SpotifyGeneral::loadLikedSongs()
{
    if(!requireLogin())
        return;
    QPointer<SpotifyGeneral> self(this);
    SpotifyClient::instance()->api()->likedTracks([self](const ApiResult &result, const QList<SpotifyTrack> &tracks) {
        if(!self)
            return;
        if(!result.ok)
            SpotifyClient::instance()->notify(result.message);
        else
            self->fillPlayList(tr("Spotify: Liked Songs"), tracks);
    });
}

void SpotifyGeneral::fillPlayList(const QString &name, const QList<SpotifyTrack> &tracks)
{
    PlayListManager *manager = PlayListManager::instance();
    PlayListModel *model = nullptr;
    for(PlayListModel *candidate : manager->playLists())
    {
        if(candidate->name() == name)
        {
            model = candidate;
            break;
        }
    }
    if(!model)
        model = manager->createPlayList(name);
    model->clear();
    model->addTracks(createPlayListTracks(tracks));
    manager->selectPlayList(model);
}
