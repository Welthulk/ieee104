/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file tls_openssl.cpp
 * @brief TLS backend on OpenSSL 3.x (see tls.hpp).
 *
 * Profile (docs/05_Normfragen_und_Festlegungen.md, N12 and E9):
 *
 * | Setting               | Default                                              |
 * |-----------------------|------------------------------------------------------|
 * | Versions              | TLS 1.2 and 1.3                                      |
 * | TLS 1.2 suites        | ECDHE with AEAD only (GCM, ChaCha20-Poly1305)        |
 * | TLS 1.3 suites        | the three suites of RFC 8446 without CCM             |
 * | Peer authentication   | mutual, X.509 chain to the configured CA             |
 * | Renegotiation (1.2)   | allowed in both directions, secure only (RFC 5746)   |
 * | Session resumption    | on, lifetime 1 h                                     |
 *
 * TODO(norm): IEC 62351-3 Ed. 2, table of mandatory cipher suites, and
 * clauses 7.4.4/7.4.5 (resumption, renegotiation) and 8.4 (key update):
 * defaults above are assumptions (docs/05, N12).
 *
 * TLS runs over the library's own socket functions (a custom BIO), so that
 * a write never raises SIGPIPE and the socket stays owned by the link.
 *
 * Layer: transport (I/O).
 */
#include "transport/tls.hpp"

#include <array>
#include <cstring>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>
#include <poll.h>
#include <utility>

namespace iec104::transport {

namespace {

constexpr const char* kDefaultCipherList =
    "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:"
    "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305";
constexpr const char* kDefaultCipherSuites =
    "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256";

// Session id context of the server; needed for resumption with client certificates.
constexpr std::array<unsigned char, 6> kSessionIdContext{'i', 'e', 'c', '1', '0', '4'};

struct SslCtxFree {
    void operator()(SSL_CTX* p) const noexcept { SSL_CTX_free(p); }
};
struct SslFree {
    void operator()(SSL* p) const noexcept { SSL_free(p); }
};
struct SslSessionFree {
    void operator()(SSL_SESSION* p) const noexcept { SSL_SESSION_free(p); }
};
struct BioFree {
    void operator()(BIO* p) const noexcept { BIO_free(p); }
};
using SslCtxPtr = std::unique_ptr<SSL_CTX, SslCtxFree>;
using SslPtr = std::unique_ptr<SSL, SslFree>;
using SslSessionPtr = std::unique_ptr<SSL_SESSION, SslSessionFree>;
using BioPtr = std::unique_ptr<BIO, BioFree>;

/** @brief First entry of OpenSSL's error queue as text; empties the queue. */
std::string take_openssl_error() {
    std::string text;
    unsigned long e = 0;
    while ((e = ERR_get_error()) != 0U) {
        if (text.empty()) {
            const char* reason = ERR_reason_error_string(e);
            text = reason != nullptr ? reason : "OpenSSL error " + std::to_string(e);
        }
    }
    return text;
}

// --- Socket BIO ---------------------------------------------------------------

/** @brief What the BIO works on: the descriptor and the result of the last socket call. */
struct IoState {
    int fd{-1};
    iec104_status last{IEC104_OK};
};

IoState& io_of(BIO* bio) noexcept {
    return *static_cast<IoState*>(BIO_get_data(bio));
}

int bio_write(BIO* bio, const char* data, int length) {
    BIO_clear_retry_flags(bio);
    IoState& io = io_of(bio);
    std::size_t written = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): octets, same object
    const auto* octets = reinterpret_cast<const std::uint8_t*>(data);
    io.last = write_some(io.fd, octets, static_cast<std::size_t>(length), written);
    if (io.last == IEC104_OK && written > 0U) {
        return static_cast<int>(written);
    }
    if (io.last == IEC104_OK) { // socket buffer full
        BIO_set_retry_write(bio);
    }
    return -1;
}

int bio_read(BIO* bio, char* buf, int size) {
    BIO_clear_retry_flags(bio);
    IoState& io = io_of(bio);
    std::size_t read = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): octets, same object
    auto* octets = reinterpret_cast<std::uint8_t*>(buf);
    io.last = read_some(io.fd, octets, static_cast<std::size_t>(size), read);
    switch (io.last) {
    case IEC104_OK:
        return static_cast<int>(read);
    case IEC104_ERR_WOULD_BLOCK:
        BIO_set_retry_read(bio);
        return -1;
    case IEC104_ERR_CLOSED:
        return 0; // end of stream
    default:
        return -1;
    }
}

long bio_ctrl(BIO* /*bio*/, int command, long /*number*/, void* /*pointer*/) {
    return command == BIO_CTRL_FLUSH ? 1 : 0; // the socket has no buffer of its own
}

BIO_METHOD* socket_bio_method() noexcept {
    // Created once, lives as long as the process.
    static BIO_METHOD* const method = [] {
        BIO_METHOD* m = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "iec104 socket");
        if (m != nullptr) {
            BIO_meth_set_write(m, bio_write);
            BIO_meth_set_read(m, bio_read);
            BIO_meth_set_ctrl(m, bio_ctrl);
        }
        return m;
    }();
    return method;
}

// --- Session -------------------------------------------------------------------

class OpenSslSession final : public TlsSession {
  public:
    OpenSslSession(int fd, bool renegotiation, std::string fingerprint) noexcept
        : renegotiation_(renegotiation), fingerprint_(std::move(fingerprint)) {
        io_.fd = fd;
    }
    OpenSslSession(const OpenSslSession&) = delete;
    OpenSslSession& operator=(const OpenSslSession&) = delete;
    OpenSslSession(OpenSslSession&&) = delete;
    OpenSslSession& operator=(OpenSslSession&&) = delete;
    ~OpenSslSession() override = default;

    /** @brief Creates the SSL object with the socket BIO; false on failure. */
    bool init(SSL_CTX* ctx, TlsRole role, const std::string& server_name) noexcept {
        ssl_.reset(SSL_new(ctx));
        BioPtr bio(BIO_new(socket_bio_method()));
        if (!ssl_ || !bio) {
            return false;
        }
        BIO_set_data(bio.get(), &io_);
        BIO_set_init(bio.get(), 1);
        BIO* raw = bio.release();
        SSL_set_bio(ssl_.get(), raw, raw); // the SSL object owns the BIO now
        // Counts completed handshakes, so that a renegotiation by the peer is seen.
        (void)SSL_set_app_data(ssl_.get(), this);
        SSL_set_info_callback(ssl_.get(), &OpenSslSession::on_info);
        if (role == TlsRole::Server) {
            SSL_set_accept_state(ssl_.get());
        } else {
            SSL_set_connect_state(ssl_.get());
            // Server Name Indication (RFC 6066) only for names, not for addresses.
            if (!server_name.empty() && !is_ip_address(server_name)) {
                (void)SSL_set_tlsext_host_name(ssl_.get(), server_name.c_str());
            }
        }
        return true;
    }

    /** @brief Offers @p session of an earlier connection for resumption (client). */
    void offer(SSL_SESSION* session) noexcept { (void)SSL_set_session(ssl_.get(), session); }

    iec104_status handshake() noexcept override {
        ERR_clear_error();
        const int rc = SSL_do_handshake(ssl_.get());
        if (rc == 1) {
            return check_fingerprint();
        }
        const int e = SSL_get_error(ssl_.get(), rc);
        if (would_block(e)) {
            return IEC104_ERR_WOULD_BLOCK;
        }
        fatal_ = true;
        const long verify = SSL_get_verify_result(ssl_.get());
        if (verify != X509_V_OK) {
            failure_ = TlsFailure::Certificate;
            error_ = X509_verify_cert_error_string(verify);
            ERR_clear_error();
        } else {
            failure_ = TlsFailure::Handshake;
            error_ = take_openssl_error();
            if (error_.empty()) {
                error_ = "connection closed by peer";
            }
        }
        return IEC104_ERR_IO;
    }

    iec104_status read(std::uint8_t* buf, std::size_t size, std::size_t& read) noexcept override {
        read = 0;
        if (fatal_) {
            return IEC104_ERR_IO;
        }
        ERR_clear_error();
        if (SSL_read_ex(ssl_.get(), buf, size, &read) == 1) {
            return IEC104_OK;
        }
        return io_error(SSL_get_error(ssl_.get(), 0));
    }

    iec104_status write(const std::uint8_t* data, std::size_t size,
                        std::size_t& written) noexcept override {
        written = 0;
        if (fatal_) {
            return IEC104_ERR_IO;
        }
        if (size == 0U) {
            return IEC104_OK;
        }
        ERR_clear_error();
        if (SSL_write_ex(ssl_.get(), data, size, &written) == 1) {
            return IEC104_OK;
        }
        return io_error(SSL_get_error(ssl_.get(), 0));
    }

    [[nodiscard]] short poll_events() const noexcept override { return want_; }

    iec104_status renew() noexcept override {
        if (fatal_) {
            return IEC104_ERR_IO;
        }
        ERR_clear_error();
        if (SSL_version(ssl_.get()) >= TLS1_3_VERSION) {
            // Key update; the peer is asked to update its keys as well (RFC 8446, 4.6.3).
            if (SSL_key_update(ssl_.get(), SSL_KEY_UPDATE_REQUESTED) != 1) {
                ERR_clear_error();
                return IEC104_ERR_WOULD_BLOCK; // an update is still pending: next interval
            }
        } else {
            if (!renegotiation_) {
                return IEC104_ERR_UNSUPPORTED;
            }
            if (SSL_renegotiate(ssl_.get()) != 1) {
                ERR_clear_error();
                return IEC104_ERR_WOULD_BLOCK;
            }
        }
        // Sends the key update or the renegotiation request now; the rest of
        // a renegotiation runs inside the following reads and writes.
        const int rc = SSL_do_handshake(ssl_.get());
        if (rc == 1) {
            return IEC104_OK;
        }
        const iec104_status st = io_error(SSL_get_error(ssl_.get(), rc));
        return st == IEC104_ERR_WOULD_BLOCK ? IEC104_OK : st;
    }

    void shutdown() noexcept override {
        // Not after a fatal error (OpenSSL forbids it) and not before the handshake.
        if (!fatal_ && SSL_is_init_finished(ssl_.get()) == 1) {
            ERR_clear_error();
            (void)SSL_shutdown(ssl_.get());
            ERR_clear_error();
        }
    }

    [[nodiscard]] TlsInfo info() const override {
        TlsInfo i;
        i.version = SSL_get_version(ssl_.get());
        const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl_.get());
        i.cipher = cipher != nullptr ? SSL_CIPHER_get_name(cipher) : "";
        i.resumed = SSL_session_reused(ssl_.get()) == 1;
        const X509* peer = SSL_get0_peer_certificate(ssl_.get());
        if (peer != nullptr) {
            i.peer_subject = subject_of(peer);
            i.peer_fingerprint = fingerprint_of(peer);
        }
        return i;
    }

    [[nodiscard]] std::uint32_t handshakes() const noexcept override { return handshakes_; }

    [[nodiscard]] TlsFailure failure() const noexcept override { return failure_; }
    [[nodiscard]] const std::string& error() const noexcept override { return error_; }

    /** @brief True for "192.0.2.1", "::1" (checked with OpenSSL's own parser). */
    static bool is_ip_address(const std::string& name) {
        ASN1_OCTET_STRING* ip = a2i_IPADDRESS(name.c_str());
        const bool valid = ip != nullptr;
        ASN1_OCTET_STRING_free(ip);
        return valid;
    }

    /** @brief SHA-256 of the DER form of @p cert as "AB:CD:...". */
    static std::string fingerprint_of(const X509* cert) {
        std::array<unsigned char, EVP_MAX_MD_SIZE> md{};
        unsigned int length = 0;
        if (X509_digest(cert, EVP_sha256(), md.data(), &length) != 1) {
            return {};
        }
        static constexpr std::array<char, 16> kHex{'0', '1', '2', '3', '4', '5', '6', '7',
                                                   '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
        std::string text;
        for (unsigned int k = 0; k < length; ++k) {
            if (k > 0U) {
                text += ':';
            }
            text += kHex.at(md.at(k) >> 4U);
            text += kHex.at(md.at(k) & 0x0FU);
        }
        return text;
    }

  private:
    // TLS 1.2 only: in TLS 1.3 post-handshake messages (tickets, key
    // updates) also end with "handshake done".
    static void on_info(const SSL* ssl, int where, int /*ret*/) {
        if ((where & SSL_CB_HANDSHAKE_DONE) != 0 && SSL_version(ssl) < TLS1_3_VERSION) {
            auto* self = static_cast<OpenSslSession*>(SSL_get_app_data(ssl));
            if (self != nullptr) {
                ++self->handshakes_;
            }
        }
    }

    static bool is_want(int e) noexcept {
        return e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE;
    }

    bool would_block(int e) noexcept {
        if (!is_want(e)) {
            return false;
        }
        want_ = e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT;
        return true;
    }

    // Maps the error of a read, write or renewal to a status.
    iec104_status io_error(int e) noexcept {
        if (would_block(e)) {
            return IEC104_ERR_WOULD_BLOCK;
        }
        fatal_ = true;
        const unsigned long first = ERR_peek_error();
        // End of stream: close_notify (ZERO_RETURN), or TCP closed without it.
        if (e == SSL_ERROR_ZERO_RETURN ||
            (e == SSL_ERROR_SSL && ERR_GET_REASON(first) == SSL_R_UNEXPECTED_EOF_WHILE_READING) ||
            (e == SSL_ERROR_SYSCALL && io_.last == IEC104_ERR_CLOSED)) {
            ERR_clear_error();
            return IEC104_ERR_CLOSED;
        }
        failure_ = TlsFailure::Protocol;
        error_ = take_openssl_error();
        if (error_.empty()) {
            error_ = e == SSL_ERROR_SYSCALL ? "socket error" : "TLS error";
        }
        return IEC104_ERR_IO;
    }

    // Pinning: the peer certificate must have the configured SHA-256 fingerprint.
    iec104_status check_fingerprint() noexcept {
        if (fingerprint_.empty()) {
            return IEC104_OK;
        }
        const X509* peer = SSL_get0_peer_certificate(ssl_.get());
        if (peer != nullptr && fingerprint_of(peer) == fingerprint_) {
            return IEC104_OK;
        }
        fatal_ = true;
        failure_ = TlsFailure::Certificate;
        error_ = "peer certificate does not match the configured fingerprint";
        return IEC104_ERR_IO;
    }

    static std::string subject_of(const X509* cert) {
        const BioPtr mem(BIO_new(BIO_s_mem()));
        if (!mem) {
            return {};
        }
        (void)X509_NAME_print_ex(mem.get(), X509_get_subject_name(cert), 0, XN_FLAG_RFC2253);
        char* data = nullptr;
        const long n = BIO_get_mem_data(mem.get(), &data);
        return n > 0 ? std::string(data, static_cast<std::size_t>(n)) : std::string();
    }

    IoState io_;
    SslPtr ssl_;
    short want_{POLLIN};
    bool fatal_{false};
    std::uint32_t handshakes_{0};
    bool renegotiation_;
    std::string fingerprint_;
    TlsFailure failure_{TlsFailure::None};
    std::string error_;
};

// --- Context -------------------------------------------------------------------

class OpenSslContext final : public TlsContext {
  public:
    OpenSslContext(TlsRole role, TlsSettings settings) noexcept
        : role_(role), settings_(std::move(settings)) {}
    OpenSslContext(const OpenSslContext&) = delete;
    OpenSslContext& operator=(const OpenSslContext&) = delete;
    OpenSslContext(OpenSslContext&&) = delete;
    OpenSslContext& operator=(OpenSslContext&&) = delete;
    ~OpenSslContext() override = default;

    /** @brief Builds the SSL_CTX from the settings (see create_tls_context()). */
    iec104_status init(std::string& error) {
        ctx_.reset(
            SSL_CTX_new(role_ == TlsRole::Server ? TLS_server_method() : TLS_client_method()));
        if (!ctx_) {
            error = "cannot create TLS context: " + take_openssl_error();
            return IEC104_ERR_INTERNAL;
        }
        iec104_status st = set_protocol(error);
        if (st == IEC104_OK) {
            st = load_credentials(error);
        }
        if (st == IEC104_OK) {
            st = set_verification(error);
        }
        if (st == IEC104_OK) {
            set_sessions();
        }
        ERR_clear_error();
        return st;
    }

    iec104_status create_session(const Socket& socket,
                                 std::unique_ptr<TlsSession>& out) noexcept override {
        auto session =
            std::make_unique<OpenSslSession>(socket.fd(), settings_.renegotiation,
                                             normalize_fingerprint(settings_.peer_fingerprint));
        if (!session->init(ctx_.get(), role_, settings_.peer_name)) {
            ERR_clear_error();
            return IEC104_ERR_INTERNAL;
        }
        if (role_ == TlsRole::Client && cached_) {
            session->offer(cached_.get());
        }
        out = std::move(session);
        return IEC104_OK;
    }

    [[nodiscard]] std::uint32_t renewal_interval_ms() const noexcept override {
        return settings_.renewal_interval_ms;
    }

  private:
    iec104_status set_protocol(std::string& error) {
        const auto valid = [](std::uint16_t v) { return v == kTls12 || v == kTls13; };
        if (!valid(settings_.min_version) || !valid(settings_.max_version) ||
            settings_.min_version > settings_.max_version) {
            error = "TLS versions: only 1.2 and 1.3, minimum not above maximum";
            return IEC104_ERR_INVALID_ARGUMENT;
        }
        SSL_CTX* c = ctx_.get();
        if (SSL_CTX_set_min_proto_version(c, settings_.min_version) != 1 ||
            SSL_CTX_set_max_proto_version(c, settings_.max_version) != 1) {
            error = "TLS versions: " + take_openssl_error();
            return IEC104_ERR_INVALID_ARGUMENT;
        }
        const std::string& list =
            settings_.cipher_list.empty() ? kDefaultCipherList : settings_.cipher_list;
        if (SSL_CTX_set_cipher_list(c, list.c_str()) != 1) {
            error = "no usable TLS 1.2 cipher suite in \"" + list + "\"";
            return IEC104_ERR_INVALID_ARGUMENT;
        }
        const std::string& suites =
            settings_.cipher_suites.empty() ? kDefaultCipherSuites : settings_.cipher_suites;
        if (SSL_CTX_set_ciphersuites(c, suites.c_str()) != 1) {
            error = "no usable TLS 1.3 cipher suite in \"" + suites + "\"";
            return IEC104_ERR_INVALID_ARGUMENT;
        }
        // Partial writes: the link keeps unsent octets itself and may move them.
        SSL_CTX_set_mode(c, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
        std::uint64_t options = SSL_OP_NO_COMPRESSION;
        if (role_ == TlsRole::Server) {
            options |= SSL_OP_CIPHER_SERVER_PREFERENCE;
        }
        // Renegotiation is always the secure one (RFC 5746); OpenSSL refuses
        // the legacy form by default. Off means off in both directions.
        if (!settings_.renegotiation) {
            options |= SSL_OP_NO_RENEGOTIATION;
        } else if (role_ == TlsRole::Server) {
            options |= SSL_OP_ALLOW_CLIENT_RENEGOTIATION;
        }
        SSL_CTX_set_options(c, options);
        return IEC104_OK;
    }

    static int password_callback(char* buf, int size, int /*rwflag*/, void* user) {
        const auto* password = static_cast<const std::string*>(user);
        if (password == nullptr || password->size() >= static_cast<std::size_t>(size)) {
            return 0;
        }
        // OpenSSL takes the length, not a terminated string.
        return static_cast<int>(password->copy(buf, password->size()));
    }

    iec104_status load_credentials(std::string& error) {
        SSL_CTX* c = ctx_.get();
        if (settings_.certificate_file.empty() != settings_.private_key_file.empty()) {
            error = "certificate and private key must be given together";
            return IEC104_ERR_INVALID_ARGUMENT;
        }
        if (settings_.certificate_file.empty()) {
            if (role_ == TlsRole::Server) {
                error = "a TLS server needs a certificate and a private key";
                return IEC104_ERR_INVALID_ARGUMENT;
            }
            return IEC104_OK; // client without certificate: only if the server allows it
        }
        if (SSL_CTX_use_certificate_chain_file(c, settings_.certificate_file.c_str()) != 1) {
            error = "cannot load certificate " + settings_.certificate_file + ": " +
                    take_openssl_error();
            return IEC104_ERR_IO;
        }
        SSL_CTX_set_default_passwd_cb(c, password_callback);
        SSL_CTX_set_default_passwd_cb_userdata(c, &settings_.key_password);
        const int loaded =
            SSL_CTX_use_PrivateKey_file(c, settings_.private_key_file.c_str(), SSL_FILETYPE_PEM);
        SSL_CTX_set_default_passwd_cb(c, nullptr);
        SSL_CTX_set_default_passwd_cb_userdata(c, nullptr);
        if (loaded != 1) {
            if (ERR_GET_REASON(ERR_peek_last_error()) == X509_R_KEY_VALUES_MISMATCH) {
                ERR_clear_error();
                error = "private key does not match the certificate";
                return IEC104_ERR_INVALID_ARGUMENT;
            }
            error = "cannot load private key " + settings_.private_key_file + ": " +
                    take_openssl_error();
            return IEC104_ERR_IO;
        }
        if (SSL_CTX_check_private_key(c) != 1) {
            error = "private key does not match the certificate";
            return IEC104_ERR_INVALID_ARGUMENT;
        }
        return IEC104_OK;
    }

    iec104_status set_verification(std::string& error) {
        SSL_CTX* c = ctx_.get();
        if (!settings_.verify_peer) {
            SSL_CTX_set_verify(c, SSL_VERIFY_NONE, nullptr);
            return IEC104_OK;
        }
        if (settings_.ca_file.empty()) {
            error = "a CA file is needed to verify the peer";
            return IEC104_ERR_INVALID_ARGUMENT;
        }
        if (SSL_CTX_load_verify_locations(c, settings_.ca_file.c_str(), nullptr) != 1) {
            error = "cannot load CA file " + settings_.ca_file + ": " + take_openssl_error();
            return IEC104_ERR_IO;
        }
        if (!settings_.crl_file.empty()) {
            X509_STORE* store = SSL_CTX_get_cert_store(c);
            X509_LOOKUP* lookup = X509_STORE_add_lookup(store, X509_LOOKUP_file());
            if (lookup == nullptr ||
                X509_load_crl_file(lookup, settings_.crl_file.c_str(), X509_FILETYPE_PEM) <= 0) {
                error = "cannot load CRL file " + settings_.crl_file + ": " + take_openssl_error();
                return IEC104_ERR_IO;
            }
            // Every certificate of the chain is checked against a CRL of its issuer.
            (void)X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
        }
        if (!settings_.peer_name.empty()) {
            X509_VERIFY_PARAM* param = SSL_CTX_get0_param(c);
            const bool ok =
                OpenSslSession::is_ip_address(settings_.peer_name)
                    ? X509_VERIFY_PARAM_set1_ip_asc(param, settings_.peer_name.c_str()) == 1
                    : X509_VERIFY_PARAM_set1_host(param, settings_.peer_name.c_str(), 0) == 1;
            if (!ok) {
                error = "invalid peer name \"" + settings_.peer_name + "\"";
                return IEC104_ERR_INVALID_ARGUMENT;
            }
        }
        if (!settings_.peer_fingerprint.empty() &&
            normalize_fingerprint(settings_.peer_fingerprint).empty()) {
            error = "peer fingerprint is not a SHA-256 value in hex";
            return IEC104_ERR_INVALID_ARGUMENT;
        }
        // Mutual authentication: the server insists on a client certificate.
        const int mode = role_ == TlsRole::Server
                             ? SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT
                             : SSL_VERIFY_PEER;
        SSL_CTX_set_verify(c, mode, nullptr);
        return IEC104_OK;
    }

    void set_sessions() {
        SSL_CTX* c = ctx_.get();
        if (!settings_.session_resumption) {
            SSL_CTX_set_session_cache_mode(c, SSL_SESS_CACHE_OFF);
            SSL_CTX_set_options(c, SSL_OP_NO_TICKET);
            (void)SSL_CTX_set_num_tickets(c, 0);
            return;
        }
        SSL_CTX_set_timeout(c, static_cast<long>(settings_.session_lifetime_s));
        if (role_ == TlsRole::Server) {
            (void)SSL_CTX_set_session_id_context(c, kSessionIdContext.data(),
                                                 kSessionIdContext.size());
            return;
        }
        // Client: keep the last session of this context for the next connect.
        SSL_CTX_set_session_cache_mode(c, SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_STORE);
        (void)SSL_CTX_set_app_data(c, this);
        SSL_CTX_sess_set_new_cb(c, &OpenSslContext::on_new_session);
    }

    static int on_new_session(SSL* ssl, SSL_SESSION* session) {
        auto* self = static_cast<OpenSslContext*>(SSL_CTX_get_app_data(SSL_get_SSL_CTX(ssl)));
        if (self == nullptr) {
            return 0;
        }
        self->cached_.reset(session);
        return 1; // the context owns the session now
    }

    TlsRole role_;
    TlsSettings settings_;
    SslCtxPtr ctx_;
    SslSessionPtr cached_;
};

} // namespace

iec104_status create_tls_context(TlsRole role, const TlsSettings& settings,
                                 std::shared_ptr<TlsContext>& out, std::string& error) {
    auto ctx = std::make_shared<OpenSslContext>(role, settings);
    const iec104_status st = ctx->init(error);
    if (st == IEC104_OK) {
        out = std::move(ctx);
    }
    return st;
}

bool tls_available() noexcept {
    return true;
}

} // namespace iec104::transport
