#pragma once

#include "ureticulum/identity.h"

namespace HeltecV3::NomadnetNode {

    /* Start a nomadnet-compatible node that serves pages over Reticulum
     * Links. Registers a "nomadnetwork.node" destination, announces
     * with the given node name, and serves /page/index.mu (plus any
     * hardcoded pages). Call after Reticulum::start(). */
    void start(const RNS::Identity& identity, const char* node_name);

}
