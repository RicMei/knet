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

#include "channel_ref.h"
#include "channel.h"
#include "loop.h"
#include "misc.h"
#include "stream.h"
#include "loop_balancer.h"
#include "buffer.h"
#include "ringbuffer.h"
#include "address.h"

typedef struct _channel_ref_info_t {
    int                      balance;         /* 是否被负载均衡标志 */
    channel_t*               channel;         /* 内部管道 */
    dlist_node_t*            loop_node;       /* 管道链表节点 */
    stream_t*                stream;          /* 管道(读/写)数据流 */
    loop_t*                  loop;            /* 管道所关联的loop_t */
    address_t*               peer_address;    /* 对端地址 */
    address_t*               local_address;   /* 本地地址 */
    channel_event_e          event;           /* 管道投递事件 */
    volatile channel_state_e state;           /* 管道状态 */
    atomic_counter_t         ref_count;       /* 引用计数 */
    channel_ref_cb_t         cb;              /* 回调 */
    time_t                   last_recv_ts;    /* 最后一次读操作时间戳（秒） */
    time_t                   timeout;         /* 读空闲超时（秒） */
    time_t                   connect_timeout; /* connect()超时（秒） */
    int                      flag;            /* 选取器所使用自定义标志位 */
    void*                    data;            /* 选取器所使用自定义数据 */
} channel_ref_info_t;

struct _channel_ref_t {
    channel_ref_info_t* ref_info; /* 管道信息 */
};

channel_ref_t* channel_ref_create(loop_t* loop, channel_t* channel) {
    channel_ref_t* channel_ref = create(channel_ref_t);
    assert(channel_ref);
    channel_ref->ref_info = create(channel_ref_info_t);
    assert(channel_ref->ref_info);
    memset(channel_ref->ref_info, 0, sizeof(channel_ref_info_t));
    channel_ref->ref_info->stream = stream_create(channel_ref);
    assert(channel_ref->ref_info->stream);
    channel_ref->ref_info->channel      = channel;
    channel_ref->ref_info->ref_count    = 0;
    channel_ref->ref_info->loop         = loop;
    channel_ref->ref_info->last_recv_ts = time(0);
    return channel_ref;
}

int channel_ref_destroy(channel_ref_t* channel_ref) {
    assert(channel_ref);
    /* 检测引用计数 */
    if (!atomic_counter_zero(&channel_ref->ref_info->ref_count)) {
        return error_ref_nonzero;
    }
    assert(channel_ref->ref_info);
    assert(channel_ref->ref_info->loop);
    assert(channel_ref->ref_info->channel);
    assert(channel_ref->ref_info->stream);
    if (channel_ref->ref_info->peer_address) {
        address_destroy(channel_ref->ref_info->peer_address);
    }
    if (channel_ref->ref_info->local_address) {
        address_destroy(channel_ref->ref_info->local_address);
    }
    /* 通知选取器删除管道相关资源 */
    impl_remove_channel_ref(channel_ref->ref_info->loop, channel_ref);
    channel_destroy(channel_ref->ref_info->channel);
    stream_destroy(channel_ref->ref_info->stream);
    destroy(channel_ref->ref_info);
    destroy(channel_ref);
    return error_ok;
}

int channel_ref_connect(channel_ref_t* channel_ref, const char* ip, int port, int timeout) {
    loop_t* loop = 0;
    assert(channel_ref);
    loop = channel_ref_choose_loop(channel_ref);
    if (channel_ref_check_state(channel_ref, channel_state_connect)) {
        /* 已经处于连接状态 */
        return error_ok;
    }
    if (timeout) {
        /* 设置超时时间戳 */
        channel_ref->ref_info->connect_timeout = time(0) + timeout;
    }
    /* 发起连接 */
    return channel_ref_connect_in_loop(channel_ref, ip, port);
}

int channel_ref_accept(channel_ref_t* channel_ref, const char* ip, int port, int backlog) {
    int error = 0;
    assert(channel_ref);
    if (channel_ref_check_state(channel_ref, channel_state_accept)) {
        /* 已经处于监听状态 */
        return error_ok;
    }
    /* 监听 */
    error = channel_accept(channel_ref->ref_info->channel, ip, port, backlog);
    if (error == error_ok) {
        loop_add_channel_ref(channel_ref->ref_info->loop, channel_ref);
        channel_ref_set_state(channel_ref, channel_state_accept);
        channel_ref_set_event(channel_ref, channel_event_recv);
    }
    return error;
}

channel_ref_t* channel_ref_share(channel_ref_t* channel_ref) {
    channel_ref_t* channel_ref_shared = 0;
    assert(channel_ref);
    channel_ref_shared = create(channel_ref_t);
    assert(channel_ref_shared);
    /* 增加管道引用计数 */
    atomic_counter_inc(&channel_ref->ref_info->ref_count);
    /* 共享管道信息指针 */
    channel_ref_shared->ref_info = channel_ref->ref_info;
    return channel_ref_shared;
}

void channel_ref_leave(channel_ref_t* channel_ref) {
    assert(channel_ref);
    /* 递减引用计数 */
    atomic_counter_dec(&channel_ref->ref_info->ref_count);
    /* 管道信息最终由loop_t销毁 */
    destroy(channel_ref);
}

void channel_ref_update_close_in_loop(loop_t* loop, channel_ref_t* channel_ref) {
    assert(loop);
    assert(channel_ref);
    if (channel_ref_check_state(channel_ref, channel_state_close)) {
        return;
    }
    channel_ref_set_state(channel_ref, channel_state_close);
    channel_ref_clear_event(channel_ref, channel_event_recv | channel_event_send);
    channel_close(channel_ref->ref_info->channel);
    if (channel_ref->ref_info->cb) {
        channel_ref->ref_info->cb(channel_ref, channel_cb_event_close);
    }
    loop_close_channel_ref(channel_ref->ref_info->loop, channel_ref);
}

void channel_ref_close(channel_ref_t* channel_ref) {
    loop_t* loop = 0;
    assert(channel_ref);
    loop = channel_ref->ref_info->loop;
    if (loop_get_thread_id(loop) != thread_get_self_id()) {
        /* 通知管道所属线程 */
        loop_notify_close(loop, channel_ref);
    } else {
        /* 本线程内关闭 */
        channel_ref_update_close_in_loop(loop, channel_ref);
    }
}

void channel_ref_update_send_in_loop(loop_t* loop, channel_ref_t* channel_ref, buffer_t* send_buffer) {
    int error = 0;
    assert(loop);
    assert(channel_ref);
    assert(send_buffer);
    error = channel_send_buffer(channel_ref->ref_info->channel, send_buffer);
    switch (error) {
    case error_send_patial:
        channel_ref_set_event(channel_ref, channel_event_send);
        break;
    case error_send_fail:
        channel_ref_close(channel_ref);
        break;
    default:
        break;
    }
}

int channel_ref_write(channel_ref_t* channel_ref, const char* data, int size) {
    loop_t*   loop        = 0;
    buffer_t* send_buffer = 0;
    int       error       = error_ok;
    assert(channel_ref);
    assert(data);
    assert(size);
    loop = channel_ref->ref_info->loop;
    if (loop_get_thread_id(loop) != thread_get_self_id()) {
        /* 转到loop所在线程发送 */
        send_buffer = buffer_create(size);
        buffer_put(send_buffer, data, size);
        loop_notify_send(loop, channel_ref, send_buffer);
    } else {
        /* 当前线程发送 */
        error = channel_send(channel_ref->ref_info->channel, data, size);
        switch (error) {
        case error_send_patial:
            channel_ref_set_event(channel_ref, channel_event_send);
            break;
        case error_send_fail:
            channel_ref_close(channel_ref);
            break;
        default:
            break;
        }
    }
    return error;
}

socket_t channel_ref_get_socket_fd(channel_ref_t* channel_ref) {
    assert(channel_ref);
    return channel_get_socket_fd(channel_ref->ref_info->channel);
}

stream_t* channel_ref_get_stream(channel_ref_t* channel_ref) {
    assert(channel_ref);
    return channel_ref->ref_info->stream;
}

loop_t* channel_ref_get_loop(channel_ref_t* channel_ref) {
    assert(channel_ref);
    return channel_ref->ref_info->loop;
}

void channel_ref_set_loop_node(channel_ref_t* channel_ref, dlist_node_t* node) {
    assert(channel_ref); /* node可以为0 */
    channel_ref->ref_info->loop_node = node;
}

dlist_node_t* channel_ref_get_loop_node(channel_ref_t* channel_ref) {
    assert(channel_ref);
    return channel_ref->ref_info->loop_node;
}

void channel_ref_set_event(channel_ref_t* channel_ref, channel_event_e e) {
    assert(channel_ref);
    impl_event_add(channel_ref, e);
    channel_ref->ref_info->event |= e;
}

channel_event_e channel_ref_get_event(channel_ref_t* channel_ref) {
    assert(channel_ref);
    return channel_ref->ref_info->event;
}

void channel_ref_clear_event(channel_ref_t* channel_ref, channel_event_e e) {
    assert(channel_ref);
    impl_event_remove(channel_ref, e);
    channel_ref->ref_info->event &= ~e;
}

void channel_ref_set_state(channel_ref_t* channel_ref, channel_state_e state) {
    assert(channel_ref);
    channel_ref->ref_info->state = state;
}

int channel_ref_check_state(channel_ref_t* channel_ref, channel_state_e state) {
    assert(channel_ref);
    return (channel_ref->ref_info->state == state);
}

int channel_ref_check_event(channel_ref_t* channel_ref, channel_event_e event) {
    assert(channel_ref);
    return (channel_ref->ref_info->event & event);
}

channel_ref_t* channel_ref_accept_from_socket_fd(channel_ref_t* channel_ref, loop_t* loop, socket_t client_fd, int event) {
    channel_t*     acceptor_channel    = channel_ref->ref_info->channel;
    uint32_t       max_send_list_len   = channel_get_max_send_list_len(acceptor_channel);
    uint32_t       max_ringbuffer_size = ringbuffer_get_max_size(channel_get_ringbuffer(acceptor_channel));
    channel_t*     client_channel      = channel_create_exist_socket_fd(client_fd, max_send_list_len, max_ringbuffer_size);
    channel_ref_t* client_ref          = channel_ref_create(loop, client_channel);
    if (event) {
        /* 添加到当前线程loop */
        loop_add_channel_ref(channel_ref->ref_info->loop, client_ref);
        /* 创建的同时设置事件和状态 */
        channel_ref_set_state(client_ref, channel_state_active);
        channel_ref_set_event(client_ref, channel_event_recv);
    }
    return client_ref;
}

void channel_ref_update_accept(channel_ref_t* channel_ref) {
    channel_ref_t* client_ref = 0;
    loop_t*        loop       = 0;
    socket_t       client_fd  = 0;
    assert(channel_ref);
    /* 查看选取器是否有自定义实现 */
    client_fd = impl_channel_accept(channel_ref);
    if (!client_fd) {
        /* 默认实现 */
        client_fd = socket_accept(channel_get_socket_fd(channel_ref->ref_info->channel));
    }
    channel_ref_set_state(channel_ref, channel_state_accept);
    channel_ref_set_event(channel_ref, channel_event_recv);
    if (client_fd) {
        loop = channel_ref_choose_loop(channel_ref);
        if (loop) {
            client_ref = channel_ref_accept_from_socket_fd(channel_ref, loop, client_fd, 0);
            /* 设置回调 */
            channel_ref_set_cb(client_ref, channel_ref->ref_info->cb);
            /* 添加到其他loop */
            loop_notify_accept(loop, client_ref);
        } else {
            client_ref = channel_ref_accept_from_socket_fd(channel_ref, channel_ref->ref_info->loop, client_fd, 1);
            /* 调用回调 */
            if (channel_ref->ref_info->cb) {
                channel_ref->ref_info->cb(client_ref, channel_cb_event_accept);
            }
        }
    }
}

void channel_ref_update_accept_in_loop(loop_t* loop, channel_ref_t* channel_ref) {
    assert(loop);
    assert(channel_ref);
    /* 添加到当前线程loop */
    loop_add_channel_ref(loop, channel_ref);
    channel_ref_set_state(channel_ref, channel_state_active);
    channel_ref_set_event(channel_ref, channel_event_recv);
    /* 调用回调 */
    if (channel_ref->ref_info->cb) {
        channel_ref->ref_info->cb(channel_ref, channel_cb_event_accept);
    }
}

void channel_ref_update_connect(channel_ref_t* channel_ref) {  
    channel_ref_set_event(channel_ref, channel_event_recv);
    channel_ref_set_state(channel_ref, channel_state_active);
    /* 调用回调 */
    if (channel_ref->ref_info->cb) {
        channel_ref->ref_info->cb(channel_ref, channel_cb_event_connect);
    }
}

void channel_ref_update_recv(channel_ref_t* channel_ref) {
    int error = 0;
    assert(channel_ref);
    error = channel_update_recv(channel_ref->ref_info->channel);
    switch (error) {
        case error_recv_fail:
            channel_ref_close(channel_ref);
            break;
        case error_recv_buffer_full:
            channel_ref_close(channel_ref);
            break;
        default:
            break;
    }
    if (error == error_ok) {
        if (channel_ref->ref_info->cb) {
            channel_ref->ref_info->cb(channel_ref, channel_cb_event_recv);
        }
        channel_ref_set_event(channel_ref, channel_event_recv);
    }
}

void channel_ref_update_send(channel_ref_t* channel_ref) {
    int error = 0;
    assert(channel_ref);
    error = channel_update_send(channel_ref->ref_info->channel);
    switch (error) {
        case error_send_fail:
            channel_ref_close(channel_ref);
            break;
        case error_send_patial:
            channel_ref_set_event(channel_ref, channel_event_send);
            break;
        default:
            break;
    }
    if (error == error_ok) {
        if (channel_ref->ref_info->cb) {
            channel_ref->ref_info->cb(channel_ref, channel_cb_event_send);
        }
    }
}

void channel_ref_update(channel_ref_t* channel_ref, channel_event_e e, time_t ts) {
    assert(channel_ref);
    if (channel_ref_check_state(channel_ref, channel_state_close)) {
        return;
    }
    if ((e & channel_event_recv) && channel_ref_check_event(channel_ref, channel_event_recv)) {
        if (channel_ref_check_state(channel_ref, channel_state_accept)) {
            /* 新连接 */
            channel_ref_update_accept(channel_ref);
        } else {
            /* 最后一次读取到数据的时间戳（秒） */
            channel_ref->ref_info->last_recv_ts = ts;
            /* 读 */
            channel_ref_update_recv(channel_ref);
        }
    } 
    if ((e & channel_event_send) && channel_ref_check_event(channel_ref, channel_event_send)) {
        if (channel_ref_check_state(channel_ref, channel_state_connect)) {
            /* 连接完成 */
            channel_ref_update_connect(channel_ref);
        } else {
            /* 写 */
            channel_ref_update_send(channel_ref);
        }
    }
}

ringbuffer_t* channel_ref_get_ringbuffer(channel_ref_t* channel_ref) {
    assert(channel_ref);
    return channel_get_ringbuffer(channel_ref->ref_info->channel);
}

loop_t* channel_ref_choose_loop(channel_ref_t* channel_ref) {
    loop_t*          loop         = 0;
    loop_t*          current_loop = 0;
    loop_balancer_t* balancer     = 0;
    assert(channel_ref);
    current_loop = channel_ref->ref_info->loop;
    if (!loop_get_thread_id(current_loop)) {
        return 0;
    }
    balancer = loop_get_balancer(current_loop);
    if (!balancer) {
        return 0;
    }
    loop = loop_balancer_choose(balancer);
    if (loop == channel_ref->ref_info->loop) {
        return 0;
    }
    return loop;
}

void channel_ref_set_flag(channel_ref_t* channel_ref, int flag) {
    assert(channel_ref);
    channel_ref->ref_info->flag = flag;
}

int channel_ref_get_flag(channel_ref_t* channel_ref) {
    assert(channel_ref);
    return channel_ref->ref_info->flag;
}

void channel_ref_set_data(channel_ref_t* channel_ref, void* data) {
    assert(channel_ref);
    channel_ref->ref_info->data = data;
}

void* channel_ref_get_data(channel_ref_t* channel_ref) {
    assert(channel_ref);
    return channel_ref->ref_info->data;
}

void channel_ref_set_loop(channel_ref_t* channel_ref, loop_t* loop) {
    channel_ref->ref_info->loop = loop;
}

int channel_ref_check_balance(channel_ref_t* channel_ref) {
    return channel_ref->ref_info->balance;
}

void channel_ref_set_timeout(channel_ref_t* channel_ref, int timeout) {
    assert(channel_ref);
    assert(0 >= timeout);
    channel_ref->ref_info->timeout = (time_t)timeout;
}

int channel_ref_check_connect_timeout(channel_ref_t* channel_ref, time_t ts) {
    assert(channel_ref);
    if (channel_ref_check_state(channel_ref, channel_state_connect)) {
        if (channel_ref->ref_info->connect_timeout) {
            return (channel_ref->ref_info->connect_timeout < ts);
        }
    }
    return 0;
}

int channel_ref_check_timeout(channel_ref_t* channel_ref, time_t ts) {
    assert(channel_ref);
    if (!channel_ref->ref_info->timeout) {
        return 0;
    }
    return ((ts - channel_ref->ref_info->last_recv_ts) > channel_ref->ref_info->timeout);
}

void channel_ref_set_cb(channel_ref_t* channel_ref, channel_ref_cb_t cb) {
    assert(channel_ref);
    channel_ref->ref_info->cb = cb;
}

channel_ref_cb_t channel_ref_get_cb(channel_ref_t* channel_ref) {
    return channel_ref->ref_info->cb;
}

int channel_ref_connect_in_loop(channel_ref_t* channel_ref, const char* ip, int port) {
    int error = 0;
    assert(channel_ref);
    error = channel_connect(channel_ref->ref_info->channel, ip, port);
    if (error == error_ok) {
        loop_add_channel_ref(channel_ref->ref_info->loop, channel_ref);
        channel_ref_set_state(channel_ref, channel_state_connect);
        channel_ref_set_event(channel_ref, channel_event_send);
    }
    return error;
}

address_t* channel_ref_get_peer_address(channel_ref_t* channel_ref) {
    if (channel_ref->ref_info->peer_address) {
        return channel_ref->ref_info->peer_address;
    }
    channel_ref->ref_info->peer_address = address_create();
    socket_getpeername(channel_ref, channel_ref->ref_info->peer_address);
    return channel_ref->ref_info->peer_address;
}

address_t* channel_ref_get_local_address(channel_ref_t* channel_ref) {
    if (channel_ref->ref_info->local_address) {
        return channel_ref->ref_info->local_address;
    }
    channel_ref->ref_info->local_address = address_create();
    socket_getsockname(channel_ref, channel_ref->ref_info->local_address);
    return channel_ref->ref_info->local_address;
}
