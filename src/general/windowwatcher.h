#ifndef WINDOWWATCHER_H
#define WINDOWWATCHER_H

#include <QObject>

class QWidget;

// Tells the poller whether a qmmp main window is on screen, and polls on focus.
class WindowWatcher : public QObject
{
    Q_OBJECT
public:
    explicit WindowWatcher(QObject *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void update();
    static bool isMainWindow(const QWidget *window);
    static bool isShown(const QWidget *window);

    bool m_visible = true;
    bool m_updatePending = false;
};

#endif
