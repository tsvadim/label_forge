// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Minimal HTTP server on 127.0.0.1 for tests that download things. It runs in
// its own thread with the blocking socket API, so it also answers while the
// test's main thread is blocked in a synchronous download. The handler gets
// the request target ("/path?query") and returns the whole HTTP response; it
// runs in the server thread.

#include <QByteArray>
#include <QHostAddress>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

class TinyHttp {
public:
    using Handler = std::function<QByteArray(const QByteArray& target)>;

    explicit TinyHttp(Handler handler) : m_handler(std::move(handler))
    {
        m_thread = std::thread([this] { run(); });
        std::unique_lock<std::mutex> lock(m_mx);
        m_cv.wait(lock, [this] { return m_port >= 0; });
    }
    ~TinyHttp()
    {
        m_stop = true;
        m_thread.join();
    }
    TinyHttp(const TinyHttp&) = delete;
    TinyHttp& operator=(const TinyHttp&) = delete;

    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_port); }
    QString url(const char* path) const { return base() + QLatin1String(path); }
    int requests() const { return m_requests; }

    static QByteArray response(int code, const QByteArray& type, const QByteArray& body,
                               const QByteArray& extraHeaders = QByteArray())
    {
        const QByteArray reason = code == 200 ? "OK" : code == 302 ? "Found" : "Not Found";
        QByteArray r = "HTTP/1.1 " + QByteArray::number(code) + ' ' + reason + "\r\n";
        if (!type.isEmpty())
            r += "Content-Type: " + type + "\r\n";
        r += extraHeaders;
        r += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        return r;
    }

private:
    void run()
    {
        QTcpServer server;
        const bool ok = server.listen(QHostAddress::LocalHost, 0);
        {
            std::lock_guard<std::mutex> lock(m_mx);
            m_port = ok ? server.serverPort() : 0;
        }
        m_cv.notify_all();
        while (ok && !m_stop) {
            if (!server.waitForNewConnection(50))
                continue;
            QTcpSocket* sock = server.nextPendingConnection();
            QByteArray req;
            while (!req.contains("\r\n\r\n") && sock->waitForReadyRead(2000))
                req += sock->readAll();
            ++m_requests;
            sock->write(m_handler(req.split(' ').value(1)));
            sock->waitForBytesWritten(2000);
            sock->disconnectFromHost();
            if (sock->state() != QAbstractSocket::UnconnectedState)
                sock->waitForDisconnected(1000);
            delete sock;
        }
    }

    Handler m_handler;
    std::thread m_thread;
    std::mutex m_mx;
    std::condition_variable m_cv;
    int m_port = -1;
    std::atomic<bool> m_stop{false};
    std::atomic<int> m_requests{0};
};
