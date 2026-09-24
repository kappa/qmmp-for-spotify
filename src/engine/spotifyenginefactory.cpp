#include <QMessageBox>
#include "spotifyengine.h"
#include "spotifyenginefactory.h"
#include "spotifylink.h"

using namespace Qt::StringLiterals;

bool SpotifyEngineFactory::supports(const QString &source) const
{
    return SpotifyLink::parse(source).kind == SpotifyLink::Track;
}

EngineProperties SpotifyEngineFactory::properties() const
{
    EngineProperties properties;
    properties.name = tr("Spotify Connect Engine");
    properties.shortName = u"spotify"_s;
    properties.protocols = QStringList { u"spotify"_s };
    properties.hasAbout = true;
    return properties;
}

AbstractEngine *SpotifyEngineFactory::create(QObject *parent)
{
    return new SpotifyEngine(parent);
}

QList<TrackInfo> SpotifyEngineFactory::createPlayList(const QString &, TrackInfo::Parts, QStringList *)
{
    return {};
}

MetaDataModel *SpotifyEngineFactory::createMetaDataModel(const QString &, bool)
{
    return nullptr;
}

QDialog *SpotifyEngineFactory::createSettings(QWidget *)
{
    return nullptr;
}

void SpotifyEngineFactory::showAbout(QWidget *parent)
{
    QMessageBox::about(parent, tr("About Spotify Connect Engine"),
                       tr("Plays Spotify tracks on the Spotify Connect device selected in Tools → Spotify → Device…"));
}

QString SpotifyEngineFactory::translation() const
{
    return QString();
}
