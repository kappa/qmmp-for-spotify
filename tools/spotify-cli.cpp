#include <QGuiApplication>
#include <QStandardPaths>
#include <QTextStream>
#include <QTime>
#include <QTimer>
#include "spotifyapi.h"
#include "spotifyauth.h"
#include "spotifypoller.h"

using namespace Qt::StringLiterals;

namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

void done(int code)
{
    out().flush();
    QCoreApplication::exit(code);
}

void finish(const ApiResult &result)
{
    if(!result.ok)
        out() << "error: " << result.message << " (HTTP " << result.status << ")\n";
    done(result.ok ? 0 : 1);
}

void printTracks(const ApiResult &result, const QList<SpotifyTrack> &tracks)
{
    for(const SpotifyTrack &t : tracks)
        out() << t.id << "  " << t.artists.join(u", "_s) << " - " << t.title << " [" << t.album << "] "
              << t.durationMs / 1000 << "s\n";
    out() << tracks.size() << " tracks\n";
    finish(result);
}

}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(u"qmmp-spotify-cli"_s);
    const QStringList args = app.arguments().mid(1);
    const QString clientId = qEnvironmentVariable("SPOTIFY_CLIENT_ID");
    if(args.isEmpty() || clientId.isEmpty())
    {
        qCritical("usage: SPOTIFY_CLIENT_ID=<id> spotify-cli login | logout | token | me | me-id | devices | state | playlists"
                  " | items <playlist id> | liked | album <album id> | track <track id>"
                  " | play <device id> <spotify uri> | pause <device id> | watch <device id>");
        return 2;
    }

    SpotifyAuth auth(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                     + u"/qmmp-spotify-cli/token.json"_s);
    auth.setClientId(clientId);
    auth.setPort(8898);
    SpotifyApi api(&auth);

    const QString command = args.at(0);
    const QString arg1 = args.value(1);
    const QString arg2 = args.value(2);
    QTimer::singleShot(0, &app, [&] {
        if(command == "login"_L1)
        {
            QObject::connect(&auth, &SpotifyAuth::loggedIn, &app, [] { out() << "logged in\n"; done(0); });
            QObject::connect(&auth, &SpotifyAuth::loginFailed, &app, [](const QString &message) { out() << message << '\n'; done(1); });
            auth.login();
        }
        else if(command == "logout"_L1)
        {
            auth.logout();
            done(0);
        }
        else if(command == "token"_L1)
            auth.withToken([](bool ok) { out() << (ok ? "token ok\n" : "no valid token\n"); done(ok ? 0 : 1); });
        else if(command == "me"_L1)
            api.displayName([](const ApiResult &result, const QString &name) { out() << name << '\n'; finish(result); });
        else if(command == "me-id"_L1)
            api.userId([](const ApiResult &result, const QString &id) { out() << id << '\n'; finish(result); });
        else if(command == "devices"_L1)
            api.devices([](const ApiResult &result, const QList<SpotifyDevice> &devices) {
                for(const SpotifyDevice &d : devices)
                    out() << d.id << "  " << d.name << " (" << d.type << ")" << (d.isActive ? " active" : "") << '\n';
                finish(result);
            });
        else if(command == "state"_L1)
            api.playerState([](const ApiResult &result, const PlayerState &s) {
                if(!s.hasPlayback)
                    out() << "nothing playing\n";
                else
                    out() << s.deviceName << " (" << s.deviceId << ")" << (s.isPlaying ? " playing " : " paused ")
                          << s.track.id << " " << s.track.title << " at " << s.progressMs << " ms\n";
                finish(result);
            });
        else if(command == "playlists"_L1)
            api.myPlaylists([](const ApiResult &result, const QList<SpotifyPlaylist> &playlists) {
                for(const SpotifyPlaylist &p : playlists)
                    out() << p.id << "  " << p.name << " (" << p.trackCount << ") owner=" << p.ownerId
                          << (p.collaborative ? " collaborative" : "") << '\n';
                finish(result);
            });
        else if(command == "items"_L1)
            api.playlistTracks(arg1, printTracks);
        else if(command == "liked"_L1)
            api.likedTracks(printTracks);
        else if(command == "album"_L1)
            api.albumTracks(arg1, printTracks);
        else if(command == "track"_L1)
            api.track(arg1, [](const ApiResult &result, const SpotifyTrack &t) { printTracks(result, { t }); });
        else if(command == "play"_L1)
            api.play(arg1, { arg2 }, 0, finish);
        else if(command == "pause"_L1)
            api.pause(arg1, finish);
        else if(command == "watch"_L1)
        {
            auto *poller = new SpotifyPoller(&api, &app);
            poller->setSelectedDevice(arg1);
            QObject::connect(poller, &SpotifyPoller::stateChanged, &app, [](const PlayerState &s) {
                out() << QTime::currentTime().toString(u"HH:mm:ss"_s) << "  "
                      << (s.hasPlayback ? s.deviceName : u"-"_s) << (s.isPlaying ? " playing " : " paused ")
                      << s.track.title << " " << s.progressMs / 1000 << "s\n";
                out().flush();
            });
            poller->setEnabled(true);
        }
        else
        {
            out() << "unknown command\n";
            done(2);
        }
    });
    return app.exec();
}
