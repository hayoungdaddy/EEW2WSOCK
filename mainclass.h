#ifndef MAINCLASS_H
#define MAINCLASS_H

#include <QObject>

#include <QtWebSockets/QtWebSockets>
#include <QMutex>
#include <QDataStream>
#include <QTimer>

#include "KGEEWLIBS_global.h"
#include "kgeewlibs.h"

#define EEW2WSOCK_VERSION 0.1

class MainClass : public QObject
{
    Q_OBJECT
public:
    explicit MainClass(QString conFile = nullptr, QObject *parent = nullptr);

private:
    _CONFIGURE cfg;
    RecvEEWMessage *rvEEW_Thread;

    QMutex mutex;

    QWebSocketServer *m_pWebSocketServer;
    QList<QWebSocket *> m_clients;

    projPJ pj_eqc;
    projPJ pj_longlat;
    void initProj();

private slots:
    void rvEEWInfo(_EEWINFO);
    void doRepeatWork();

    void onNewConnection();
    void socketDisconnected();
};


class ProcessEEWThread : public QThread
{
    Q_OBJECT
public:
    ProcessEEWThread(QWebSocket *websocket = nullptr, QWidget *parent = nullptr);
    ~ProcessEEWThread();

public slots:
    void recvTextMessage(QString);

private:
    QWebSocket *pSocket;
    _BINARY_SMALL_EEWLIST_PACKET generateData();
    void sendBinaryMessage(_BINARY_SMALL_EEWLIST_PACKET);

signals:
    void sendBinDataToMC(_BINARY_SMALL_EEWLIST_PACKET, QWebSocket *);
};


#endif // MAINCLASS_H
