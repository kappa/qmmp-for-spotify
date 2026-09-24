#ifndef SPOTIFYINPUTSOURCE_H
#define SPOTIFYINPUTSOURCE_H

#include <qmmp/inputsource.h>

// Carries a Spotify link to the engine; there is no audio data to read.
class SpotifyInputSource : public InputSource
{
    Q_OBJECT
public:
    explicit SpotifyInputSource(const QString &url, QObject *parent = nullptr) : InputSource(url, parent) {}

    QIODevice *ioDevice() const override { return nullptr; }
    bool initialize() override
    {
        m_ready = true;
        emit ready();
        return true;
    }
    bool isReady() const override { return m_ready; }

private:
    bool m_ready = false;
};

#endif
