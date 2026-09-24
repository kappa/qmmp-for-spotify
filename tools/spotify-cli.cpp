#include <QGuiApplication>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include "spotifyauth.h"

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

}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(u"qmmp-spotify-cli"_s);
    const QStringList args = app.arguments().mid(1);
    const QString clientId = qEnvironmentVariable("SPOTIFY_CLIENT_ID");
    if(args.isEmpty() || clientId.isEmpty())
    {
        qCritical("usage: SPOTIFY_CLIENT_ID=<id> spotify-cli login|logout|token");
        return 2;
    }

    SpotifyAuth auth(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                     + u"/qmmp-spotify-cli/token.json"_s);
    auth.setClientId(clientId);
    auth.setPort(8898);

    const QString command = args.at(0);
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
        else
        {
            out() << "unknown command\n";
            done(2);
        }
    });
    return app.exec();
}
