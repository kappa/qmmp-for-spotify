#include "volumelogic.h"

VolumeLogic::VolumeLogic(VolumeSink *sink, Clock clock) : m_sink(sink), m_clock(std::move(clock))
{}

void VolumeLogic::setSelectedDevice(const QString &deviceId)
{
    if(deviceId == m_device)
        return;
    m_device = deviceId;
    m_deviceVolume = -1;
    m_sentAt = -1;
    m_supports = true;
    m_rejected = false;
    m_noticeShown = false;
}

void VolumeLogic::setActive(bool active)
{
    m_active = active;
    if(!active)
        m_pending = false;
}

void VolumeLogic::setQmmpState(int volume, bool muted)
{
    m_qmmpVolume = volume;
    m_muted = muted;
}

// SoundCore also re-emits unchanged values (e.g. when SoundCore::stop reloads the mixer);
// those and the echoes of setQmmpVolume are dropped here
void VolumeLogic::qmmpVolumeChanged(int volume)
{
    if(volume == m_qmmpVolume)
        return;
    m_qmmpVolume = volume;
    userChanged();
}

void VolumeLogic::qmmpMutedChanged(bool muted)
{
    if(muted == m_muted)
        return;
    m_muted = muted;
    userChanged();
}

void VolumeLogic::updateState(const PlayerState &state)
{
    if(!state.hasPlayback || state.deviceId != m_device || m_device.isEmpty())
        return;
    m_supports = state.supportsVolume;
    if(!unsupported())
        m_noticeShown = false;
    if(state.volumePercent < 0 || m_pending)
        return;
    if(m_sentAt >= 0)
    {
        const bool confirmed = state.volumePercent == m_deviceVolume;
        // polls issued before our change reached the device still show the old volume
        if(!confirmed && m_clock() - m_sentAt < kEchoGraceMs)
            return;
        m_sentAt = -1;
    }
    m_deviceVolume = state.volumePercent;
    if(!m_active || m_deviceVolume == effectiveVolume())
        return;
    // SoundCore::setVolume unmutes and emits synchronously; set our view first so both echoes are no-ops
    m_qmmpVolume = m_deviceVolume;
    m_muted = false;
    m_sink->setQmmpVolume(m_deviceVolume);
}

void VolumeLogic::setVolumeFailed(bool disallowed)
{
    m_sentAt = -1;
    m_deviceVolume = -1;
    if(!disallowed)
        return;
    m_rejected = true;
    m_pending = false;
    noticeUnsupported();
}

void VolumeLogic::tick()
{
    if(!m_pending || m_clock() - m_changedAt < kDebounceMs)
        return;
    m_pending = false;
    const int target = effectiveVolume();
    if(!m_active || m_device.isEmpty() || unsupported() || target == m_deviceVolume)
        return;
    m_deviceVolume = target;
    m_sentAt = m_clock();
    m_sink->apiSetVolume(target);
}

void VolumeLogic::userChanged()
{
    if(!m_active)
        return;
    if(unsupported())
    {
        noticeUnsupported();
        return;
    }
    m_pending = true;
    m_changedAt = m_clock();
}

int VolumeLogic::effectiveVolume() const
{
    return m_muted ? 0 : m_qmmpVolume;
}

bool VolumeLogic::unsupported() const
{
    return m_rejected || !m_supports;
}

void VolumeLogic::noticeUnsupported()
{
    if(m_noticeShown)
        return;
    m_noticeShown = true;
    m_sink->reportVolumeUnsupported();
}
