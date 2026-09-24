#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QVBoxLayout>
#include "playlistdialog.h"
#include "spotifyapi.h"
#include "spotifyclient.h"

PlayListDialog::PlayListDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Load Spotify Playlist"));
    m_list = new QListWidget(this);
    m_status = new QLabel(tr("Loading…"), this);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_list);
    layout->addWidget(m_status);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &PlayListDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &PlayListDialog::reject);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &PlayListDialog::accept);

    QPointer<PlayListDialog> self(this);
    SpotifyClient::instance()->api()->myPlaylists([self](const ApiResult &result, const QList<SpotifyPlaylist> &playlists) {
        if(!self)
            return;
        self->m_playlists = playlists;
        for(const SpotifyPlaylist &playlist : playlists)
            self->m_list->addItem(tr("%1 (%2 tracks)").arg(playlist.name).arg(playlist.trackCount));
        self->m_status->setText(result.ok ? QString() : result.message);
    });
}

SpotifyPlaylist PlayListDialog::selected() const
{
    const int row = m_list->currentRow();
    return row >= 0 && row < m_playlists.size() ? m_playlists.at(row) : SpotifyPlaylist();
}
