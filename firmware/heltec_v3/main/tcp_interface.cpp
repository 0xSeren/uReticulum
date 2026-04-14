#include "tcp_interface.h"

#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "esp_log.h"
#include "esp_timer.h"

using namespace RNS;

static const char* TAG = "tcp_if";

/* Python Reticulum TCPClientInterface uses a 2-byte big-endian length
 * prefix for each frame, followed by the raw Reticulum packet bytes.
 * HDLC-like framing (FEND/FESC) is NOT used on TCP — just len+data. */

namespace HeltecV3 {

TcpInterface::TcpInterface(const char* host, uint16_t port)
    : InterfaceImpl("tcp"), _host(host), _port(port) {}

TcpInterface::~TcpInterface() { disconnect(); }

std::shared_ptr<TcpInterface> TcpInterface::create(const char* host, uint16_t port) {
    return std::shared_ptr<TcpInterface>(new TcpInterface(host, port));
}

bool TcpInterface::try_connect() {
    struct addrinfo hints = {}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", _port);

    int rc = getaddrinfo(_host.c_str(), port_str, &hints, &res);
    if (rc != 0 || !res) {
        ESP_LOGW(TAG, "DNS resolve failed for %s: %d", _host.c_str(), rc);
        return false;
    }

    _sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (_sock < 0) {
        freeaddrinfo(res);
        return false;
    }

    /* Non-blocking connect with 5s timeout. */
    fcntl(_sock, F_SETFL, fcntl(_sock, F_GETFL) | O_NONBLOCK);
    rc = connect(_sock, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    if (rc < 0 && errno != EINPROGRESS) {
        close(_sock); _sock = -1;
        return false;
    }

    /* Wait for connect completion. */
    fd_set wset;
    FD_ZERO(&wset);
    FD_SET(_sock, &wset);
    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    rc = select(_sock + 1, nullptr, &wset, nullptr, &tv);
    if (rc <= 0) {
        close(_sock); _sock = -1;
        return false;
    }

    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(_sock, SOL_SOCKET, SO_ERROR, &err, &len);
    if (err != 0) {
        close(_sock); _sock = -1;
        return false;
    }

    /* Switch back to blocking with a short read timeout. */
    fcntl(_sock, F_SETFL, fcntl(_sock, F_GETFL) & ~O_NONBLOCK);
    tv = { .tv_sec = 0, .tv_usec = 10000 }; /* 10ms */
    setsockopt(_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    _connected = true;
    ESP_LOGI(TAG, "connected to %s:%u", _host.c_str(), _port);
    return true;
}

void TcpInterface::disconnect() {
    if (_sock >= 0) {
        close(_sock);
        _sock = -1;
    }
    _connected = false;
}

bool TcpInterface::start() {
    if (try_connect()) {
        _online = true;
        return true;
    }
    /* Will retry in loop(). */
    ESP_LOGW(TAG, "initial connect to %s:%u failed, will retry", _host.c_str(), _port);
    _online = true;  /* mark online so Transport still registers us */
    return true;
}

void TcpInterface::stop() {
    disconnect();
    _online = false;
}

void TcpInterface::loop() {
    uint64_t now = (uint64_t)(esp_timer_get_time() / 1000);

    if (!_connected) {
        if (now < _reconnect_at) return;
        if (try_connect()) return;
        _reconnect_at = now + 5000;  /* retry in 5s */
        return;
    }

    /* Read available frames. Each frame: [len_hi][len_lo][payload...] */
    uint8_t hdr[2];
    int n = recv(_sock, hdr, 2, 0);
    if (n == 2) {
        uint16_t frame_len = ((uint16_t)hdr[0] << 8) | hdr[1];
        if (frame_len > 0 && frame_len <= 600) {
            uint8_t buf[600];
            uint16_t got = 0;
            while (got < frame_len) {
                int r = recv(_sock, buf + got, frame_len - got, 0);
                if (r <= 0) break;
                got += r;
            }
            if (got == frame_len) {
                this->handle_incoming(Bytes(buf, frame_len));
            }
        }
    } else if (n == 0) {
        /* Peer closed. */
        ESP_LOGI(TAG, "peer closed connection");
        disconnect();
    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        ESP_LOGW(TAG, "recv error: %d", errno);
        disconnect();
    }
}

void TcpInterface::send_outgoing(const Bytes& data) {
    if (!_connected || _sock < 0) return;
    _txb += data.size();

    /* Length-prefix + payload in one write. */
    uint8_t hdr[2] = {
        (uint8_t)((data.size() >> 8) & 0xFF),
        (uint8_t)( data.size()       & 0xFF),
    };
    if (send(_sock, hdr, 2, 0) != 2 ||
        send(_sock, data.data(), data.size(), 0) != (int)data.size()) {
        ESP_LOGW(TAG, "send failed");
        disconnect();
    }
}

}
