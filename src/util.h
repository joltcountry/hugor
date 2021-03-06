#ifndef UTIL_H
#define UTIL_H

#include <QApplication>

template <typename F>
static void
runInMainThread(F&& fun)
{
    QObject tmp;
    QObject::connect(&tmp, &QObject::destroyed, qApp, std::move(fun), Qt::BlockingQueuedConnection);
}

#endif
