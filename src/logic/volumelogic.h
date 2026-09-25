#ifndef VOLUMELOGIC_H
#define VOLUMELOGIC_H

#include <functional>
#include <QString>
#include "spotifytypes.h"

class VolumeSink
{
public:
    virtual ~VolumeSink() = default;
    virtual void apiSetVolume(int percent) = 0;
    virtual void setQmmpVolume(int percent) = 0;
    virtual void reportVolumeUnsupported() = 0;
};

class VolumeLogic
{
public:
    using Clock = std::function<qint64()>;

    static constexpr qint64 kDebounceMs = 300;
    static constexpr qint64 kEchoGraceMs = 2500;

    VolumeLogic(VolumeSink *sink, Clock clock);

    void setSelectedDevice(const QString &deviceId);
    void setActive(bool active);
    void setQmmpState(int volume, bool muted);
    void qmmpVolumeChanged(int volume);
    void qmmpMutedChanged(bool muted);
    void updateState(const PlayerState &state);
    void setVolumeFailed(bool disallowed);
    void tick();

    bool hasPendingSend() const { return m_pending; }

private:
    void userChanged();
    int effectiveVolume() const;
    bool unsupported() const;
    void noticeUnsupported();

    VolumeSink *m_sink;
    Clock m_clock;
    QString m_device;
    bool m_active = false;
    int m_qmmpVolume = 0;
    bool m_muted = false;
    bool m_pending = false;
    qint64 m_changedAt = 0;
    int m_deviceVolume = -1;
    qint64 m_sentAt = -1;
    bool m_supports = true;
    bool m_rejected = false;
    bool m_noticeShown = false;
};

#endif
