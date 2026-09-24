#ifndef SPOTIFYGENERAL_H
#define SPOTIFYGENERAL_H

#include <QMessageBox>
#include <QObject>
#include <QPointer>

class QMenu;

class SpotifyGeneral : public QObject
{
    Q_OBJECT
public:
    explicit SpotifyGeneral(QObject *parent = nullptr);
    ~SpotifyGeneral();

private:
    bool requireLogin();
    void chooseDevice();
    void showNotice(const QString &message);

    QMenu *m_menu;
    QPointer<QMessageBox> m_notice;
};

#endif
