#include "mainclass.h"

QList<_EEWINFO> eewInfos;

MainClass::MainClass(QString configFileName, QObject *parent) : QObject(parent)
{
    activemq::library::ActiveMQCPP::initializeLibrary();

    cfg = readCFG(configFileName);
    qRegisterMetaType<_EEWINFO>("_EEWINFO");

    initProj();

    writeLog(cfg.logDir, cfg.processName, "======================================================");

    if(cfg.eew_amq_topic != "")
    {
        QString eewFailover = "failover:(tcp://" + cfg.eew_amq_ip + ":" + cfg.eew_amq_port + ")";

        rvEEW_Thread = new RecvEEWMessage;
        if(!rvEEW_Thread->isRunning())
        {
            rvEEW_Thread->setup(eewFailover, cfg.eew_amq_user, cfg.eew_amq_passwd, cfg.eew_amq_topic, true, false);
            connect(rvEEW_Thread, SIGNAL(_rvEEWInfo(_EEWINFO)), this, SLOT(rvEEWInfo(_EEWINFO)));
            rvEEW_Thread->start();
        }
    }

    writeLog(cfg.logDir, cfg.processName, "EEW2SOCK Started");

    m_pWebSocketServer = new QWebSocketServer(QStringLiteral("EEW2SOCK"), QWebSocketServer::NonSecureMode,  this);

    if(m_pWebSocketServer->listen(QHostAddress::Any, cfg.websocketPort))
    {
        writeLog(cfg.logDir, cfg.processName, "Listening on port : " + QString::number(cfg.websocketPort));

        connect(m_pWebSocketServer, &QWebSocketServer::newConnection,
                this, &MainClass::onNewConnection);
        connect(m_pWebSocketServer, &QWebSocketServer::closed,
                this, &QCoreApplication::quit);
    }

    QTimer *systemTimer = new QTimer;
    connect(systemTimer, SIGNAL(timeout()), this, SLOT(doRepeatWork()));
    systemTimer->start(1000);
}

void MainClass::doRepeatWork()
{
    QDateTime systemTimeUTC = QDateTime::currentDateTimeUtc();
    QDateTime dataTimeUTC = systemTimeUTC.addSecs(- SECNODS_FOR_ALIGN_QSCD); // GMT

    mutex.lock();
    if(!eewInfos.isEmpty())
    {
        for(int i=0;i<eewInfos.size();i++)
        {
            _EEWINFO myeew = eewInfos.at(i);
            if(dataTimeUTC.toTime_t() > myeew.origintime + KEEP_LARGE_DATA_DURATION)
            {
                eewInfos.removeAt(i);
            }
        }
    }
    mutex.unlock();
}

void MainClass::initProj()
{
    if (!(pj_longlat = pj_init_plus("+proj=longlat +ellps=WGS84")) )
    {
        qDebug() << "Can't initialize projection.";
        exit(1);
    }

    if (!(pj_eqc = pj_init_plus("+proj=eqc +ellps=WGS84")) )
    {
        qDebug() << "Can't initialize projection.";
        exit(1);
    }
}

void MainClass::onNewConnection()
{
    QWebSocket *pSocket = m_pWebSocketServer->nextPendingConnection();
    connect(pSocket, &QWebSocket::disconnected, this, &MainClass::socketDisconnected);
    m_clients << pSocket;

    ProcessEEWThread *prThread = new ProcessEEWThread(pSocket);
    if(!prThread->isRunning())
    {
        prThread->start();
        connect(pSocket, &QWebSocket::disconnected, prThread, &ProcessEEWThread::quit);
        connect(pSocket, &QWebSocket::textMessageReceived, prThread, &ProcessEEWThread::recvTextMessage);
        connect(prThread, &ProcessEEWThread::finished, prThread, &ProcessEEWThread::deleteLater);
    }
}

void MainClass::socketDisconnected()
{
    QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());

    if(pClient){
        m_clients.removeAll(pClient);
        pClient->deleteLater();
    }
}

void MainClass::rvEEWInfo(_EEWINFO newEewInfo)
{
    ll2xy4Small(pj_longlat, pj_eqc, newEewInfo.longitude, newEewInfo.latitude, &newEewInfo.smapX, &newEewInfo.smapY);
    ll2xy4Large(pj_longlat, pj_eqc, newEewInfo.longitude, newEewInfo.latitude, &newEewInfo.lmapX, &newEewInfo.lmapY);

    mutex.lock();
    if(eewInfos.isEmpty())
    {
        eewInfos.append(newEewInfo);
        writeLog(cfg.logDir, cfg.processName, "New EEW Received. : " +
                 QString::number(newEewInfo.eew_evid) + " " + QString::number(newEewInfo.origintime) + " " +
                 QString::number(newEewInfo.latitude, 'f', 4) + " " + QString::number(newEewInfo.longitude, 'f', 4) + " " +
                 QString::number(newEewInfo.magnitude, 'f', 1));
    }
    else
    {
        bool needInsert = true;
        for(int i=0;i<eewInfos.size();i++)
        {
            _EEWINFO myeew = eewInfos.at(i);
            if(newEewInfo.eew_evid == myeew.eew_evid)
            {
                needInsert = false;
                eewInfos.replace(i, newEewInfo);
                writeLog(cfg.logDir, cfg.processName, "Duplicated EEW Received. : " +
                         QString::number(newEewInfo.eew_evid) + " " + QString::number(newEewInfo.origintime) + " " +
                         QString::number(newEewInfo.latitude, 'f', 4) + " " + QString::number(newEewInfo.longitude, 'f', 4) + " " +
                         QString::number(newEewInfo.magnitude, 'f', 1));
            }
        }

        if(needInsert)
        {
            eewInfos.append(newEewInfo);

            writeLog(cfg.logDir, cfg.processName, "New EEW Received. : " +
                     QString::number(newEewInfo.eew_evid) + " " + QString::number(newEewInfo.origintime) + " " +
                     QString::number(newEewInfo.latitude, 'f', 4) + " " + QString::number(newEewInfo.longitude, 'f', 4) + " " +
                     QString::number(newEewInfo.magnitude, 'f', 1));
        }
    }
    mutex.unlock();
}



ProcessEEWThread::ProcessEEWThread(QWebSocket *socket, QWidget *parent)
{
    pSocket = socket;
}

ProcessEEWThread::~ProcessEEWThread()
{
}

void ProcessEEWThread::recvTextMessage(QString message)
{
    if(message.startsWith("Hello"))
        return;

    _BINARY_SMALL_EEWLIST_PACKET mypacket = generateData();
    sendBinaryMessage(mypacket);
}

_BINARY_SMALL_EEWLIST_PACKET ProcessEEWThread::generateData()
{
    _BINARY_SMALL_EEWLIST_PACKET mypacket;

    if(!eewInfos.isEmpty())
    {
        mypacket.numEEW = eewInfos.size();
        for(int i=0;i<eewInfos.size();i++)
        {
            mypacket.eewInfos[i] = eewInfos.at(i);
        }
    }
    else
        mypacket.numEEW = 0;

    return mypacket;
}

void ProcessEEWThread::sendBinaryMessage(_BINARY_SMALL_EEWLIST_PACKET mypacket)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.writeRawData((char*)&mypacket, sizeof(_BINARY_SMALL_EEWLIST_PACKET));

    pSocket->sendBinaryMessage(data);
}
