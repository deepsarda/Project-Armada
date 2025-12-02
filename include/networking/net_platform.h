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

/* Threading abstraction for cross-platform compatibility */
typedef HANDLE net_thread_t;
typedef CRITICAL_SECTION net_mutex_t;

#define net_thread_create(thread, func, arg)                                            \
    (*(thread) = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(func), (arg), 0, NULL), \
     *(thread) == NULL ? -1 : 0)
#define net_thread_join(thread) \
    (WaitForSingleObject((thread), INFINITE), CloseHandle((thread)), 0)
#define net_thread_detach(thread) \
    CloseHandle(thread)
#define net_mutex_init(mutex) \
    (InitializeCriticalSection(mutex), 0)
#define net_mutex_destroy(mutex) \
    DeleteCriticalSection(mutex)
#define net_mutex_lock(mutex) \
    EnterCriticalSection(mutex)
#define net_mutex_unlock(mutex) \
    LeaveCriticalSection(mutex)

#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <pthread.h>

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

/* Threading abstraction for POSIX systems */
typedef pthread_t net_thread_t;
typedef pthread_mutex_t net_mutex_t;

#define net_thread_create(thread, func, arg) \
    pthread_create((thread), NULL, (func), (arg))
#define net_thread_join(thread) \
    pthread_join((thread), NULL)
#define net_thread_detach(thread) \
    pthread_detach(thread)
#define net_mutex_init(mutex) \
    pthread_mutex_init((mutex), NULL)
#define net_mutex_destroy(mutex) \
    pthread_mutex_destroy(mutex)
#define net_mutex_lock(mutex) \
    pthread_mutex_lock(mutex)
#define net_mutex_unlock(mutex) \
    pthread_mutex_unlock(mutex)

#endif

#endif // NET_PLATFORM_H
