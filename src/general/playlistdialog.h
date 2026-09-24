#ifndef PLAYLISTDIALOG_H
#define PLAYLISTDIALOG_H

#include <QDialog>
#include "spotifytypes.h"

class QLabel;
class QListWidget;

class PlayListDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PlayListDialog(QWidget *parent = nullptr);
    SpotifyPlaylist selected() const;

private:
    QListWidget *m_list;
    QLabel *m_status;
    QList<SpotifyPlaylist> m_playlists;
};

#endif
