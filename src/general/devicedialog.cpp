#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
#include "devicedialog.h"
#include "spotifyapi.h"
#include "spotifyclient.h"

DeviceDialog::DeviceDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Spotify Device"));
    resize(420, 320);
    m_list = new QListWidget(this);
    m_status = new QLabel(this);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton *refreshButton = buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_list);
    layout->addWidget(m_status);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &DeviceDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &DeviceDialog::reject);
    connect(refreshButton, &QPushButton::clicked, this, &DeviceDialog::refresh);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &DeviceDialog::accept);
    refresh();
}

void DeviceDialog::accept()
{
    QListWidgetItem *item = m_list->currentItem();
    if(item)
        SpotifyClient::instance()->setDevice(item->data(Qt::UserRole).toString(), item->data(Qt::UserRole + 1).toString());
    QDialog::accept();
}

void DeviceDialog::refresh()
{
    m_status->setText(tr("Loading…"));
    QPointer<DeviceDialog> self(this);
    SpotifyClient::instance()->api()->devices([self](const ApiResult &result, const QList<SpotifyDevice> &devices) {
        if(!self)
            return;
        self->m_list->clear();
        const QString selected = SpotifyClient::instance()->deviceId();
        for(const SpotifyDevice &device : devices)
        {
            QString text = QStringLiteral("%1 (%2)").arg(device.name, device.type);
            if(device.isActive)
                text += tr(" — active");
            auto *item = new QListWidgetItem(text, self->m_list);
            item->setData(Qt::UserRole, device.id);
            item->setData(Qt::UserRole + 1, device.name);
            if(device.id == selected)
                self->m_list->setCurrentItem(item);
        }
        if(!result.ok)
            self->m_status->setText(result.message);
        else if(devices.isEmpty())
            self->m_status->setText(tr("No devices. Open Spotify on a device or start Soloist, then refresh."));
        else
            self->m_status->clear();
    });
}
