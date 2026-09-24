#ifndef SPOTIFYINPUTFACTORY_H
#define SPOTIFYINPUTFACTORY_H

#include <QObject>
#include <qmmp/inputsourcefactory.h>

class SpotifyInputFactory : public QObject, public InputSourceFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qmmp.qmmp.InputSourceFactoryInterface.1.0")
    Q_INTERFACES(InputSourceFactory)
public:
    InputSourceProperties properties() const override;
    InputSource *create(const QString &url, QObject *parent = nullptr) override;
    QDialog *createSettings(QWidget *parent) override;
    void showAbout(QWidget *parent) override;
    QString translation() const override;
};

#endif
