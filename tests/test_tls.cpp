/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file test_tls.cpp
 * @brief TLS between server and client over loopback (public C API only).
 *
 * Certificates: test PKI of tools/make_test_certs.sh (made by the build).
 * Rules: docs/05_Normfragen_und_Festlegungen.md, N12 (profile) and E9
 * (handshake under t0).
 */
#include "iec104/iec104.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string cert(const std::string& name) {
    return std::string(IEC104_TEST_CERT_DIR) + "/" + name;
}

template <std::size_t N> void put(char (&field)[N], const std::string& text) {
    ASSERT_LT(text.size(), N);
    std::memcpy(static_cast<char*>(field), text.c_str(), text.size() + 1U);
}

/** @brief TLS settings with the test CA and the certificate @p name (.pem/.key). */
iec104_tls_config tls_with(const std::string& name) {
    iec104_tls_config t{};
    iec104_tls_config_init(&t);
    put(t.ca_file, cert("ca.pem"));
    if (!name.empty()) {
        put(t.certificate_file, cert(name + ".pem"));
        put(t.private_key_file, cert(name + ".key"));
    }
    return t;
}

/** @brief What the callbacks of one handle saw. */
struct Seen {
    std::vector<std::pair<iec104_security_event, std::string>> security;
    iec104_link_state state{IEC104_LINK_CONNECTING};
    iec104_close_reason reason{IEC104_CLOSE_NONE};
    int objects{0};

    [[nodiscard]] bool has(iec104_security_event e, const std::string& part = "") const {
        return std::any_of(security.begin(), security.end(), [&](const auto& s) {
            return s.first == e && s.second.find(part) != std::string::npos;
        });
    }
    [[nodiscard]] int count(iec104_security_event e) const {
        return static_cast<int>(std::count_if(security.begin(), security.end(),
                                              [&](const auto& s) { return s.first == e; }));
    }
    [[nodiscard]] std::string text() const {
        std::string all;
        for (const auto& s : security) {
            all += std::string(iec104_security_event_string(s.first)) + ": " + s.second + "\n";
        }
        return all;
    }

    static void on_security(void* user, uint32_t /*connection*/, iec104_security_event e,
                            const char* detail) {
        static_cast<Seen*>(user)->security.emplace_back(e, detail);
    }
    static void on_state(void* user, uint32_t /*connection*/, iec104_link_state s,
                         iec104_close_reason r) {
        auto* seen = static_cast<Seen*>(user);
        seen->state = s;
        seen->reason = r;
    }
    static void on_object(void* user, uint32_t /*connection*/, const iec104_asdu_info* /*info*/,
                          const iec104_object* /*object*/) {
        ++static_cast<Seen*>(user)->objects;
    }
    iec104_callbacks callbacks() {
        iec104_callbacks c{};
        c.user = this;
        c.on_security = on_security;
        c.on_state = on_state;
        c.on_object = on_object;
        return c;
    }
};

class Tls : public testing::Test {
  protected:
    void SetUp() override {
        if (iec104_tls_available() == 0) {
            GTEST_SKIP() << "library built without TLS";
        }
    }
    void TearDown() override {
        iec104_client_destroy(client_);
        iec104_server_destroy(server_);
    }

    /** @brief Server with one measured value, TLS @p tls (NULL = plain), any free port. */
    void start_server(const iec104_tls_config* tls, uint32_t t0_ms = 0) {
        iec104_link_config link{};
        iec104_link_config_init(&link);
        if (t0_ms != 0U) {
            link.t0_ms = t0_ms;
        }
        const iec104_callbacks cb = srv_.callbacks();
        ASSERT_EQ(iec104_server_create(&link, &cb, &server_), IEC104_OK);
        if (tls != nullptr) {
            ASSERT_EQ(iec104_server_set_tls(server_, tls), IEC104_OK);
        }
        iec104_object value{};
        value.type = 13;
        value.ioa = 100;
        value.value = 1.5;
        ASSERT_EQ(iec104_server_add_point(server_, 1, &value, 1), IEC104_OK);
        ASSERT_EQ(iec104_server_listen(server_, "127.0.0.1", 0), IEC104_OK);
    }

    /** @brief Client connecting to the server, TLS @p tls (NULL = plain). */
    void connect_client(const iec104_tls_config* tls) {
        if (client_ == nullptr) {
            const iec104_callbacks cb = cli_.callbacks();
            ASSERT_EQ(iec104_client_create(nullptr, &cb, &client_), IEC104_OK);
        }
        if (tls != nullptr) {
            ASSERT_EQ(iec104_client_set_tls(client_, tls), IEC104_OK);
        }
        cli_.state = IEC104_LINK_CONNECTING;
        cli_.reason = IEC104_CLOSE_NONE;
        ASSERT_EQ(iec104_client_connect(client_, "127.0.0.1", iec104_server_port(server_)),
                  IEC104_OK);
    }

    /** @brief Polls both sides until @p done or @p ms have passed. */
    bool run_until(const std::function<bool()>& done, int ms = 5000) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (!done() && std::chrono::steady_clock::now() < end) {
            (void)iec104_server_poll(server_, 1);
            if (client_ != nullptr) {
                (void)iec104_client_poll(client_, 1);
            }
        }
        return done();
    }

    /** @brief STARTDT (if not started yet) and a general interrogation; true if the value arrived.
     */
    bool start_and_interrogate() {
        if (cli_.state != IEC104_LINK_STARTED) {
            if (!run_until([&] { return cli_.state == IEC104_LINK_STOPPED; })) {
                return false;
            }
            (void)iec104_client_start(client_);
            if (!run_until([&] { return cli_.state == IEC104_LINK_STARTED; })) {
                return false;
            }
        }
        const int before = cli_.objects;
        (void)iec104_client_interrogation(client_, 1, IEC104_COT_INTERROGATED_STATION);
        return run_until([&] { return cli_.objects >= before + 3; }); // ACTCON, value, ACTTERM
    }

    bool client_closed() {
        return run_until([&] { return cli_.state == IEC104_LINK_CLOSED; });
    }
    bool server_closed() {
        return run_until([&] { return srv_.state == IEC104_LINK_CLOSED; });
    }

    iec104_server* server_{nullptr};
    iec104_client* client_{nullptr};
    Seen srv_;
    Seen cli_;
};

} // namespace

// Mutual authentication with TLS 1.3 (default): both sides see the other's
// certificate, IEC 104 runs unchanged on top, close ends with close_notify.
TEST_F(Tls, MutualAuthenticationTls13) {
    const iec104_tls_config s = tls_with("server");
    const iec104_tls_config c = tls_with("client");
    start_server(&s);
    connect_client(&c);
    ASSERT_TRUE(start_and_interrogate()) << srv_.text() << cli_.text();

    EXPECT_TRUE(srv_.has(IEC104_SECURITY_TLS_ESTABLISHED, "TLSv1.3"));
    EXPECT_TRUE(srv_.has(IEC104_SECURITY_TLS_ESTABLISHED, "peer CN=client.test"));
    iec104_tls_info info{};
    ASSERT_EQ(iec104_client_get_tls_info(client_, &info), IEC104_OK);
    EXPECT_STREQ(info.version, "TLSv1.3");
    EXPECT_STREQ(info.peer_subject, "CN=localhost");
    EXPECT_EQ(std::strlen(info.peer_fingerprint), 95U); // 32 octets "AB:" ...
    EXPECT_EQ(info.resumed, 0);
    ASSERT_EQ(iec104_server_get_tls_info(server_, 1, &info), IEC104_OK);
    EXPECT_STREQ(info.peer_subject, "CN=client.test");
    EXPECT_EQ(iec104_server_get_tls_info(server_, 2, &info), IEC104_ERR_WRONG_STATE);

    iec104_client_close(client_);
    ASSERT_TRUE(server_closed());
    EXPECT_EQ(srv_.reason, IEC104_CLOSE_PEER); // orderly end, no TLS error
}

// TLS 1.2 with a chosen ECDHE/AEAD suite.
TEST_F(Tls, Tls12WithChosenSuite) {
    const iec104_tls_config s = tls_with("server");
    iec104_tls_config c = tls_with("client");
    c.max_version = IEC104_TLS_1_2;
    put(c.cipher_list, "ECDHE-ECDSA-AES256-GCM-SHA384");
    start_server(&s);
    connect_client(&c);
    ASSERT_TRUE(start_and_interrogate()) << srv_.text() << cli_.text();
    iec104_tls_info info{};
    ASSERT_EQ(iec104_client_get_tls_info(client_, &info), IEC104_OK);
    EXPECT_STREQ(info.version, "TLSv1.2");
    EXPECT_STREQ(info.cipher, "ECDHE-ECDSA-AES256-GCM-SHA384");
}

// The client trusts only its CA: a server certificate of another CA is rejected.
TEST_F(Tls, ClientRejectsServerOfForeignCa) {
    const iec104_tls_config s = tls_with("foreign");
    const iec104_tls_config c = tls_with("client");
    start_server(&s);
    connect_client(&c);
    ASSERT_TRUE(client_closed());
    EXPECT_EQ(cli_.reason, IEC104_CLOSE_TLS);
    EXPECT_TRUE(cli_.has(IEC104_SECURITY_CERTIFICATE_REJECTED, "local issuer")) << cli_.text();
    EXPECT_FALSE(cli_.has(IEC104_SECURITY_TLS_ESTABLISHED));
}

// Mutual authentication: the server closes if the client has no certificate,
// an expired one or one of a foreign CA.
TEST_F(Tls, ServerRejectsClientCertificates) {
    const iec104_tls_config s = tls_with("server");
    start_server(&s);
    const struct {
        const char* name;
        const char* reason;
    } cases[] = {
        {"", "certificate"}, // none
        {"expired", "expired"},
        {"foreign", "local issuer"},
    };
    for (const auto& k : cases) {
        SCOPED_TRACE(k.name);
        srv_ = Seen{};
        const iec104_tls_config c = tls_with(k.name);
        connect_client(&c);
        ASSERT_TRUE(server_closed());
        EXPECT_EQ(srv_.reason, IEC104_CLOSE_TLS);
        EXPECT_FALSE(srv_.has(IEC104_SECURITY_TLS_ESTABLISHED));
        EXPECT_NE(srv_.text().find(k.reason), std::string::npos) << srv_.text();
        ASSERT_TRUE(client_closed()); // the client learns it from the alert or the close
        EXPECT_NE(cli_.reason, IEC104_CLOSE_NONE);
    }
}

// CRL: a revoked client certificate is rejected, a valid one accepted.
TEST_F(Tls, RevocationList) {
    iec104_tls_config s = tls_with("server");
    put(s.crl_file, cert("crl.pem"));
    start_server(&s);
    const iec104_tls_config revoked = tls_with("revoked");
    connect_client(&revoked);
    ASSERT_TRUE(server_closed());
    EXPECT_TRUE(srv_.has(IEC104_SECURITY_CERTIFICATE_REJECTED, "revoked")) << srv_.text();
    ASSERT_TRUE(client_closed());

    srv_ = Seen{};
    const iec104_tls_config valid = tls_with("client");
    connect_client(&valid);
    EXPECT_TRUE(start_and_interrogate()) << srv_.text() << cli_.text();
}

// Name check: the client expects a DNS name or an address in the server certificate.
TEST_F(Tls, PeerName) {
    const iec104_tls_config s = tls_with("server");
    start_server(&s);
    for (const char* name : {"localhost", "127.0.0.1"}) {
        SCOPED_TRACE(name);
        iec104_tls_config c = tls_with("client");
        put(c.peer_name, name);
        connect_client(&c);
        ASSERT_TRUE(start_and_interrogate()) << cli_.text();
        iec104_client_close(client_);
        ASSERT_TRUE(client_closed());
    }
    iec104_tls_config c = tls_with("client");
    put(c.peer_name, "other.test");
    connect_client(&c);
    ASSERT_TRUE(client_closed());
    EXPECT_EQ(cli_.reason, IEC104_CLOSE_TLS);
    EXPECT_TRUE(cli_.has(IEC104_SECURITY_CERTIFICATE_REJECTED, "mismatch")) << cli_.text();
}

// Paths: the expected peer name can differ per path; a path without its own
// name uses the one of the client settings. Here the client expects
// "other.test", path 1 "localhost": only path 1 gets through.
TEST_F(Tls, PeerNamePerPath) {
    const iec104_tls_config s = tls_with("server");
    start_server(&s);
    const iec104_callbacks cb = cli_.callbacks();
    ASSERT_EQ(iec104_client_create(nullptr, &cb, &client_), IEC104_OK);
    iec104_tls_config c = tls_with("client");
    put(c.peer_name, "other.test");
    ASSERT_EQ(iec104_client_set_tls(client_, &c), IEC104_OK);
    iec104_path_config paths[2]{};
    for (iec104_path_config& p : paths) {
        put(p.host, "127.0.0.1");
        p.port = iec104_server_port(server_);
    }
    put(paths[0].tls_peer_name, "localhost");
    ASSERT_EQ(iec104_client_connect_paths(client_, paths, 2, nullptr), IEC104_OK);
    ASSERT_EQ(iec104_client_start(client_), IEC104_OK);
    ASSERT_TRUE(run_until([&] { return iec104_client_leading_path(client_) == 1U; }))
        << cli_.text();
    iec104_path_info i{};
    ASSERT_TRUE(run_until([&] {
        return iec104_client_get_path_info(client_, 2, &i) == IEC104_OK &&
               i.last_close == IEC104_CLOSE_TLS;
    })) << cli_.text();
    EXPECT_TRUE(cli_.has(IEC104_SECURITY_CERTIFICATE_REJECTED, "mismatch")) << cli_.text();
    iec104_tls_info info{};
    EXPECT_EQ(iec104_client_get_tls_info(client_, &info), IEC104_OK); // of the leading path
}

// Pinning: the peer certificate must have the configured fingerprint (any
// notation of the hex value).
TEST_F(Tls, PeerFingerprint) {
    const iec104_tls_config s = tls_with("server");
    start_server(&s);
    iec104_tls_config c = tls_with("client");
    connect_client(&c);
    ASSERT_TRUE(start_and_interrogate());
    iec104_tls_info info{};
    ASSERT_EQ(iec104_client_get_tls_info(client_, &info), IEC104_OK);
    iec104_client_close(client_);
    ASSERT_TRUE(client_closed());

    std::string plain; // lower case, no colons
    for (const char ch : std::string(info.peer_fingerprint)) {
        if (ch != ':') {
            plain += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
    }
    put(c.peer_fingerprint, plain);
    connect_client(&c);
    ASSERT_TRUE(start_and_interrogate()) << cli_.text();
    iec104_client_close(client_);
    ASSERT_TRUE(client_closed());

    plain[0] = plain[0] == '0' ? '1' : '0';
    put(c.peer_fingerprint, plain);
    connect_client(&c);
    ASSERT_TRUE(client_closed());
    EXPECT_EQ(cli_.reason, IEC104_CLOSE_TLS);
    EXPECT_TRUE(cli_.has(IEC104_SECURITY_CERTIFICATE_REJECTED, "fingerprint")) << cli_.text();
}

// A plain client at a TLS server, and a TLS client at a plain server, both end with an error.
TEST_F(Tls, PlainAndTlsDoNotMix) {
    const iec104_tls_config s = tls_with("server");
    start_server(&s);
    connect_client(nullptr);
    ASSERT_TRUE(run_until([&] { return cli_.state == IEC104_LINK_STOPPED; }));
    (void)iec104_client_start(client_); // STARTDT in clear text instead of a ClientHello
    ASSERT_TRUE(server_closed());
    EXPECT_EQ(srv_.reason, IEC104_CLOSE_TLS);
    EXPECT_TRUE(srv_.has(IEC104_SECURITY_HANDSHAKE_FAILED)) << srv_.text();
    ASSERT_TRUE(client_closed());

    iec104_server_destroy(server_);
    server_ = nullptr;
    iec104_client_destroy(client_);
    client_ = nullptr;
    srv_ = Seen{};
    cli_ = Seen{};
    start_server(nullptr);
    const iec104_tls_config c = tls_with("client");
    connect_client(&c);
    ASSERT_TRUE(client_closed());
    EXPECT_TRUE(cli_.has(IEC104_SECURITY_HANDSHAKE_FAILED)) << cli_.text();
    EXPECT_FALSE(cli_.has(IEC104_SECURITY_TLS_ESTABLISHED));
}

// E9: t0 limits the handshake. A peer that connects but never starts TLS is
// closed after t0.
TEST_F(Tls, HandshakeLimitedByT0) {
    const iec104_tls_config s = tls_with("server");
    start_server(&s, 1000);
    connect_client(nullptr); // plain client without STARTDT: sends nothing
    const auto begin = std::chrono::steady_clock::now();
    ASSERT_TRUE(server_closed());
    const auto took = std::chrono::steady_clock::now() - begin;
    EXPECT_EQ(srv_.reason, IEC104_CLOSE_T0_TIMEOUT);
    EXPECT_TRUE(srv_.has(IEC104_SECURITY_HANDSHAKE_FAILED, "t0")) << srv_.text();
    EXPECT_GE(took, std::chrono::milliseconds(900));
    EXPECT_LT(took, std::chrono::milliseconds(3000));
}

// Key renewal by time: key update (TLS 1.3) and renegotiation (TLS 1.2, by
// either side); data keeps flowing.
TEST_F(Tls, KeyRenewal) {
    const struct {
        uint16_t version;
        bool server_renews;
        const char* kind;
    } cases[] = {
        {IEC104_TLS_1_3, true, "key update"},
        {IEC104_TLS_1_2, true, "renegotiation"},
        {IEC104_TLS_1_2, false, "renegotiation"},
    };
    for (const auto& k : cases) {
        SCOPED_TRACE(std::string(k.kind) + (k.server_renews ? " by server" : " by client"));
        iec104_tls_config s = tls_with("server");
        iec104_tls_config c = tls_with("client");
        c.max_version = k.version;
        (k.server_renews ? s : c).renewal_interval_s = 1;
        start_server(&s);
        connect_client(&c);
        ASSERT_TRUE(start_and_interrogate());
        Seen& renewer = k.server_renews ? srv_ : cli_;
        // Spontaneous values while the keys are renewed twice.
        iec104_object value{};
        value.type = 13;
        value.ioa = 100;
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
        while (std::chrono::steady_clock::now() < end) {
            value.value += 1.0;
            (void)iec104_server_update(server_, 1, &value);
            run_until([] { return false; }, 50);
        }
        EXPECT_GE(renewer.count(IEC104_SECURITY_KEY_RENEWAL), 2) << renewer.text();
        EXPECT_TRUE(renewer.has(IEC104_SECURITY_KEY_RENEWAL, k.kind));
        EXPECT_EQ(cli_.state, IEC104_LINK_STARTED) << srv_.text() << cli_.text();
        EXPECT_TRUE(start_and_interrogate()); // still usable
        iec104_client_destroy(client_);
        client_ = nullptr;
        iec104_server_destroy(server_);
        server_ = nullptr;
        srv_ = Seen{};
        cli_ = Seen{};
    }
}

// Session resumption: the second connect of a client resumes the first
// session; switched off, it does not.
TEST_F(Tls, SessionResumption) {
    const iec104_tls_config s = tls_with("server");
    start_server(&s);
    for (const int resumption : {1, 0}) {
        SCOPED_TRACE(resumption);
        iec104_tls_config c = tls_with("client");
        c.session_resumption = resumption;
        iec104_client_destroy(client_);
        client_ = nullptr;
        connect_client(&c);
        ASSERT_TRUE(start_and_interrogate()); // also receives the session ticket
        iec104_client_close(client_);
        ASSERT_TRUE(client_closed());
        connect_client(nullptr); // same settings
        ASSERT_TRUE(start_and_interrogate());
        iec104_tls_info info{};
        ASSERT_EQ(iec104_client_get_tls_info(client_, &info), IEC104_OK);
        EXPECT_EQ(info.resumed, resumption);
        iec104_client_close(client_);
        ASSERT_TRUE(client_closed());
    }
}

// Certificate change at run time: open connections keep their session, new
// ones get the new certificate.
TEST_F(Tls, CertificateChangeAtRunTime) {
    const iec104_tls_config s1 = tls_with("server");
    const iec104_tls_config s2 = tls_with("server2");
    iec104_tls_config c = tls_with("client");
    c.session_resumption = 0;
    start_server(&s1);
    connect_client(&c);
    ASSERT_TRUE(start_and_interrogate());
    iec104_tls_info first{};
    ASSERT_EQ(iec104_client_get_tls_info(client_, &first), IEC104_OK);

    ASSERT_EQ(iec104_server_set_tls(server_, &s2), IEC104_OK);
    iec104_client* second = nullptr;
    Seen seen2;
    const iec104_callbacks cb = seen2.callbacks();
    ASSERT_EQ(iec104_client_create(nullptr, &cb, &second), IEC104_OK);
    ASSERT_EQ(iec104_client_set_tls(second, &c), IEC104_OK);
    ASSERT_EQ(iec104_client_connect(second, "127.0.0.1", iec104_server_port(server_)), IEC104_OK);
    const bool up = run_until([&] {
        (void)iec104_client_poll(second, 1);
        return seen2.state == IEC104_LINK_STOPPED;
    });
    iec104_tls_info other{};
    const iec104_status st = iec104_client_get_tls_info(second, &other);
    iec104_client_destroy(second);
    ASSERT_TRUE(up) << seen2.text();
    ASSERT_EQ(st, IEC104_OK);
    EXPECT_STRNE(first.peer_fingerprint, other.peer_fingerprint);
    EXPECT_TRUE(start_and_interrogate()); // the first connection still works
}

// Settings are checked when they are set; the previous ones stay on error.
TEST_F(Tls, SettingsErrors) {
    start_server(nullptr);
    iec104_tls_config t = tls_with("server");
    put(t.ca_file, cert("missing.pem"));
    EXPECT_EQ(iec104_server_set_tls(server_, &t), IEC104_ERR_IO);

    t = tls_with("");
    EXPECT_EQ(iec104_server_set_tls(server_, &t), IEC104_ERR_INVALID_ARGUMENT); // no certificate

    t = tls_with("server");
    put(t.private_key_file, cert("client.key"));
    EXPECT_EQ(iec104_server_set_tls(server_, &t), IEC104_ERR_INVALID_ARGUMENT); // wrong key

    t = tls_with("server");
    put(t.cipher_list, "NO-SUCH-SUITE");
    EXPECT_EQ(iec104_server_set_tls(server_, &t), IEC104_ERR_INVALID_ARGUMENT);

    t = tls_with("server");
    t.min_version = IEC104_TLS_1_3;
    t.max_version = IEC104_TLS_1_2;
    EXPECT_EQ(iec104_server_set_tls(server_, &t), IEC104_ERR_INVALID_ARGUMENT);

    t = tls_with("server");
    put(t.peer_fingerprint, "12:34");
    EXPECT_EQ(iec104_server_set_tls(server_, &t), IEC104_ERR_INVALID_ARGUMENT);

    // Encrypted key: right and wrong password.
    iec104_client* c = nullptr;
    ASSERT_EQ(iec104_client_create(nullptr, nullptr, &c), IEC104_OK);
    t = tls_with("client");
    put(t.private_key_file, cert("client_enc.key"));
    put(t.key_password, "test-password");
    EXPECT_EQ(iec104_client_set_tls(c, &t), IEC104_OK);
    put(t.key_password, "wrong");
    EXPECT_EQ(iec104_client_set_tls(c, &t), IEC104_ERR_IO);
    iec104_client_destroy(c);

    // The server still runs without TLS: a plain client gets through.
    connect_client(nullptr);
    EXPECT_TRUE(start_and_interrogate());
}

// Without TLS support every attempt reports UNSUPPORTED (only in such a build).
TEST(TlsBuild, AvailableOrUnsupported) {
    iec104_server* s = nullptr;
    ASSERT_EQ(iec104_server_create(nullptr, nullptr, &s), IEC104_OK);
    iec104_tls_config t{};
    iec104_tls_config_init(&t);
    EXPECT_EQ(t.min_version, IEC104_TLS_1_2);
    EXPECT_EQ(t.max_version, IEC104_TLS_1_3);
    EXPECT_EQ(t.verify_peer, 1);
    EXPECT_EQ(t.session_lifetime_s, 3600U);
    if (iec104_tls_available() == 0) {
        EXPECT_EQ(iec104_server_set_tls(s, &t), IEC104_ERR_UNSUPPORTED);
    }
    EXPECT_EQ(iec104_server_set_tls(s, nullptr), IEC104_OK); // TLS off is always possible
    iec104_server_destroy(s);
}
