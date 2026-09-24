#include <QMessageBox>
#include "settingsdialog.h"
#include "spotifygeneral.h"
#include "spotifygeneralfactory.h"

using namespace Qt::StringLiterals;

GeneralProperties SpotifyGeneralFactory::properties() const
{
    GeneralProperties properties;
    properties.name = tr("Spotify Plugin");
    properties.shortName = u"spotify"_s;
    properties.hasAbout = true;
    properties.hasSettings = true;
    return properties;
}

QObject *SpotifyGeneralFactory::create(QObject *parent)
{
    return new SpotifyGeneral(parent);
}

QDialog *SpotifyGeneralFactory::createSettings(QWidget *parent)
{
    return new SettingsDialog(parent);
}

void SpotifyGeneralFactory::showAbout(QWidget *parent)
{
    QMessageBox::about(parent, tr("About Spotify Plugin"),
                       tr("Controls Spotify Connect devices from qmmp, loads Spotify playlists and follows what the device plays."));
}

QString SpotifyGeneralFactory::translation() const
{
    return QString();
}
