#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QTimer>
#include <QWidget>
#include "spotifyclient.h"
#include "spotifypoller.h"
#include "windowwatcher.h"

WindowWatcher::WindowWatcher(QObject *parent) : QObject(parent)
{
    qApp->installEventFilter(this);
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [](Qt::ApplicationState state) {
        if(state == Qt::ApplicationActive)
            SpotifyClient::instance()->poller()->pollOnInteraction();
    });
    update();
}

bool WindowWatcher::eventFilter(QObject *watched, QEvent *event)
{
    switch(event->type())
    {
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::WindowStateChange:
        // the widget's visibility may not be updated yet: look once the event is done
        if(!m_updatePending && watched->isWidgetType() && static_cast<QWidget *>(watched)->isWindow())
        {
            m_updatePending = true;
            QTimer::singleShot(0, this, &WindowWatcher::update);
        }
        break;
    default:
        break;
    }
    return false;
}

void WindowWatcher::update()
{
    m_updatePending = false;
    bool visible = false;
    for(const QWidget *window : QApplication::topLevelWidgets())
    {
        if(isMainWindow(window) && isShown(window))
        {
            visible = true;
            break;
        }
    }
    if(visible == m_visible)
        return;
    m_visible = visible;
    SpotifyClient::instance()->poller()->setVisible(visible);
}

// qsui's main window, the skinned UI's main, playlist and equalizer windows, other plugins'
// windows; not dialogs (settings, our dialogs, notices), menus, tooltips or on-screen popups
bool WindowWatcher::isMainWindow(const QWidget *window)
{
    if(qobject_cast<const QDialog *>(window) || window->windowFlags().testFlag(Qt::X11BypassWindowManagerHint))
        return false;
    switch(window->windowType())
    {
    case Qt::Popup:
    case Qt::ToolTip:
    case Qt::SplashScreen:
        return false;
    default:
        return true;
    }
}

// the skinned UI's playlist and equalizer are separate windows owned by the main window:
// they count only while it is shown too
bool WindowWatcher::isShown(const QWidget *window)
{
    if(!window->isVisible() || window->isMinimized())
        return false;
    const QWidget *owner = window->parentWidget();
    return !owner || isShown(owner->window());
}
