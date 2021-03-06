/*
 * Copyright (c) 2014-2015, dennis wang
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL dennis wang BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "misc.h"
#include "loop.h"
#include "channel_ref.h"
#include "address.h"

socket_t socket_create() {
    socket_t socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#if WIN32
    if (socket_fd == INVALID_SOCKET) {
        return 0;
    }
#else
    if (socket_fd < 0) {
        return 0;
    }
#endif /* (WIN32 || WIN64) */
    return socket_fd;
}

int socket_connect(socket_t socket_fd, const char* ip, int port) {
#if defined(WIN32) || defined(WIN64)
    DWORD last_error = 0;
#endif /* defined(WIN32) || defined(WIN64) */
    int error = 0;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons((unsigned short)port);
#if defined(WIN32) || defined(WIN64)
    sa.sin_addr.S_un.S_addr = INADDR_ANY;
    if (ip) {
        sa.sin_addr.S_un.S_addr = inet_addr(ip);
    }
#else
    sa.sin_addr.s_addr = INADDR_ANY;
    if (ip) {
        sa.sin_addr.s_addr = inet_addr(ip);
    }
#endif /* defined(WIN32) || defined(WIN64) */
    error = connect(socket_fd, (struct sockaddr*)&sa, sizeof(struct sockaddr));
#if defined(WIN32) || defined(WIN64)
    if (error < 0) {
        last_error = GetLastError();
        if ((WSAEWOULDBLOCK != last_error) && (WSAEISCONN != last_error)) {
            return error_connect_fail;
        }
    }
#else
    if (error < 0) {
        if ((errno != EINPROGRESS) && (errno != EINTR) && (errno != EISCONN)) {
            return error_connect_fail;
        }
    }
#endif /* defined(WIN32) || defined(WIN64) */
    return error_ok;
}

int socket_bind_and_listen(socket_t socket_fd, const char* ip, int port, int backlog) {
    int error = 0;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons((unsigned short)port);
#if defined(WIN32) || defined(WIN64)
    sa.sin_addr.S_un.S_addr = INADDR_ANY;
    if (ip) {
        sa.sin_addr.S_un.S_addr = inet_addr(ip);
    }
#else
    sa.sin_addr.s_addr = INADDR_ANY;
    if (ip) {
        sa.sin_addr.s_addr = inet_addr(ip);
    }
#endif /* defined(WIN32) || defined(WIN64) */
    socket_set_reuse_addr_on(socket_fd);
    socket_set_linger_off(socket_fd);
    error = bind(socket_fd, (struct sockaddr*)&sa, sizeof(struct sockaddr));
    if (error < 0) {
        return error_bind_fail;
    }
    /* 监听 */
    error = listen(socket_fd, backlog);
    if (error < 0) {
        return error_listen_fail;
    }
    return error_ok;
}

socket_t socket_accept(socket_t socket_fd) {
    socket_t     client_fd = 0; /* 客户端套接字 */
    socket_len_t addr_len  = sizeof(struct sockaddr_in);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    /* 接受客户端 */
    client_fd = accept(socket_fd, (struct sockaddr*)&sa, &addr_len);
    if (client_fd < 0) {
        return 0;
    }
    return client_fd;
}

int socket_set_reuse_addr_on(socket_t socket_fd) {
    int reuse_addr = 1;
    return setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse_addr , sizeof(reuse_addr));
}

int socket_set_non_blocking_on(socket_t socket_fd) {
#if WIN32
    u_long nonblocking = 1;
    if (socket_fd == INVALID_SOCKET) {
        assert(0);
        return 1;
    }
    if (SOCKET_ERROR == ioctlsocket(socket_fd, FIONBIO, &nonblocking)) {
        assert(0);
        return 1;
    }
#else
    int flags = 0;
    if (socket_fd < 0) {
        assert(0);
        return 1;
    }
    flags = fcntl(socket_fd, F_GETFL, 0);
    fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
#endif /* (WIN32 || WIN64) */
    return 0;
}

int socket_close(socket_t socket_fd) {
#if WIN32
    return closesocket(socket_fd);
#else
    return close(socket_fd);
#endif /* (WIN32 || WIN64) */
}

int socket_set_nagle_off(socket_t socket_fd) {
    int nodelay = 1;
    return setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));
}

int socket_set_linger_off(socket_t socket_fd) {
    struct linger linger;
    memset(&linger, 0, sizeof(linger));
    return setsockopt(socket_fd, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));
}

int socket_set_keepalive_off(socket_t socket_fd) {
    int keepalive = 0;
    return setsockopt(socket_fd, SOL_SOCKET, SO_KEEPALIVE, (char*)&keepalive, sizeof(keepalive));
}

int socket_set_donot_route_on(socket_t socket_fd) {
    int donot_route = 1;
    return setsockopt(socket_fd, SOL_SOCKET, SO_DONTROUTE, (char*)&donot_route, sizeof(donot_route));
}

int socket_set_recv_buffer_size(socket_t socket_fd, int size) {
    return setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, (char*)&size, sizeof(size));
}

int socket_set_send_buffer_size(socket_t socket_fd, int size) {
    return setsockopt(socket_fd, SOL_SOCKET, SO_SNDBUF, (char*)&size, sizeof(size));
}

int socket_check_send_ready(socket_t socket_fd) {
    fd_set send_fds[1];
    struct timeval tv = {0, 0};
    int error = 0;
    FD_ZERO(send_fds);
    FD_SET(socket_fd, send_fds);
    error = select((int)(socket_fd + 1), 0, send_fds, 0, &tv);
    if (0 > error) {
        return 0;
    }
    return FD_ISSET(socket_fd, send_fds);
}

int socket_send(socket_t socket_fd, const char* data, uint32_t size) {
    int send_bytes = 0;
#if defined(WIN32) || defined(WIN64)
    DWORD error = 0;
    send_bytes = send(socket_fd, data, (int)size, 0);
#else
    send_bytes = send(socket_fd, data, (int)size, MSG_NOSIGNAL);
#endif /* defined(WIN32) */
    if (send_bytes < 0) {
    #if defined(WIN32) || defined(WIN64)
        error = GetLastError();
        if ((error == 0) || (error == WSAEINTR) || (error == WSAEINPROGRESS) || (error == WSAEWOULDBLOCK)) {
            return 0;
        } else {
            send_bytes = -1;
        }
    #else
        if ((errno == 0) || (errno == EAGAIN ) || (errno == EWOULDBLOCK) || (errno == EINTR)) {
            return 0;
        } else {
            send_bytes = -1;
        }
    #endif /* defined(WIN32) || defined(WIN64) */
    } else if (send_bytes == 0) {
        return -1;
    }
    return send_bytes;
}

int socket_recv(socket_t socket_fd, char* data, uint32_t size) {
    int recv_bytes = 0;
#if defined(WIN32) || defined(WIN64)
    DWORD error = 0;
    recv_bytes = recv(socket_fd, data, (int)size, 0);
#else
    recv_bytes = recv(socket_fd, data, (int)size, MSG_NOSIGNAL);
#endif /* defined(WIN32) */
    if (recv_bytes < 0) {
    #if defined(WIN32) || defined(WIN64)
        error = GetLastError();
        if ((error == 0) || (error == WSAEINTR) || (error == WSAEINPROGRESS) || (error == WSAEWOULDBLOCK)) {
            return 0;
        } else {
            recv_bytes = -1;
        }
    #else
        if ((errno == 0) || (errno == EAGAIN ) || (errno == EWOULDBLOCK) || (errno == EINTR)) {
            return 0;
        } else {
            recv_bytes = -1;
        }
    #endif /* defined(WIN32) || defined(WIN64) */
    } else if (recv_bytes == 0) {
        recv_bytes = -1;
    }
    return recv_bytes;
}

#if defined(WIN32) || defined(WIN64)
u_short _get_random_port(int begin, int gap) {
    srand((int)time(0));
    return (u_short)(begin + abs(rand() % gap));
}
#endif /* defined(WIN32) || defined(WIN64) */

int socket_pair(socket_t pair[2]) {
#if defined(WIN32) || defined(WIN64)
    int      error       = 1;
    long     flag        = 1;
    int      port_begin  = 20000;
    int      port_gap    = 30000;
    int      addr_len    = sizeof(struct sockaddr_in);
    u_short  port        = _get_random_port(port_begin, port_gap);
    socket_t accept_sock = INVALID_SOCKET;
    struct sockaddr_in accept_addr;
    struct sockaddr_in connect_addr;
    memset(pair, INVALID_SOCKET, sizeof(pair));
    memset(&accept_addr, 0, sizeof(accept_addr));
    memset(&connect_addr, 0, sizeof(connect_addr));
    accept_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (accept_sock == INVALID_SOCKET) {
        goto error_return;
    }    
    memset(&accept_addr, 0, sizeof(accept_addr));
    accept_addr.sin_port = htons(port);
    accept_addr.sin_family = AF_INET;
    accept_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    /* 绑定随机端口 */
    error = bind(accept_sock, (struct sockaddr*)&accept_addr,sizeof(accept_addr));
    while (error) {
        if (WSAEADDRINUSE != GetLastError()) {
            goto error_return;
        }
        /* 随机分配一个端口 */
        port = _get_random_port(port_begin, port_gap);
        memset(&accept_addr, 0, sizeof(accept_addr));
        accept_addr.sin_port = htons(port);
        accept_addr.sin_family = AF_INET;
        accept_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        /* 重新绑定 */
        error = bind(accept_sock, (struct sockaddr*)&accept_addr,sizeof(accept_addr));
    }
    /* 监听 */
    error = listen(accept_sock, 1);
    if (error) {
        goto error_return;
    }
    /* 获取地址 */
    error = getsockname(accept_sock, (struct sockaddr*)&connect_addr, &addr_len);
    if (error) {
        goto error_return;
    }
    /* 建立客户端套接字 */
    pair[0] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (pair[0] == INVALID_SOCKET) {
        goto error_return;
    }
    /* 设置非阻塞 */
    ioctlsocket(pair[0], FIONBIO, (u_long*)&flag);
    connect_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    /* 建立连接 */
    error = connect(pair[0], (struct sockaddr*)&connect_addr, sizeof(connect_addr));
    if(error < 0) {
        error = WSAGetLastError();
        if ((error != WSAEWOULDBLOCK) && (error != WSAEINPROGRESS)) {
            goto error_return;
        }
    }
    /* 接受连接 */
    pair[1] = accept(accept_sock, (struct sockaddr*)&accept_addr, &addr_len);
    if(pair[1] == INVALID_SOCKET) {
        goto error_return;
    }
    socket_close(accept_sock);
    return 0;

error_return:
    if (accept_sock != INVALID_SOCKET) {
        closesocket(accept_sock);
    }
    if (pair[0] != INVALID_SOCKET) {
        closesocket(pair[0]);
    }
    if (pair[1] != INVALID_SOCKET) {
        closesocket(pair[1]);
    }
    return 1;
#else
    int error = socketpair(AF_UNIX, SOCK_STREAM, 0, pair);
    if (error) {
        return 1;
    }
    return (socket_set_non_blocking_on(pair[0]) && socket_set_non_blocking_on(pair[1]));
#endif /* defined(WIN32) || defined(WIN64) */
}

int socket_getpeername(channel_ref_t* channel_ref, address_t* address) {
#if defined(WIN32) || defined(WIN64)
    char* ip;
#else
    char ip[32] = {0};
#endif /* defined(WIN32) || define(WIN64) */
    int port;
    struct sockaddr_in addr;
    socket_len_t len = sizeof(struct sockaddr);
    int retval = getpeername(channel_ref_get_socket_fd(channel_ref), (struct sockaddr*)&addr, &len);
    if (retval < 0) {
        return error_getpeername;
    }
#if defined(WIN32) || defined(WIN64)
    ip = inet_ntoa(addr.sin_addr);
#else
    inet_ntop(AF_INET, &addr.sin_addr.s_addr, ip, sizeof(ip));
#endif /* defined(WIN32) || define(WIN64) */
    port = ntohs(addr.sin_port);
    address_set(address, ip, port);
    return error_ok;
}

int socket_getsockname(channel_ref_t* channel_ref,address_t* address) {
#if defined(WIN32) || defined(WIN64)
    char* ip;
#else
    char ip[32] = {0};
#endif /* defined(WIN32) || define(WIN64) */
    int port;
    struct sockaddr_in addr;
    socket_len_t len = sizeof(struct sockaddr);
    int retval = getsockname(channel_ref_get_socket_fd(channel_ref), (struct sockaddr*)&addr, &len);
    if (retval < 0) {
        return error_getpeername;
    }
#if defined(WIN32) || defined(WIN64)
    ip = inet_ntoa(addr.sin_addr);
#else
    inet_ntop(AF_INET, &addr.sin_addr.s_addr, ip, sizeof(ip));
#endif /* defined(WIN32) || define(WIN64) */
    port = ntohs(addr.sin_port);
    address_set(address, ip, port);
    return error_ok;
}

atomic_counter_t atomic_counter_inc(atomic_counter_t* counter) {
#if WIN32
    return InterlockedIncrement(counter);
#else
    return __sync_add_and_fetch(counter, 1);
#endif /* (WIN32 || WIN64) */
}

atomic_counter_t atomic_counter_dec(atomic_counter_t* counter) {
#if WIN32
    return InterlockedDecrement(counter);
#else
    return __sync_sub_and_fetch(counter, 1);
#endif /* (WIN32 || WIN64) */
}

int atomic_counter_zero(atomic_counter_t* counter) {
    return (*counter == 0);
}

struct _lock_t {
    #if defined(WIN32) || defined(WIN64)
        CRITICAL_SECTION lock;
    #else
        pthread_mutex_t lock;
    #endif /* defined(WIN32) || defined(WIN64) */
};

void _lock_init(lock_t* lock) {
    #if defined(WIN32) || defined(WIN64)
        InitializeCriticalSection(&lock->lock);
    #else
        pthread_mutex_init(&lock->lock, 0);
    #endif /* defined(WIN32) || defined(WIN64) */ 
}

lock_t* lock_create() {
    lock_t* lock = create(lock_t);
    _lock_init(lock);
    return lock;
}

void lock_destroy(lock_t* lock) {
    #if defined(WIN32) || defined(WIN64)
        DeleteCriticalSection(&lock->lock);
    #else
        pthread_mutex_destroy(&lock->lock);
    #endif /* defined(WIN32) || defined(WIN64) */
    destroy(lock);
}

void lock_lock(lock_t* lock) {
    #if defined(WIN32) || defined(WIN64)
        EnterCriticalSection(&lock->lock);
    #else
        pthread_mutex_lock(&lock->lock);
    #endif /* defined(WIN32) || defined(WIN64) */ 
}

int lock_trylock(lock_t* lock) {
    #if defined(WIN32) || defined(WIN64)
        return TryEnterCriticalSection(&lock->lock);
    #else
        return !pthread_mutex_trylock(&lock->lock);
    #endif /* defined(WIN32) || defined(WIN64) */ 
}

void lock_unlock(lock_t* lock) {
    #if defined(WIN32) || defined(WIN64)
        LeaveCriticalSection(&lock->lock);
    #else
        pthread_mutex_unlock(&lock->lock);
    #endif /* defined(WIN32) || defined(WIN64) */ 
}

thread_runner_t* thread_runner_create(thread_func_t func, void* params) {
    thread_runner_t* runner = create(thread_runner_t);
    assert(runner);
    memset(runner, 0, sizeof(thread_runner_t));
    runner->func   = func;
    runner->params = params;
    return runner;
}

void thread_runner_destroy(thread_runner_t* runner) {
    assert(runner);
    if (runner->running) {
        return;
    }
    destroy(runner);
}

void _thread_func(void* params) {
    thread_runner_t* runner = 0;
    assert(params);
    runner = (thread_runner_t*)params;
    runner->func(runner->params);
}

void _thread_loop_func(void* params) {
    int error = 0;
    thread_runner_t* runner = (thread_runner_t*)params;
    loop_t* loop = (loop_t*)runner->params;
    while (thread_runner_check_start(runner)) {
        error = loop_run_once(loop);
        if (error != error_ok) {
            thread_runner_stop(runner);
            assert(0);
        }
    }
}

#if defined(WIN32) || defined(WIN64)
void thread_loop_func_win(void* params) {
    _thread_loop_func(params);
}
#else
void* thread_loop_func_pthread(void* params) {
    _thread_loop_func(params);
    return 0;
}
#endif /* defined(WIN32) || defined(WIN64) */

#if defined(WIN32) || defined(WIN64)
void thread_func_win(void* params) {
    _thread_func(params);
}
#else
void* thread_func_pthread(void* params) {
    _thread_func(params);
    return 0;
}
#endif /* defined(WIN32) || defined(WIN64) */

int thread_runner_start(thread_runner_t* runner, int stack_size) {
#if defined(WIN32) || defined(WIN64)
    uintptr_t retval = 0;
#else
    int retval = 0;
    pthread_attr_t attr;
#endif /* defined(WIN32) || defined(WIN64) */
    assert(runner);
    if (!runner->func) {
        return error_thread_start_fail;
    }
    runner->running = 1;
#if defined(WIN32) || defined(WIN64)
    retval = _beginthread(thread_func_win, stack_size, runner);
    if (retval < 0) {
        return error_thread_start_fail;
    }
    runner->thread_id = retval;
#else
    if (stack_size) {
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, stack_size);
        retval = pthread_create(&runner->thread_id, &attr, thread_func_pthread, runner);
    } else {
        retval = pthread_create(&runner->thread_id, 0, thread_func_pthread, runner);
    }
    if (retval) {
        return error_thread_start_fail;
    }
#endif /* defined(WIN32) || defined(WIN64) */
    return error_ok;
}

int thread_runner_start_loop(thread_runner_t* runner, loop_t* loop, int stack_size) {
#if defined(WIN32) || defined(WIN64)
    uintptr_t retval = 0;
#else
    int retval = 0;
    pthread_attr_t attr;
#endif /* defined(WIN32) || defined(WIN64) */
    assert(runner);
    assert(loop);
    runner->params = loop;
    runner->running = 1;
#if defined(WIN32) || defined(WIN64)
    retval = _beginthread(thread_loop_func_win, stack_size, runner);
    if (retval < 0) {
        return error_thread_start_fail;
    }
    runner->thread_id = retval;
#else
    if (stack_size) {
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, stack_size);
        retval = pthread_create(&runner->thread_id, &attr, thread_loop_func_pthread, runner);
    } else {
        retval = pthread_create(&runner->thread_id, 0, thread_loop_func_pthread, runner);
    }
    if (retval) {
        return error_thread_start_fail;
    }
#endif /* defined(WIN32) || defined(WIN64) */
    return error_ok;
}

void thread_runner_stop(thread_runner_t* runner) {
    assert(runner);
    runner->running = 0;
}

void thread_runner_join(thread_runner_t* runner) {
#if defined(WIN32) || defined(WIN64)
    DWORD error = 0;
#else
    void* retval = 0;
#endif /* defined(WIN32) || defined(WIN64) */
    assert(runner);
#if defined(WIN32) || defined(WIN64)
    error = WaitForSingleObject((HANDLE)runner->thread_id, INFINITE);
    if ((error != WAIT_OBJECT_0) && (error != WAIT_ABANDONED)) {
        assert(0);
    }
#else
    pthread_join(runner->thread_id, &retval);
#endif /* defined(WIN32) || defined(WIN64) */
}

int thread_runner_check_start(thread_runner_t* runner) {
    assert(runner);
    return runner->running;
}

void* thread_runner_get_params(thread_runner_t* runner) {
    assert(runner);
    return runner->params;
}

thread_id_t thread_get_self_id() {
#if WIN32
    return GetCurrentThreadId();
#else
    return pthread_self();
#endif /* (WIN32 || WIN64) */
}

void thread_sleep_ms(int ms) {
#if WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif /* (WIN32 || WIN64) */
}

uint32_t time_get_milliseconds() {
#if defined(WIN32) || defined(WIN64)
    return GetTickCount();
#else
    struct timeval tv;
    uint64_t ms;
    gettimeofday(&tv, 0);
    ms = tv.tv_sec * 1000;
    ms += tv.tv_usec / 1000;
    return ms;
#endif /* defined(WIN32) || defined(WIN64) */
}

uint64_t time_get_microseconds() {
#if defined(WIN32) || defined(WIN64)
    LARGE_INTEGER freq;
    LARGE_INTEGER fc;
    if (!QueryPerformanceFrequency(&freq)) {
        assert(0);
    }
    if (!QueryPerformanceCounter(&fc)) {
        assert(0);
    }
    return fc.QuadPart / (freq.QuadPart / 1000 / 1000 / 1000);
#else
    struct timeval tv;
    uint64_t ms;
    gettimeofday(&tv, 0);
    ms = tv.tv_sec * 1000 * 1000;
    ms += tv.tv_usec;
    return ms;
#endif /* defined(WIN32) || defined(WIN64) */
}
