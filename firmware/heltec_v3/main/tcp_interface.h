#pragma once

#include <memory>
#include <string>

#include "ureticulum/interface.h"

namespace HeltecV3 {

    /* Reticulum TCPClientInterface — connects to an upstream Reticulum
     * node (e.g. rnsd) over TCP and exchanges length-prefixed frames.
     * The transport node registers this alongside the LoRa interface so
     * packets flow between the LoRa mesh and the Internet. */
    class TcpInterface : public RNS::InterfaceImpl {
    public:
        TcpInterface(const char* host, uint16_t port);
        ~TcpInterface() override;

        static std::shared_ptr<TcpInterface> create(const char* host, uint16_t port);

        bool start() override;
        void stop()  override;
        void loop()  override;
        void send_outgoing(const RNS::Bytes& data) override;
        std::string toString() const override { return "TcpInterface[" + _host + "]"; }

    private:
        std::string _host;
        uint16_t    _port;
        int         _sock = -1;
        bool        _connected = false;
        uint64_t    _reconnect_at = 0;

        bool try_connect();
        void disconnect();
    };

}
