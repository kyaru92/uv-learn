#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cinttypes>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

#include "uv.h"
#include "ngx_queue.h"

constexpr int MAX_ID_LEN = 16;
constexpr int BUF_SIZE = 1024;
constexpr int BACK_LOG = 128;

struct user {
    ngx_queue_t queue;
    uv_tcp_t handle;
    char id[MAX_ID_LEN];
};

void server(const char *servaddr, int port);

void on_connection(uv_stream_t *server_handle, int status);

void on_close(uv_handle_t *handle);

void on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buffer);

void on_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf);

void on_write(uv_write_t *req, int status);

template<typename... Args>
void broadcast(char *format, Args... args);

void unicast(user *cur_user, char *msg);

void give_a_nickname(user *cur_user);

const char *userinfo(user *cur_user);

ngx_queue_t users;
uv_loop_t *loop = uv_default_loop();


int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s addr port\n", basename(argv[0]));
        exit(1);
    }
    int port = atoi(argv[2]);
    server(argv[1], port);
    return 0;
}

// Startup
void server(const char *servaddr, int port) {
    ngx_queue_init(&users);

    uv_tcp_t server_handle;
    uv_tcp_init(loop, &server_handle);

    sockaddr_in addr;
    uv_ip4_addr(servaddr, port, &addr);

    int ret;
    if ((ret = uv_tcp_bind(&server_handle, reinterpret_cast<const sockaddr *>(&addr), 0))) {
        printf("uv_tcp_bind err: %s\n", uv_strerror(ret));
        return;
    }
    if ((ret = uv_listen(reinterpret_cast<uv_stream_t *>(&server_handle), BACK_LOG, on_connection))) {
        printf("uv_listen err: %s\n", uv_strerror(ret));
        return;
    }

    printf("Listen on %s:%d\n", servaddr, port);
    uv_run(loop, UV_RUN_DEFAULT);

    return;
}

void on_connection(uv_stream_t *server_handle, int status) {
    if (status) {
        printf("err: %s\n", uv_strerror(status));
        return;
    }
    // a new connection treats as a new anonymous user
    auto cur_user = new user;
    ngx_queue_insert_tail(&users, &cur_user->queue);
    give_a_nickname(cur_user);
    uv_tcp_init(loop, &cur_user->handle);
    if (uv_accept(server_handle, reinterpret_cast<uv_stream_t *>(&cur_user->handle))) {
        printf("err: %s\n", uv_strerror(status));
        return;
    }

    broadcast("* %s joined from %s\n", cur_user->id, userinfo(cur_user));

    uv_read_start(reinterpret_cast<uv_stream_t *>(&cur_user->handle), on_alloc, on_read);
}

void on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buffer) {
    static char buf[BUF_SIZE];
    *buffer = uv_buf_init(buf, BUF_SIZE);
}

void on_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
    auto cur_user = ngx_queue_data(handle, user, handle);
    // disconnect
    if (nread == UV__EOF) {
        ngx_queue_remove(&cur_user->queue);
        uv_close(reinterpret_cast<uv_handle_t *>(&cur_user->handle), on_close);
        broadcast("* %s has left the chat room\n", cur_user->id);
        return;
    }
    if(nread < 0) {
        ngx_queue_remove(&cur_user->queue);
        uv_close(reinterpret_cast<uv_handle_t *>(&cur_user->handle), on_close);
        return;
    }

    broadcast("%s said: %.*s", cur_user->id, nread, buf->base);
}

void on_close(uv_handle_t *handle) {
    auto cur_user = ngx_queue_data(handle, user, handle);
    delete cur_user;
}

template<typename... Args>
void broadcast(char *format, Args... args) {
    char msg[BUF_SIZE];
    snprintf(msg, BUF_SIZE, format, args...);

    for (ngx_queue_t *q = users.next; q != &users; q = q->next) {
        auto cur_user = ngx_queue_data(q, user, queue);
        unicast(cur_user, msg);
    }
}

void on_write(uv_write_t *req, int status) {
    free(req);
}

void unicast(user *cur_user, char *msg) {
    auto len = strlen(msg);
    // msg is a stack variable from broadcast. uv_write just enqueues a write request, so we need to copy the msg here
    auto *req = reinterpret_cast<uv_write_t *>(malloc((sizeof(uv_write_t) + len)));
    void *msg_addr = req + 1;
    memcpy(msg_addr, msg, len);
    auto buf = uv_buf_init(reinterpret_cast<char *>(msg_addr), len);
    uv_write(req, reinterpret_cast<uv_stream_t *>(&cur_user->handle), &buf, 1, on_write);
}

const char *userinfo(user *cur_user) {
    sockaddr_in peer;
    int len = sizeof(sockaddr_in);
    int ret;
    if ((ret = uv_tcp_getpeername(&cur_user->handle, reinterpret_cast<sockaddr *>(&peer), &len))) {
        printf("err: %s\n", uv_strerror(ret));
        return "";
    }

    char addr[INET_ADDRSTRLEN];
    static char buf[INET_ADDRSTRLEN + 7];
    uv_inet_ntop(AF_INET, &peer.sin_addr, addr, INET_ADDRSTRLEN);
    snprintf(buf, INET_ADDRSTRLEN + 7, "%s:%d", addr, ntohs(peer.sin_port));

    return buf;
}

void give_a_nickname(user *cur_user) {
    static int prefix = 0;
    static uint32_t count = 0;
    snprintf(cur_user->id, MAX_ID_LEN, "%c%d", 'A' + prefix, count);
    prefix = random() % 26;
    ++count;
}