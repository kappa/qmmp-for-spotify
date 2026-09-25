#ifndef FOLLOWER_H
#define FOLLOWER_H

#include <QObject>
#include "spotifytypes.h"

// Makes qmmp follow what the selected device plays when the change came from elsewhere (e.g. the phone).
class Follower : public QObject
{
    Q_OBJECT
public:
    explicit Follower(QObject *parent = nullptr);

private:
    void onState(const PlayerState &state);

    PlayerState m_previous;
};

#endif
