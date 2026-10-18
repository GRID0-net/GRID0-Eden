// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <httplib.h>
#include "core/hle/service/ssl/switchnet_ca.h"

namespace Service::SSL {

inline void ConfigureSwitchNetTrust(httplib::SSLClient& client, const std::string& ca_bundle) {
    client.enable_server_certificate_verification(true);
    client.enable_server_hostname_verification(true);
    // These APIs mark the CA as custom, avoiding an extra Windows system-store check.
    if (ca_bundle.empty()) {
        client.load_ca_cert_store(SwitchNetLocalCaPem.data(), SwitchNetLocalCaPem.size());
    } else {
        client.set_ca_cert_path(ca_bundle);
    }
}

} // namespace Service::SSL
