/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file fuzz_session.cpp
 * @brief libFuzzer target for the application layer of the controlled station.
 *
 * The input is one ASDU as received from a controlling station (at most 249
 * octets, IEC 104 profile). It is handed to a ServerSession with a small
 * process image; the answers are collected. Invariants (abort on violation):
 *   - no crash, no sanitizer finding;
 *   - every answer has the length of a valid ASDU (1..249 octets);
 *   - every answer decodes again with the codec, except the negative mirror
 *     of a type the profile does not accept (cause 44), which keeps the
 *     request's octets;
 *   - every answer carries a cause used for answers (4, 5, 7, 9, 10,
 *     20..41, 44..47).
 *
 * Build:  cmake --preset fuzz && cmake --build --preset fuzz
 * Seeds:  tools/make_fuzz_seeds.sh (the reference ASDUs of tests/data/)
 * Run:    ./build/fuzz/fuzz_session -max_total_time=300 fuzz/corpus_session
 */
#include "codec/asdu.hpp"
#include "session/process_image.hpp"
#include "session/server_session.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

using namespace iec104;

namespace {

class CollectingSender : public session::AsduSender {
public:
    std::vector<std::vector<std::uint8_t>> sent;
    bool can_send() override { return sent.size() < 10000U; }
    iec104_status send(const std::uint8_t* asdu, std::size_t length) override {
        sent.emplace_back(asdu, asdu + length);
        return IEC104_OK;
    }
};

class AlternatingHandler : public session::CommandHandler {
public:
    bool accept{false};
    bool on_command(const codec::AsduHeader& /*header*/,
                    const codec::InformationObject& /*object*/) override {
        accept = !accept;
        return accept;
    }
};

session::ProcessImage make_image() {
    session::ProcessImage image;
    for (std::uint32_t i = 0; i < 40U; ++i) {
        codec::InformationObject o{};
        o.ioa = 100U + i;
        o.sva = static_cast<std::int16_t>(i);
        (void)image.set(1, 11, o, static_cast<std::uint16_t>(1U << (i % 16U)));
    }
    codec::InformationObject c{};
    c.ioa = 200;
    (void)image.set(1, 15, c, 1);
    (void)image.set(2, 1, c, 2);
    return image;
}

bool answer_cause(std::uint8_t cause) {
    return cause == 4U || cause == 5U || cause == 7U || cause == 9U || cause == 10U ||
           (cause >= 20U && cause <= 41U) || (cause >= 44U && cause <= 47U);
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0U || size > codec::kMaxAsduLength) {
        return 0;
    }
    static const session::ProcessImage image = make_image();
    session::ServerSession s(codec::kIec104Profile, 0);
    AlternatingHandler handler;
    session::SessionOptions options;
    CollectingSender sender;

    s.on_started(image, options);
    (void)s.handle(data, size, image, handler, options);
    s.pump(sender);

    for (const auto& asdu : sender.sent) {
        if (asdu.empty() || asdu.size() > codec::kMaxAsduLength) {
            std::abort();
        }
        codec::AsduHeader h{};
        std::size_t n = 0;
        if (codec::decode_asdu_header(asdu.data(), asdu.size(), codec::kIec104Profile, h, n) !=
                IEC104_OK ||
            !answer_cause(h.cause)) {
            std::abort();
        }
        if (h.cause != 44U) {
            std::array<codec::InformationObject, codec::kMaxObjects> objects{};
            codec::DecodedAsdu d{};
            if (codec::decode_asdu(asdu.data(), asdu.size(), codec::kIec104Profile,
                                   objects.data(), objects.size(), d) != IEC104_OK) {
                std::abort();
            }
        }
    }
    return 0;
}
