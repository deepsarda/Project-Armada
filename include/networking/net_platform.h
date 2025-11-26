#ifndef NET_PLATFORM_H
#define NET_PLATFORM_H

#include <stdint.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <BaseTsd.h>
#ifndef _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#define _SSIZE_T_DEFINED
#endif
typedef SOCKET net_socket_t;
#define NET_INVALID_SOCKET INVALID_SOCKET
#define NET_SOCKET_ERROR SOCKET_ERROR
#define NET_CLOSE_SOCKET closesocket
#define NET_ERRNO() WSAGetLastError()
#define NET_EWOULDBLOCK WSAEWOULDBLOCK
#define NET_EAGAIN WSAEWOULDBLOCK
#define NET_EINTR WSAEINTR
#define NET_EBADF WSAEBADF
#define NET_MSG_DONTWAIT 0x1000
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
typedef int net_socket_t;
#define NET_INVALID_SOCKET (-1)
#define NET_SOCKET_ERROR (-1)
#define NET_CLOSE_SOCKET close
#define NET_ERRNO() errno
#define NET_EWOULDBLOCK EWOULDBLOCK
#define NET_EAGAIN EAGAIN
#define NET_EINTR EINTR
#define NET_EBADF EBADF
#define NET_MSG_DONTWAIT MSG_DONTWAIT
#endif

#endif // NET_PLATFORM_H
