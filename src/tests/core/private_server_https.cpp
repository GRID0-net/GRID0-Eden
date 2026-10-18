// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
#include <catch2/catch_test_macros.hpp>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509v3.h>
#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/hle/service/ssl/ssl_backend.h"
#include "core/hle/service/sockets/private_server.h"
#include "core/internal_network/network.h"
#include "core/internal_network/sockets.h"
#include "core/hle/service/ssl/switchnet_https.h"

namespace {
struct Certificate {
    std::string pem;
    std::string key;
};

Certificate MakeCertificate() {
    const std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(EVP_RSA_gen(2048), EVP_PKEY_free);
    const std::unique_ptr<X509, decltype(&X509_free)> cert(X509_new(), X509_free);
    REQUIRE(key);
    REQUIRE(cert);
    REQUIRE(X509_set_version(cert.get(), 2) == 1);
    REQUIRE(ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1) == 1);
    REQUIRE(X509_gmtime_adj(X509_getm_notBefore(cert.get()), -60));
    REQUIRE(X509_gmtime_adj(X509_getm_notAfter(cert.get()), 86400));
    REQUIRE(X509_set_pubkey(cert.get(), key.get()) == 1);
    auto* subject = X509_get_subject_name(cert.get());
    REQUIRE(X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
                                      reinterpret_cast<const unsigned char*>("fixture.nintendo.com"),
                                      -1, -1, 0) == 1);
    REQUIRE(X509_set_issuer_name(cert.get(), subject) == 1);
    for (const auto& [nid, value] : {
             std::pair{NID_basic_constraints, "critical,CA:TRUE"},
             std::pair{NID_subject_alt_name, "DNS:fixture.nintendo.com"}}) {
        const std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> extension(
            X509V3_EXT_conf_nid(nullptr, nullptr, nid, value), X509_EXTENSION_free);
        REQUIRE(extension);
        REQUIRE(X509_add_ext(cert.get(), extension.get(), -1) == 1);
    }
    REQUIRE(X509_sign(cert.get(), key.get(), EVP_sha256()) > 0);

    const std::unique_ptr<BIO, decltype(&BIO_free)> cert_bio(BIO_new(BIO_s_mem()), BIO_free);
    const std::unique_ptr<BIO, decltype(&BIO_free)> key_bio(BIO_new(BIO_s_mem()), BIO_free);
    REQUIRE(PEM_write_bio_X509(cert_bio.get(), cert.get()) == 1);
    REQUIRE(PEM_write_bio_PrivateKey(key_bio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) == 1);
    char* data = nullptr;
    const auto cert_size = BIO_get_mem_data(cert_bio.get(), &data);
    std::string pem(data, static_cast<std::size_t>(cert_size));
    const auto key_size = BIO_get_mem_data(key_bio.get(), &data);
    return {std::move(pem), std::string(data, static_cast<std::size_t>(key_size))};
}

const Certificate& FixtureCertificate() {
    static const Certificate certificate = MakeCertificate();
    return certificate;
}

struct LocalServer {
    Certificate certificate = FixtureCertificate();
    httplib::SSLServer server{httplib::SSLServer::PemMemory{
        certificate.pem.data(), certificate.pem.size(), certificate.key.data(),
        certificate.key.size(), nullptr, 0, nullptr}};
    std::filesystem::path ca_path = std::filesystem::temp_directory_path() /
        ("grid0-test-ca-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".pem");
    std::thread listener;
    int port = 0;

    LocalServer() {
        REQUIRE(server.is_valid());
        std::ofstream ca(ca_path, std::ios::binary);
        ca << certificate.pem;
        ca.close();
        REQUIRE(ca.good());
        server.Get("/", [](const auto&, auto& response) { response.set_content("fixture", "text/plain"); });
        port = server.bind_to_any_port("127.0.0.1");
        REQUIRE(port > 0);
        listener = std::thread([this] { server.listen_after_bind(); });
        server.wait_until_ready();
    }
    ~LocalServer() {
        server.stop();
        if (listener.joinable()) {
            listener.join();
        }
        std::error_code ec;
        std::filesystem::remove(ca_path, ec);
    }
};
} // namespace

TEST_CASE("GRID0 HTTPS trusts a custom CA while checking the original hostname", "[grid0][https]") {
    LocalServer fixture;
    httplib::SSLClient client("fixture.nintendo.com", fixture.port);
    client.set_hostname_addr_map({{"fixture.nintendo.com", "127.0.0.1"}});
    client.set_connection_timeout(2);
    client.set_read_timeout(2);
    Service::SSL::ConfigureSwitchNetTrust(client, fixture.ca_path.string());
    const auto response = client.Get("/");
    REQUIRE(response);
    REQUIRE(response->status == 200);
    REQUIRE(response->body == "fixture");
}

TEST_CASE("GRID0 HTTPS refuses the wrong hostname and an unrelated CA", "[grid0][https]") {
    LocalServer fixture;
    SECTION("wrong hostname") {
        httplib::SSLClient client("wrong.nintendo.com", fixture.port);
        client.set_hostname_addr_map({{"wrong.nintendo.com", "127.0.0.1"}});
        client.set_connection_timeout(2);
        Service::SSL::ConfigureSwitchNetTrust(client, fixture.ca_path.string());
        const auto response = client.Get("/");
        REQUIRE_FALSE(response);
        REQUIRE(response.error() == httplib::Error::SSLServerHostnameVerification);
    }
    SECTION("unrelated CA") {
        httplib::SSLClient client("fixture.nintendo.com", fixture.port);
        client.set_hostname_addr_map({{"fixture.nintendo.com", "127.0.0.1"}});
        client.set_connection_timeout(2);
        Service::SSL::ConfigureSwitchNetTrust(client, "");
        const auto response = client.Get("/");
        REQUIRE_FALSE(response);
        REQUIRE(response.error() == httplib::Error::SSLServerVerification);
    }
}

TEST_CASE("Guest TLS negotiates its ALPN offer with the server", "[grid0][https][alpn]") {
    const auto original_bundle = Settings::values.private_server_ca_bundle.GetValue();
    SCOPE_EXIT { Settings::values.private_server_ca_bundle = original_bundle; };
    std::atomic<bool> negotiated = false;
    LocalServer fixture;
    Settings::values.private_server_ca_bundle = fixture.ca_path.string();
    SSL_CTX_set_alpn_select_cb(
        static_cast<SSL_CTX*>(fixture.server.tls_context()),
        [](SSL*, const unsigned char** output, unsigned char* output_size,
           const unsigned char* input, unsigned int input_size, void* userdata) {
            static constexpr unsigned char supported[]{2, 'h', '2'};
            if (SSL_select_next_proto(const_cast<unsigned char**>(output), output_size,
                                      supported, sizeof(supported), input, input_size) !=
                OPENSSL_NPN_NEGOTIATED) {
                return SSL_TLSEXT_ERR_ALERT_FATAL;
            }
            static_cast<std::atomic<bool>*>(userdata)->store(true);
            return SSL_TLSEXT_ERR_OK;
        },
        &negotiated);
    Network::NetworkInstance network;
    auto socket = std::make_shared<Network::Socket>();
    REQUIRE(socket->Initialize(Network::Domain::INET, Network::Type::STREAM,
                               Network::Protocol::TCP) == Network::Errno::SUCCESS);
    REQUIRE(socket->SetRcvTimeo(2000) == Network::Errno::SUCCESS);
    REQUIRE(socket->SetSndTimeo(2000) == Network::Errno::SUCCESS);
    REQUIRE(socket->Connect({Network::Domain::INET, {127, 0, 0, 1},
                             static_cast<u16>(fixture.port)}) == Network::Errno::SUCCESS);
    std::unique_ptr<Service::SSL::SSLConnectionBackend> backend;
    REQUIRE(Service::SSL::CreateSSLConnectionBackend(&backend) == ResultSuccess);
    backend->SetSocket(socket);
    REQUIRE(backend->SetHostName("fixture.nintendo.com") == ResultSuccess);
    const std::array<u8, 3> malformed{3, 'h', '2'};
    REQUIRE(backend->SetAlpnProtocols(malformed) != ResultSuccess);
    const std::array<u8, 3> protocols{2, 'h', '2'};
    REQUIRE(backend->SetAlpnProtocols(protocols) == ResultSuccess);
    REQUIRE(backend->DoHandshake() == ResultSuccess);
    REQUIRE(negotiated.load());
}

TEST_CASE("Redirected guest TLS recovers missing SNI and supports non-consuming peek", "[grid0][https][peek]") {
    const auto original_bundle = Settings::values.private_server_ca_bundle.GetValue();
    const auto original_server = Settings::values.private_server_address.GetValue();
    SCOPE_EXIT {
        Settings::values.private_server_ca_bundle = original_bundle;
        Settings::values.private_server_address = original_server;
    };
    LocalServer fixture;
    Settings::values.private_server_ca_bundle = fixture.ca_path.string();
    Settings::values.private_server_address = "127.0.0.1";
    Service::Sockets::RememberRedirectedHost("127.0.0.1", "fixture.nintendo.com");
    Network::NetworkInstance network;
    auto socket = std::make_shared<Network::Socket>();
    REQUIRE(socket->Initialize(Network::Domain::INET, Network::Type::STREAM,
                               Network::Protocol::TCP) == Network::Errno::SUCCESS);
    REQUIRE(socket->SetRcvTimeo(2000) == Network::Errno::SUCCESS);
    REQUIRE(socket->SetSndTimeo(2000) == Network::Errno::SUCCESS);
    REQUIRE(socket->Connect({Network::Domain::INET, {127, 0, 0, 1},
                             static_cast<u16>(fixture.port)}) == Network::Errno::SUCCESS);
    std::unique_ptr<Service::SSL::SSLConnectionBackend> backend;
    REQUIRE(Service::SSL::CreateSSLConnectionBackend(&backend) == ResultSuccess);
    backend->SetSocket(socket);
    REQUIRE(backend->DoHandshake() == ResultSuccess);
    const std::string request = "GET / HTTP/1.1\r\nHost: fixture.nintendo.com\r\nConnection: close\r\n\r\n";
    size_t written = 0;
    REQUIRE(backend->Write(&written, {reinterpret_cast<const u8*>(request.data()), request.size()}) == ResultSuccess);
    REQUIRE(written == request.size());
    std::array<u8, 12> peeked{};
    size_t peek_size = 0;
    REQUIRE(backend->Peek(&peek_size, peeked) == ResultSuccess);
    REQUIRE(peek_size == peeked.size());
    std::array<u8, 12> received{};
    size_t read_size = 0;
    REQUIRE(backend->Read(&read_size, received) == ResultSuccess);
    REQUIRE(read_size == peek_size);
    REQUIRE(received == peeked);
    REQUIRE(std::string(received.begin(), received.end()) == "HTTP/1.1 200");
}
