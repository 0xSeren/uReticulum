#include "nomadnet_node.h"

#include <string.h>

#include "esp_log.h"

#include "ureticulum/destination.h"
#include "ureticulum/identity.h"
#include "ureticulum/link.h"
#include "ureticulum/transport.h"

static const char* TAG = "nn_node";

namespace {

    RNS::Destination g_dest{RNS::Type::NONE};
    std::string g_node_name;

    /* The index page served when a nomadnet user visits this node.
     * Uses micron markup (.mu) — the lightweight markup language
     * nomadnet's text UI renders. */
    const char* INDEX_PAGE =
        ">uReticulum Node\n"
        "\n"
        "This node is running on a Heltec WiFi LoRa 32 V3\n"
        "with the uReticulum firmware — a FreeRTOS-native\n"
        "Reticulum stack on an ESP32-S3 + SX1262.\n"
        "\n"
        "`F222`f\n"
        ">Features\n"
        "`f`F\n"
        "  * LoRa 915 MHz mesh transport\n"
        "  * WiFi TCP bridge to the Internet\n"
        "  * RNode-compatible UART + BLE bridge\n"
        "  * OTA firmware updates\n"
        "  * Ed25519/X25519 authenticated links\n"
        "\n"
        "-\n"
        "`cServed by uReticulum on ESP32-S3`a\n";

    RNS::Bytes serve_index(const RNS::Bytes&, const RNS::Bytes&,
                           const RNS::Bytes&, const RNS::Bytes&,
                           const RNS::Identity&, double) {
        return RNS::Bytes(reinterpret_cast<const uint8_t*>(INDEX_PAGE),
                          strlen(INDEX_PAGE));
    }

}

namespace HeltecV3::NomadnetNode {

void start(const RNS::Identity& identity, const char* node_name) {
    g_node_name = node_name;

    g_dest = RNS::Destination(identity,
                              RNS::Type::Destination::IN,
                              RNS::Type::Destination::SINGLE,
                              "nomadnetwork",
                              "node");
    RNS::Transport::register_destination(g_dest);

    /* Register page handlers. The path must match what nomadnet's
     * Browser.py requests: "/page/index.mu" for the landing page. */
    g_dest.register_request_handler("/page/index.mu", serve_index);

    /* Accept incoming Links on this destination. When a peer opens a
     * Link, the Transport layer creates the Link; request handlers
     * fire on inbound REQUEST packets over that Link. */
    RNS::Transport::set_link_request_handler(
        [](const RNS::Destination& d, const RNS::Bytes& req, const RNS::Packet& pkt)
            -> std::shared_ptr<RNS::Link> {
            auto link = RNS::Link::validate_request(d, req, pkt);
            if (link) {
                ESP_LOGI(TAG, "nomadnet peer connected via Link %s",
                         link->hash().toHex().substr(0, 8).c_str());
            }
            return link;
        });

    /* Announce the node so nomadnet users can discover it. The
     * app_data is the node name as UTF-8 — that's what nomadnet's
     * Directory shows. */
    RNS::Bytes name_bytes(reinterpret_cast<const uint8_t*>(node_name),
                          strlen(node_name));
    g_dest.announce(name_bytes);
    ESP_LOGI(TAG, "nomadnet node '%s' online, dest=%s",
             node_name, g_dest.hash().toHex().c_str());
}

}
