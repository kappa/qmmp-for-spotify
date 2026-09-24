#include <QMessageBox>
#include "spotifyinputfactory.h"
#include "spotifyinputsource.h"

using namespace Qt::StringLiterals;

InputSourceProperties SpotifyInputFactory::properties() const
{
    InputSourceProperties properties;
    properties.name = tr("Spotify Links");
    properties.shortName = u"spotify"_s;
    properties.protocols = QStringList { u"spotify"_s };
    properties.regExps = { QRegularExpression(u"^https?://open\\.spotify\\.com/(?:intl-[A-Za-z-]+/)?(?:track|album|playlist)/"_s) };
    properties.hasAbout = true;
    return properties;
}

InputSource *SpotifyInputFactory::create(const QString &url, QObject *parent)
{
    return new SpotifyInputSource(url, parent);
}

QDialog *SpotifyInputFactory::createSettings(QWidget *)
{
    return nullptr;
}

void SpotifyInputFactory::showAbout(QWidget *parent)
{
    QMessageBox::about(parent, tr("About Spotify Links"),
                       tr("Lets qmmp accept spotify:// and open.spotify.com links. Playback is handled by the Spotify engine."));
}

QString SpotifyInputFactory::translation() const
{
    return QString();
}
