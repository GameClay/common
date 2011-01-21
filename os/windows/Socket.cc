/**
 * @file
 *
 * Define the abstracted socket interface for Windows
 */

/******************************************************************************
 * Copyright 2009-2011, Qualcomm Innovation Center, Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 ******************************************************************************/

#include <qcc/platform.h>

// Do not change the order of these includes; they are order dependent.
#include <Winsock2.h>
#include <Mswsock.h>
#include <ws2tcpip.h>

#include <qcc/IPAddress.h>
#include <qcc/ScatterGatherList.h>
#include <qcc/Socket.h>
#include <qcc/windows/utility.h>

#include <Status.h>

#define QCC_MODULE "NETWORK"

/* Scatter gather only support on Vista and later */
#define QCC_USE_SCATTER_GATHER 0


namespace qcc {

static void MakeSockAddr(const IPAddress& addr, uint16_t port,
                         sockaddr_in* addrBuf, socklen_t& addrSize)
{
    if (addr.IsIPv4()) {
        struct sockaddr_in* sa = reinterpret_cast<struct sockaddr_in*>(addrBuf);
        assert(addrSize >= sizeof(*sa));
        sa->sin_family = AF_INET;
        sa->sin_port = htons(port);
        sa->sin_addr.s_addr = addr.GetIPv4AddressNetOrder();
    } else {
        struct sockaddr_in6* sa = reinterpret_cast<struct sockaddr_in6*>(addrBuf);
        assert(addrSize >= sizeof(*sa));
        sa->sin6_family = AF_INET6;
        sa->sin6_port = htons(port);
        sa->sin6_flowinfo = 0;  // TODO: What should go here???
        addr.RenderIPv6Binary(sa->sin6_addr.s6_addr, sizeof(sa->sin6_addr.s6_addr));
        sa->sin6_scope_id = 0;  // TODO: What should go here???
    }
}

static QStatus GetSockAddr(const SOCKADDR_STORAGE* addrBuf, socklen_t addrSize,
                           IPAddress& addr, uint16_t& port)
{
    QStatus status = ER_OK;
    char hostname[NI_MAXHOST];
    char servInfo[NI_MAXSERV];

    DWORD dwRetval = getnameinfo((struct sockaddr*) addrBuf,
                                 sizeof (struct sockaddr),
                                 hostname,
                                 NI_MAXHOST, servInfo,
                                 NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV);

    if (dwRetval != 0) {
        int err = WSAGetLastError();
        status = ER_OS_ERROR;
        QCC_LogError(status, ("GetSockAddr: %d - %s", err, strerror(err)));
    } else {
        addr = IPAddress(hostname);
        port = atoi(servInfo);
    }

    return status;
}


QStatus Socket(AddressFamily addrFamily, SocketType type, SocketFd& sockfd)
{
    QStatus status = ER_OK;
    uint32_t ret;

    QCC_DbgTrace(("Socket(addrFamily = %d, type = %d, sockfd = <>)", addrFamily, type));

    if (addrFamily == QCC_AF_UNIX) {
        return ER_NOT_IMPLEMENTED;
    }
    ret = socket(static_cast<int>(addrFamily), static_cast<int>(type), 0);
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        status = ER_OS_ERROR;
        QCC_LogError(status, ("Opening socket: %d - %s", err, strerror(err)));
    } else {
        sockfd = static_cast<SocketFd>(ret);
    }
    return status;
}


QStatus Connect(SocketFd sockfd, const IPAddress& remoteAddr, uint16_t remotePort)
{
    QStatus status = ER_OK;
    int ret;
    sockaddr_in addr;
    socklen_t addrLen = sizeof(addr);

    QCC_DbgTrace(("Connect(sockfd = %d, remoteAddr = %s, remotePort = %hu)",
                  sockfd, remoteAddr.ToString().c_str(), remotePort));

    MakeSockAddr(remoteAddr, remotePort, &addr, addrLen);
    ret = connect(static_cast<SOCKET>(sockfd), reinterpret_cast<struct sockaddr*>(&addr), addrLen);
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (WSAEWOULDBLOCK == err) {
            status = ER_WOULDBLOCK;
        } else if (WSAEISCONN == err) {
            status = ER_OK;
        } else {
            status = ER_OS_ERROR;
            QCC_LogError(status, ("Connecting to %s %d: %d - %s", remoteAddr.ToString().c_str(), remotePort, err, strerror(err)));
        }
    } else {
        uint32_t mode = 1; // Non-blocking
        ret = ioctlsocket(sockfd, FIONBIO, &mode);
        if (ret == SOCKET_ERROR) {
            int err = WSAGetLastError();
            status = ER_OS_ERROR;
            QCC_LogError(status, ("Failed to set socket non-blocking %d - %s", err, strerror(err)));
            closesocket(sockfd);
        }
    }
    return status;
}


QStatus Connect(SocketFd sockfd, const char* pathName)
{
    return ER_NOT_IMPLEMENTED;
}



QStatus Bind(SocketFd sockfd, const IPAddress& localAddr, uint16_t localPort)
{
    QStatus status = ER_OK;
    int ret;
    sockaddr_in addr;
    socklen_t addrLen = sizeof(addr);

    QCC_DbgTrace(("Bind(sockfd = %d, localAddr = %s, localPort = %hu)",
                  sockfd, localAddr.ToString().c_str(), localPort));

    MakeSockAddr(localAddr, localPort, &addr, addrLen);
    ret = bind(static_cast<SOCKET>(sockfd), reinterpret_cast<struct sockaddr*>(&addr), addrLen);
    if (ret != 0) {
        int err = WSAGetLastError();
        status = (err == WSAEADDRNOTAVAIL ? ER_SOCKET_BIND_ERROR : ER_OS_ERROR);
        QCC_LogError(status, ("Binding to %s %d: %d - %s",
                              localAddr.ToString().c_str(), localPort, err, strerror(err)));
    }
    return status;
}


QStatus Bind(SocketFd sockfd, const char* pathName)
{
    return ER_NOT_IMPLEMENTED;
}


QStatus Listen(SocketFd sockfd, int backlog)
{
    QStatus status = ER_OK;
    int ret;

    QCC_DbgTrace(("Bind(sockfd = %d, backlog = %d)", sockfd, backlog));

    ret = listen(static_cast<SOCKET>(sockfd), backlog);
    if (ret != 0) {
        int err = WSAGetLastError();
        status = ER_OS_ERROR;
        QCC_LogError(status, ("Listening: %d - %s", err, strerror(err)));
    }
    return status;
}


QStatus Accept(SocketFd sockfd, IPAddress& remoteAddr, uint16_t& remotePort, SocketFd& newSockfd)
{
    QStatus status = ER_OK;
    uint32_t ret;
    struct sockaddr_storage addr;
    socklen_t addrLen = sizeof(addr);

    QCC_DbgTrace(("Accept(sockfd = %d, remoteAddr = <>, remotePort = <>)", sockfd));

    ret = accept(static_cast<SOCKET>(sockfd), reinterpret_cast<struct sockaddr*>(&addr), &addrLen);
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (WSAEWOULDBLOCK == err) {
            status = ER_WOULDBLOCK;
        } else {
            status = ER_OS_ERROR;
            QCC_LogError(status, ("Listening: %d - %s", err, strerror(err)));
        }
        newSockfd = -1;
    } else {
        if (addr.ss_family == AF_INET) {
            struct sockaddr_in* sa = reinterpret_cast<struct sockaddr_in*>(&addr);
            uint8_t* portBuf = reinterpret_cast<uint8_t*>(&sa->sin_port);
            remoteAddr = IPAddress(reinterpret_cast<uint8_t*>(&sa->sin_addr.s_addr),
                                   IPAddress::IPv4_SIZE);
            remotePort = (static_cast<uint16_t>(portBuf[0]) << 8) | static_cast<uint16_t>(portBuf[1]);
        } else if (addr.ss_family == AF_INET6) {
            struct sockaddr_in6* sa = reinterpret_cast<struct sockaddr_in6*>(&addr);
            uint8_t* portBuf = reinterpret_cast<uint8_t*>(&sa->sin6_port);
            remoteAddr = IPAddress(reinterpret_cast<uint8_t*>(&sa->sin6_addr.s6_addr),
                                   IPAddress::IPv6_SIZE);
            remotePort = (static_cast<uint16_t>(portBuf[0]) << 8) | static_cast<uint16_t>(portBuf[1]);
        } else {
            remotePort = 0;
        }
        newSockfd = static_cast<SocketFd>(ret);
        uint32_t mode = 1; // Non-blocking
        ret = ioctlsocket(newSockfd, FIONBIO, &mode);
        if (ret == SOCKET_ERROR) {
            int err = WSAGetLastError();
            status = ER_OS_ERROR;
            QCC_LogError(status, ("Failed to set socket non-blocking %d - %s", err, strerror(err)));
            closesocket(newSockfd);
            newSockfd = -1;
        } else {
            QCC_DbgHLPrintf(("Accept(sockfd = %d) newSockfd = %d", sockfd, newSockfd));
        }
    }
    return status;
}


QStatus Accept(SocketFd sockfd, SocketFd& newSockfd)
{
    IPAddress addr;
    uint16_t port;
    return Accept(sockfd, addr, port, newSockfd);
}


QStatus Shutdown(SocketFd sockfd)
{
    QStatus status = ER_OK;
    int ret;

    QCC_DbgHLPrintf(("Shutdown(sockfd = %d)", sockfd));

    ret = shutdown(static_cast<SOCKET>(sockfd), SD_BOTH);
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        status = ER_OS_ERROR;
        QCC_LogError(status, ("Shutdown socket: %d - %s", err, strerror(err)));
    }
    return status;
}


void Close(SocketFd sockfd)
{
    uint32_t ret;

    QCC_DbgTrace(("Close (sockfd = %d)", sockfd));
    ret = closesocket(static_cast<SOCKET>(sockfd));
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        QCC_LogError(ER_OS_ERROR, ("Close socket: %d - %s", err, strerror(err)));
    }
}


QStatus GetLocalAddress(SocketFd sockfd, IPAddress& addr, uint16_t& port)
{
    QStatus status = ER_OK;
    struct sockaddr_storage addrBuf;
    socklen_t addrLen = sizeof(addrBuf);
    int ret;

    QCC_DbgTrace(("GetLocalAddress(sockfd = %d, addr = <>, port = <>)", sockfd));

    memset(&addrBuf, 0, addrLen);

    ret = getsockname(static_cast<SOCKET>(sockfd), reinterpret_cast<struct sockaddr*>(&addrBuf), &addrLen);

    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        status = ER_OS_ERROR;
        QCC_LogError(status, ("Geting Local Address: %d - %s", err, strerror(err)));
    } else {
        QCC_DbgPrintf(("ret = %d  addrBuf.ss_family = %d  addrLen = %d", ret, addrBuf.ss_family, addrLen));
        if (addrBuf.ss_family == AF_INET) {
            struct sockaddr_in* sa = reinterpret_cast<struct sockaddr_in*>(&addrBuf);
            uint8_t* portBuf = reinterpret_cast<uint8_t*>(&sa->sin_port);
            addr = IPAddress(reinterpret_cast<uint8_t*>(&sa->sin_addr.s_addr), IPAddress::IPv4_SIZE);
            port = (static_cast<uint16_t>(portBuf[0]) << 8) | static_cast<uint16_t>(portBuf[1]);
        } else if (addrBuf.ss_family == AF_INET6) {
            struct sockaddr_in6* sa = reinterpret_cast<struct sockaddr_in6*>(&addrBuf);
            uint8_t* portBuf = reinterpret_cast<uint8_t*>(&sa->sin6_port);
            addr = IPAddress(reinterpret_cast<uint8_t*>(&sa->sin6_addr.s6_addr), IPAddress::IPv6_SIZE);
            port = (static_cast<uint16_t>(portBuf[0]) << 8) | static_cast<uint16_t>(portBuf[1]);
        } else {
            port = 0;
        }
        QCC_DbgPrintf(("Local Address: %s - %u", addr.ToString().c_str(), port));
    }

    return status;
}


QStatus Send(SocketFd sockfd, const void* buf, size_t len, size_t& sent)
{
    QStatus status = ER_OK;
    size_t ret;

    QCC_DbgTrace(("ERSend(sockfd = %d, *buf = <>, len = %lu, sent = <>)", sockfd, len));
    assert(buf != NULL);

    QCC_DbgLocalData(buf, len);

    ret = send(static_cast<SOCKET>(sockfd), static_cast<const char*>(buf), len, 0);
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (WSAEWOULDBLOCK == err) {
            sent = 0;
            status = ER_WOULDBLOCK;
        } else {
            status = ER_OS_ERROR;
            QCC_LogError(status, ("Send: %d - %s", err, strerror(err)));
        }
    } else {
        sent = static_cast<size_t>(ret);
        QCC_DbgPrintf(("Sent %u bytes", sent));
    }
    return status;
}


QStatus SendTo(SocketFd sockfd, IPAddress& remoteAddr, uint16_t remotePort,
               const void* buf, size_t len, size_t& sent)
{
    QStatus status = ER_OK;
    sockaddr_in addr;
    socklen_t addrLen = sizeof(addr);
    size_t ret;

    QCC_DbgTrace(("SendTo(sockfd = %d, remoteAddr = %s, remotePort = %u, *buf = <>, len = %lu, sent = <>)",
                  sockfd, remoteAddr.ToString().c_str(), remotePort, len));
    assert(buf != NULL);

    QCC_DbgLocalData(buf, len);

    MakeSockAddr(remoteAddr, remotePort, &addr, addrLen);
    ret = sendto(static_cast<SOCKET>(sockfd), static_cast<const char*>(buf), len, 0,
                 reinterpret_cast<struct sockaddr*>(&addr), addrLen);
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (WSAEWOULDBLOCK == err) {
            sent = 0;
            status = ER_WOULDBLOCK;
        } else {
            status = ER_OS_ERROR;
            QCC_LogError(status, ("Send: %d - %s", err, strerror(err)));
        }
    } else {
        sent = static_cast<size_t>(ret);
        QCC_DbgPrintf(("Sent %u bytes", sent));
    }
    return status;
}


#if QCC_USE_SCATTER_GATHER

static QStatus SendSGCommon(SocketFd sockfd, sockaddr_in* addr, socklen_t addrLen,
                            const ScatterGatherList& sg, size_t& sent)
{
    QStatus status = ER_OK;
    size_t ret;
    size_t index;
    WSAMSG msg;
    WSABUF* iov;
    WSABUF control;
    ScatterGatherList::const_iterator iter;

    QCC_DbgTrace(("SendSGCommon(sockfd = %d, *addr, addrLen, sg, sent = <>)", sockfd));

    iov = new WSABUF[sg.Size()];
    for (index = 0, iter = sg.Begin(); iter != sg.End(); ++index, ++iter) {
        iov[index].buf = iter->buf;
        iov[index].len = iter->len;
        QCC_DbgLocalData(iov[index].buf, iov[index].len);
    }

    msg.name = reinterpret_cast<LPSOCKADDR>(addr);
    msg.namelen = addrLen;
    msg.lpBuffers = iov;
    msg.dwBufferCount = sg.Size();
    msg.Control = control;
    control.len = 0;
    msg.dwFlags = 0;

    DWORD dwsent;
    ret = WSASendMsg(static_cast<SOCKET>(sockfd), &msg, 0, &dwsent, NULL, NULL);
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (WSAEWOULDBLOCK == err) {
            status = ER_WOULDBLOCK;
            sent = 0;
        } else {
            status = ER_OS_ERROR;
            QCC_LogError(status, ("Send: %d - %s", err, strerror(err)));
        }
    }
    QCC_DbgPrintf(("Sent %u bytes", dwsent));
    sent = dwsent;

    delete[] iov;
    return status;
}

QStatus SendSG(SocketFd sockfd, const ScatterGatherList& sg, size_t& sent)
{
    QCC_DbgTrace(("SendSG(sockfd = %d, sg, sent = <>)", sockfd));

    return SendSGCommon(sockfd, NULL, 0, sg, sent);
}

QStatus SendToSG(SocketFd sockfd, IPAddress& remoteAddr, uint16_t remotePort,
                 const ScatterGatherList& sg, size_t& sent)
{
    sockaddr_in addr;
    socklen_t addrLen = sizeof(addr);

    QCC_DbgTrace(("SendToSG(sockfd = %d, remoteAddr = %s, remotePort = %u, sg, sent = <>)",
                  sockfd, remoteAddr.ToString().c_str(), remotePort));

    MakeSockAddr(remoteAddr, remotePort, &addr, addrLen);
    return SendSGCommon(sockfd, &addr, addrLen, sg, sent);
}

#else

QStatus SendSG(SocketFd sockfd, const ScatterGatherList& sg, size_t& sent)
{
    QStatus status;
    uint8_t* tmpBuf = new uint8_t[sg.MaxDataSize()];
    sg.CopyToBuffer(tmpBuf, sg.MaxDataSize());
    status = Send(sockfd, tmpBuf, sg.DataSize(), sent);
    delete[] tmpBuf;
    return status;
}

QStatus SendToSG(SocketFd sockfd, IPAddress& remoteAddr, uint16_t remotePort,
                 const ScatterGatherList& sg, size_t& sent)
{
    QStatus status;
    uint8_t* tmpBuf = new uint8_t[sg.MaxDataSize()];
    sg.CopyToBuffer(tmpBuf, sg.MaxDataSize());
    status = SendTo(sockfd, remoteAddr, remotePort, tmpBuf, sg.DataSize(), sent);
    delete[] tmpBuf;
    return status;
}

#endif


QStatus Recv(SocketFd sockfd, void* buf, size_t len, size_t& received)
{
    QStatus status = ER_OK;
    size_t ret;

    QCC_DbgTrace(("Recv(sockfd = %d, buf = <>, len = %lu, received = <>)", sockfd, len));
    assert(buf != NULL);

    ret = recv(static_cast<SOCKET>(sockfd), static_cast<char*>(buf), len, 0);
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (WSAEWOULDBLOCK == err) {
            status = ER_WOULDBLOCK;
            QCC_DbgPrintf(("Recv WOULDBLOCK")); // TODO remove me
        } else {
            status = ER_OS_ERROR;
            QCC_LogError(status, ("Receive: %d - %s", err, strerror(err)));
        }
        received = 0;
    } else {
        received = static_cast<size_t>(ret);
        QCC_DbgPrintf(("Received %u bytes", received));
    }

    QCC_DbgRemoteData(buf, received);

    return status;
}


QStatus RecvFrom(SocketFd sockfd, IPAddress& remoteAddr, uint16_t& remotePort,
                 void* buf, size_t len, size_t& received)
{
    QStatus status = ER_OK;
    SOCKADDR_STORAGE fromAddr;
    socklen_t addrLen = sizeof(fromAddr);
    size_t ret;
    received = 0;

    QCC_DbgTrace(("RecvFrom(sockfd = %d, buf = <>, len = %lu, received = <>)", sockfd, len));
    assert(buf != NULL);

    ret = recvfrom(static_cast<int>(sockfd), static_cast<char*>(buf), len, 0,
                   reinterpret_cast<sockaddr*>(&fromAddr), &addrLen);
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (WSAEWOULDBLOCK == err) {
            status = ER_WOULDBLOCK;
        } else {
            status = ER_OS_ERROR;
            QCC_LogError(status, ("Receive: %d - %s", err, strerror(err)));
        }
        received = 0;
    } else {
        received = static_cast<size_t>(ret);
        status = GetSockAddr(&fromAddr, addrLen, remoteAddr, remotePort);
        QCC_DbgPrintf(("Received %u bytes, remoteAddr = %s, remotePort = %u",
                       received, remoteAddr.ToString().c_str(), remotePort));
    }

    QCC_DbgRemoteData(buf, received);

    return status;
}


#if QCC_USE_SCATTER_GATHER

static QStatus RecvSGCommon(SocketFd sockfd, SOCKADDR_STORAGE* addr, socklen_t* addrLen,
                            ScatterGatherList& sg, size_t& received)
{
    QStatus status = ER_OK;
    size_t ret;
    WSAMSG msg;
    size_t index;
    WSABUF* iov;
    WSABUF control;
    ScatterGatherList::const_iterator iter;
    QCC_DbgTrace(("RecvSGCommon(sockfd = &d, addr, addrLen, sg = <>, received = <>)",
                  sockfd));

    iov = new WSABUF[sg.Size()];
    for (index = 0, iter = sg.Begin(); iter != sg.End(); ++index, ++iter) {
        iov[index].buf = iter->buf;
        iov[index].len = iter->len;
    }

    msg.name = reinterpret_cast<LPSOCKADDR>(addr);
    msg.namelen = *addrLen;
    msg.lpBuffers = iov;
    msg.dwBufferCount = sg.Size();
    msg.Control = control;
    control.len = 0;
    msg.dwFlags = 0;

    ret = WSARecvMsg(static_cast<SOCKET>(sockfd), &msg, 0, received, NULL, NULL);
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (WSAEWOULDBLOCK == err) {
            received = 0;
            status = ER_WOULDBLOCK;
        } else {
            status = ER_OS_ERROR;
            QCC_LogError(status, ("Receive: %d - %s", err, strerror(err)));
        }
    } else {
        sg.SetDataSize(received);
        *addrLen = msg.namelen;
    }
    delete[] iov;

#if !defined(NDEBUG)
    QCC_DbgPrintf(("Received %u bytes", received));
    for (iter = sg.Begin(); iter != sg.End(); ++iter) {
        QCC_DbgRemoteData(iter->buf, iter->len);
    }
#endif

    return status;
}



QStatus RecvSG(SocketFd sockfd, ScatterGatherList& sg, size_t& received)
{
    socklen_t addrLen = 0;
    QCC_DbgTrace(("RecvSG(sockfd = %d, sg = <>, received = <>)", sockfd));

    return RecvSGCommon(sockfd, NULL, &addrLen, sg, received);
}


QStatus RecvFromSG(SocketFd sockfd, IPAddress& remoteAddr, uint16_t& remotePort,
                   ScatterGatherList& sg, size_t& received)
{
    QStatus status;
    SOCKADDR_STORAGE addr;
    socklen_t addrLen = sizeof(addr);

    status = RecvSGCommon(sockfd, &addr, &addrLen, sg, received);
    if (ER_OK == status) {
        GetSockAddr(&addr, addrLen, remoteAddr, remotePort);
        QCC_DbgTrace(("RecvFromSG(sockfd = %d, remoteAddr = %s, remotePort = %u, sg = <>, rcvd = %u)",
                      sockfd, remoteAddr.ToString().c_str(), remotePort, received));
    }
    return status;
}


#else
QStatus RecvSG(SocketFd sockfd, ScatterGatherList& sg, size_t& received)
{
    QStatus status = ER_OK;
    uint8_t* tmpBuf = new uint8_t[sg.MaxDataSize()];
    QCC_DbgTrace(("RecvSG(sockfd = %d, sg = <>, received = <>)", sockfd));

    status = Recv(sockfd, tmpBuf, sg.MaxDataSize(), received);
    if (ER_OK == status) {
        sg.CopyFromBuffer(tmpBuf, received);
    }
    QCC_DbgPrintf(("Received %u bytes", received));
    delete[] tmpBuf;
    return status;
}


QStatus RecvFromSG(SocketFd sockfd, IPAddress& remoteAddr, uint16_t& remotePort,
                   ScatterGatherList& sg, size_t& received)
{
    QStatus status = ER_OK;
    uint8_t* tmpBuf = new uint8_t[sg.MaxDataSize()];
    QCC_DbgTrace(("RecvToSG(sockfd = %d, remoteAddr = %s, remotePort = %u, sg = <>, sent = <>)",
                  sockfd, remoteAddr.ToString().c_str(), remotePort));

    status = RecvFrom(sockfd, remoteAddr, remotePort, tmpBuf, sg.MaxDataSize(), received);
    if (ER_OK == status) {
        sg.CopyFromBuffer(tmpBuf, received);
    }
    QCC_DbgPrintf(("Received %u bytes", received));
    delete[] tmpBuf;
    return status;
}

#endif

int InetPtoN(int af, const char* src, void* dst)
{
    int err;
    if (af == AF_INET6) {
        struct sockaddr_in6 sin6;
        int sin6Len = sizeof(sin6);
        memset(&sin6, 0, sin6Len);
        sin6.sin6_family = AF_INET6;
        err = WSAStringToAddressA((LPSTR)src, AF_INET6, NULL, (struct sockaddr*)&sin6, &sin6Len);
        if (!err) {
            memcpy(dst, &sin6.sin6_addr, sizeof(sin6.sin6_addr));
            return 1;
        }
    }
    if (af == AF_INET) {
        struct sockaddr_in sin;
        int sinLen = sizeof(sin);
        memset(&sin, 0, sinLen);
        sin.sin_family = AF_INET;
        err = WSAStringToAddressA((LPSTR)src, AF_INET, NULL, (struct sockaddr*)&sin, &sinLen);
        if (!err) {
            memcpy(dst, &sin.sin_addr, sizeof(sin.sin_addr));
            return 1;
        }
    }
    return -1;
}


const char* InetNtoP(int af, const void* src, char* dst, socklen_t size)
{
    DWORD sz = (DWORD)size;
    if (af == AF_INET6) {
        struct sockaddr_in6 sin6;
        memset(&sin6, 0, sizeof(sin6));
        sin6.sin6_family = AF_INET6;
        sin6.sin6_flowinfo = 0;
        memcpy(&sin6.sin6_addr, src, sizeof(sin6.sin6_addr));
        if (WSAAddressToStringA((struct sockaddr*)&sin6, sizeof(sin6), NULL, dst, &sz) == 0) {
            return dst;
        }
    }
    if (af == AF_INET) {
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        memcpy(&sin.sin_addr, src, sizeof(sin.sin_addr));
        if (WSAAddressToStringA((struct sockaddr*)&sin, sizeof(sin), NULL, dst, &sz) == 0) {
            return dst;
        }
    }
    return NULL;
}

}   /* namespace */







