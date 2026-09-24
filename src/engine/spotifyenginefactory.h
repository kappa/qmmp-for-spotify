#ifndef SPOTIFYENGINEFACTORY_H
#define SPOTIFYENGINEFACTORY_H

#include <QObject>
#include <qmmp/enginefactory.h>

class SpotifyEngineFactory : public QObject, public EngineFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qmmp.qmmp.EngineFactoryInterface.1.0")
    Q_INTERFACES(EngineFactory)
public:
    bool supports(const QString &source) const override;
    EngineProperties properties() const override;
    AbstractEngine *create(QObject *parent = nullptr) override;
    QList<TrackInfo> createPlayList(const QString &path, TrackInfo::Parts parts, QStringList *ignoredPaths) override;
    MetaDataModel *createMetaDataModel(const QString &path, bool readOnly) override;
    QDialog *createSettings(QWidget *parent) override;
    void showAbout(QWidget *parent) override;
    QString translation() const override;
};

#endif
