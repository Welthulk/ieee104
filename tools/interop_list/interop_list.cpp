/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file interop_list.cpp
 * @brief iec104_interop_list: prints the library's interoperability list.
 *
 *   iec104_interop_list [--role station|master|both] [--config FILE] [--format md|csv]
 *
 * The list follows the checklist of clause 9 (interop_data.hpp). Parameters
 * (field lengths, timers, k, w, port, redundancy groups, TLS) come from the
 * configuration file, else from the defaults. Markdown is for reading and
 * handing over; CSV (one row per item, ';' separated) is for comparing with
 * the list of a peer in a spreadsheet.
 */
#include "interop_data.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iec104/iec104.h>
#include <set>
#include <string>
#include <vector>

namespace {

using interop::Group;
using interop::Mark;

struct Options {
    bool station{true};
    bool master{true};
    bool csv{false};
    std::string config_file;
};

const char* mark_text(Mark m) {
    switch (m) {
    case Mark::Yes:
        return "X";
    case Mark::Raw:
        return "X roh";
    case Mark::Partial:
        return "teilw.";
    case Mark::NotIn104:
        return "–";
    case Mark::No:
        break;
    }
    return "";
}

/** @brief Parses "3 5 20-36" into a set of causes. */
std::set<int> causes(const char* spec) {
    std::set<int> out;
    const std::string s = spec;
    std::size_t pos = 0;
    while (pos < s.size()) {
        const std::size_t end = s.find(' ', pos);
        const std::string part =
            s.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos = end == std::string::npos ? s.size() : end + 1U;
        if (part.empty()) {
            continue;
        }
        const std::size_t dash = part.find('-');
        const int from = std::atoi(part.c_str());
        const int to = dash == std::string::npos ? from : std::atoi(part.c_str() + dash + 1U);
        for (int c = from; c <= to; ++c) {
            out.insert(c);
        }
    }
    return out;
}

/** @brief Columns of the type/cause matrix; 20 stands for 20..36, 37 for 37..41. */
struct CauseColumn {
    int first;
    int last;
    const char* label;
};
constexpr CauseColumn kColumns[] = {
    {1, 1, "1"},    {2, 2, "2"},    {3, 3, "3"},    {4, 4, "4"},       {5, 5, "5"},
    {6, 6, "6"},    {7, 7, "7"},    {8, 8, "8"},    {9, 9, "9"},       {10, 10, "10"},
    {11, 11, "11"}, {12, 12, "12"}, {13, 13, "13"}, {20, 36, "20–36"}, {37, 41, "37–41"},
    {44, 44, "44"}, {45, 45, "45"}, {46, 46, "46"}, {47, 47, "47"},
};

bool column_used(const std::set<int>& used, const CauseColumn& c) {
    for (int x = c.first; x <= c.last; ++x) {
        if (used.count(x) != 0U) {
            return true;
        }
    }
    return false;
}

const char* group_title(Group g) {
    switch (g) {
    case Group::Monitor:
        return "Process information in monitor direction";
    case Group::Control:
        return "Process information in control direction";
    case Group::SystemMonitor:
        return "System information in monitor direction";
    case Group::SystemControl:
        return "System information in control direction";
    case Group::Parameter:
        return "Parameter in control direction";
    case Group::File:
        return "File transfer";
    }
    return "";
}

/** @brief Parameters of the list, from the configuration. */
struct Parameters {
    iec104_config config{};
    bool tls{false};
};

// --- Output ----------------------------------------------------------------------

class Writer {
  public:
    explicit Writer(const Options& o) : o_(o) {}

    void heading(int level, const std::string& text) const {
        if (!o_.csv) {
            std::printf("\n%s %s\n\n", std::string(static_cast<std::size_t>(level), '#').c_str(),
                        text.c_str());
        }
    }
    void text(const std::string& t) const {
        if (!o_.csv) {
            std::printf("%s\n", t.c_str());
        }
    }
    /** @brief Header of an item table with the role columns. */
    void item_table(const char* first, const char* second) const {
        if (o_.csv) {
            return;
        }
        std::printf("| %s | %s |%s%s Bemerkung |\n| --- | --- |%s%s --- |\n", first, second,
                    o_.station ? " Station |" : "", o_.master ? " Leitstelle |" : "",
                    o_.station ? " --- |" : "", o_.master ? " --- |" : "");
    }
    /** @brief One item: section (CSV only), two text columns, marks, remark. */
    void item(const std::string& section, const std::string& a, const std::string& b, Mark station,
              Mark master, const std::string& remark) const {
        if (o_.csv) {
            std::printf("%s;%s;%s;%s;%s;%s\n", csv(section).c_str(), csv(a).c_str(), csv(b).c_str(),
                        o_.station ? mark_text(station) : "", o_.master ? mark_text(master) : "",
                        csv(remark).c_str());
            return;
        }
        std::printf("| %s | %s |", a.c_str(), b.c_str());
        if (o_.station) {
            std::printf(" %s |", mark_text(station));
        }
        if (o_.master) {
            std::printf(" %s |", mark_text(master));
        }
        std::printf(" %s |\n", remark.c_str());
    }
    /** @brief A value row (parameters): same columns, values instead of marks. */
    void value(const std::string& section, const std::string& name, const std::string& standard,
               const std::string& selected, const std::string& remark) const {
        if (o_.csv) {
            std::printf("%s;%s;%s;%s;;%s\n", csv(section).c_str(), csv(name).c_str(),
                        csv(standard).c_str(), csv(selected).c_str(), csv(remark).c_str());
            return;
        }
        std::printf("| %s | %s | %s | %s |\n", name.c_str(), standard.c_str(), selected.c_str(),
                    remark.c_str());
    }

  private:
    static std::string csv(const std::string& s) {
        std::string out;
        for (const char c : s) {
            out += c == ';' ? ',' : c;
        }
        return out;
    }
    const Options& o_;
};

std::string seconds(std::uint32_t ms) {
    char buf[32];
    if (ms % 1000U == 0U) {
        std::snprintf(buf, sizeof buf, "%u s", ms / 1000U);
    } else {
        std::snprintf(buf, sizeof buf, "%.3g s", static_cast<double>(ms) / 1000.0);
    }
    return buf;
}

std::string octets(unsigned n) {
    return n == 1U ? "1 Oktett" : std::to_string(n) + " Oktette";
}

void print_header(const Writer& w, const Options& o, const Parameters& p) {
    if (o.csv) {
        std::printf("Abschnitt;Punkt;Bezeichnung;Station;Leitstelle;Bemerkung\n");
        return;
    }
    std::printf("# Interoperabilitätsliste (IEC 60870-5-104, Abschnitt 9)\n\n");
    std::printf("IEC 60870-5-104 Library %s · Rolle: %s · Parameter: %s\n", iec104_version_string(),
                o.station && o.master ? "Station und Leitstelle"
                : o.station           ? "Station (gesteuerte Station, Server)"
                                      : "Leitstelle (steuernde Station, Client)",
                o.config_file.empty() ? "Standardwerte" : o.config_file.c_str());
    w.text("");
    w.text("Erzeugt mit `iec104_interop_list`. Gliederung und Bezeichnungen nach öffentlichen "
           "Wiedergaben der Checkliste (docs/02_Technik_und_Quellen.md, [ORA]); die Angaben "
           "stammen aus dem Code der Library. Eigene Parameter: `iec104_interop_list --config "
           "datei.conf`, zum Abgleich mit der Liste einer Gegenstelle `--format csv`.");
    w.text("");
    w.text("| Zeichen | Bedeutung |\n| --- | --- |\n"
           "| X | unterstützt, von der Library selbst oder über ihre Schnittstelle |\n"
           "| X roh | nur als rohe ASDU (`on_asdu`, `*_send_asdu`) |\n"
           "| teilw. | teilweise; die Bemerkung sagt, was die Anwendung tun muss |\n"
           "| – | in IEC 104 nicht vorgesehen (schwarzes Feld der Checkliste) |\n"
           "| (leer) | nicht unterstützt |");
    (void)p;
}

void print_system(const Writer& w, const Options& o) {
    w.heading(2, "9.1 System or device");
    w.item_table("Punkt", "Bezeichnung");
    w.item("9.1", "Controlling station definition", "Master, Client", Mark::No,
           o.master ? Mark::Yes : Mark::No, "iec104_client_*");
    w.item("9.1", "Controlled station definition", "Slave, Server",
           o.station ? Mark::Yes : Mark::No, Mark::No, "iec104_server_*");
    w.heading(2, "9.2 Network configuration, 9.3 Physical layer, 9.4 Link layer");
    w.text("Entfallen in IEC 104 (schwarze Felder): Übertragung über TCP/IP.");
}

void print_application_layer(const Writer& w, const Parameters& p) {
    const iec104_link_config& l = p.config.link;
    w.heading(2, "9.5 Application layer");
    w.item_table("Punkt", "Wert");
    w.item("9.5", "Transmission mode for application data",
           "Mode 1 (least significant octet first)", Mark::Yes, Mark::Yes, "");
    w.item("9.5", "Common address of ASDU", octets(l.ca_length), Mark::Yes, Mark::Yes,
           l.ca_length == 2U ? "Norm: zwei Oktette" : "abweichend von der Norm (ca_length)");
    w.item("9.5", "Information object address", octets(l.ioa_length) + ", unstrukturiert",
           Mark::Yes, Mark::Yes,
           l.ioa_length == 3U ? "Norm: drei Oktette" : "abweichend von der Norm (ioa_length)");
    w.item("9.5", "Cause of transmission",
           octets(l.cot_length) + (l.cot_length == 2U
                                       ? " mit Herkunftsadresse " + std::to_string(l.originator)
                                       : std::string()),
           Mark::Yes, Mark::Yes,
           l.cot_length == 2U ? "Norm: zwei Oktette" : "abweichend von der Norm");
    w.item("9.5", "Length of APDU",
           "max. " + std::to_string(4U + static_cast<unsigned>(l.max_asdu_length)) +
               " Oktette (ASDU " + std::to_string(l.max_asdu_length) + ")",
           Mark::Yes, Mark::Yes, "Norm: 253; empfangen werden immer bis 253 (max_asdu_length)");
}

void print_types(const Writer& w, const Options& o) {
    Group current = Group::File;
    bool first = true;
    for (const interop::TypeRow& t : interop::kTypes) {
        if (first || t.group != current) {
            current = t.group;
            first = false;
            w.heading(3, std::string("9.5 ") + group_title(t.group));
            w.item_table("Typ", "Bezeichnung");
        }
        w.item(std::string("9.5 ") + group_title(t.group),
               "<" + std::to_string(t.id) + "> " + t.name, t.description, t.station, t.master,
               t.remark);
    }
    (void)o;
}

void print_matrix(const Writer& w, const Options& o, bool station) {
    const char* who = station ? "Station" : "Leitstelle";
    w.heading(3, std::string("9.5 Type identification and cause of transmission assignments (") +
                     who + ")");
    if (!o.csv) {
        std::printf("Ursachen, die die %s sendet oder auswertet.\n\n| Typ |", who);
        for (const CauseColumn& c : kColumns) {
            std::printf(" %s |", c.label);
        }
        std::printf("\n| --- |");
        for (std::size_t i = 0; i < sizeof kColumns / sizeof kColumns[0]; ++i) {
            std::printf(" --- |");
        }
        std::printf("\n");
    }
    for (const interop::TypeRow& t : interop::kTypes) {
        const std::set<int> used = causes(station ? t.station_causes : t.master_causes);
        if (used.empty()) {
            continue;
        }
        if (o.csv) {
            std::string list;
            for (const CauseColumn& c : kColumns) {
                if (column_used(used, c)) {
                    list += (list.empty() ? "" : " ") + std::string(c.label);
                }
            }
            std::printf("9.5 Cause (%s);<%u> %s;%s;%s;%s;\n", who, t.id, t.name, list.c_str(),
                        station ? "X" : "", station ? "" : "X");
            continue;
        }
        std::printf("| <%u> %s |", t.id, t.name);
        for (const CauseColumn& c : kColumns) {
            std::printf(" %s |", column_used(used, c) ? "X" : "");
        }
        std::printf("\n");
    }
}

void print_functions(const Writer& w) {
    const char* group = "";
    for (const interop::FunctionRow& f : interop::kFunctions) {
        if (std::strcmp(group, f.group) != 0) {
            if (*group == '\0') {
                w.heading(2, "9.6 Basic application functions");
                w.item_table("Funktion", "Punkt");
            }
            group = f.group;
        }
        w.item(std::string("9.6 ") + f.group, f.group, f.item, f.station, f.master, f.remark);
    }
}

void print_parameters(const Writer& w, const Options& o, const Parameters& p) {
    const iec104_link_config& l = p.config.link;
    w.heading(2, "9.6 Time-outs, k, w, port, redundancy, RFC 2200");
    if (!o.csv) {
        std::printf("| Parameter | Normwert | gewählt | Bemerkung |\n| --- | --- | --- | --- |\n");
    }
    const std::string s = "9.6 Parameters";
    w.value(s, "t0", "30 s", seconds(l.t0_ms),
            "Verbindungsaufbau; mit TLS einschließlich Handshake");
    w.value(s, "t1", "15 s", seconds(l.t1_ms), "Quittung gesendeter I- und U-Rahmen (act)");
    w.value(s, "t2", "10 s", seconds(l.t2_ms), "spätestens eigene Quittung; t2 < t1");
    w.value(s, "t3", "20 s", l.t3_ms == 0U ? "aus" : seconds(l.t3_ms),
            "Testrahmen nach Ruhe; Bereich 1 s … 48 h, 0 nur tolerant");
    w.value(s, "k", "12", std::to_string(l.k), "Bereich 1 … 32767");
    w.value(s, "w", "8", std::to_string(l.w), "Bereich 1 … 32767; w ≤ 2/3 k empfohlen");
    w.value(s, "Portnumber", "2404", std::to_string(p.config.port),
            p.tls ? "mit TLS üblich 19998 (IANA iec-104-sec)" : "frei wählbar");
    w.value(s, "Redundant connections (Anzahl Gruppen)", "–",
            p.config.group_count == 0U ? "keine (jede Verbindung eigene Gruppe)"
                                       : std::to_string(p.config.group_count),
            "Verbindungen je Gruppe unbegrenzt; eine aktiv; Puffer " +
                std::to_string(p.config.server.event_buffer_size) + " Ereignisse");
    w.value(s, "RFC 2200 suite", "Ethernet 802.3", "TCP/IP des Betriebssystems", "IPv4 und IPv6");
    w.value(s, "Maximum allowable delay of commands", "–",
            p.config.server.max_command_delay_ms == 0U
                ? "aus"
                : seconds(p.config.server.max_command_delay_ms),
            "nur Station; Befehle 58–64 und 107");

    w.heading(2, "Ergänzung: Sicherheit (IEC 62351-3)");
    if (!o.csv) {
        std::printf("| Parameter | Normwert | gewählt | Bemerkung |\n| --- | --- | --- | --- |\n");
    }
    const std::string t = "Ergänzung TLS";
    w.value(t, "TLS", "–", p.tls ? "an" : "aus", "OpenSSL 3; TLS 1.2 und 1.3");
    if (p.tls) {
        const iec104_tls_config& c = p.config.tls;
        w.value(t, "Versionen", "1.2 … 1.3",
                std::string(c.min_version == IEC104_TLS_1_3 ? "1.3" : "1.2") + " … " +
                    (c.max_version == IEC104_TLS_1_2 ? "1.2" : "1.3"),
                "");
        w.value(t, "Gegenseitige Authentifizierung", "ja", c.verify_peer != 0 ? "ja" : "nein",
                "X.509");
        w.value(t, "Sperrliste (CRL)", "–", c.crl_file[0] != '\0' ? "ja" : "nein", "");
        w.value(t, "Renegotiation (TLS 1.2)", "ja", c.renegotiation != 0 ? "ja" : "nein",
                "nur RFC 5746");
        w.value(t, "Schlüsselerneuerung", "–",
                c.renewal_interval_s == 0U ? "aus" : std::to_string(c.renewal_interval_s) + " s",
                "Key Update (1.3) bzw. Renegotiation (1.2)");
    }
}

void usage() {
    std::printf("iec104_interop_list - interoperability list (IEC 60870-5-104, clause 9)\n\n"
                "usage: iec104_interop_list [--role station|master|both] [--config FILE]\n"
                "                           [--format md|csv]\n\n"
                "  --role    station = controlled station (server), master = controlling\n"
                "            station (client), both (default)\n"
                "  --config  parameters from a configuration file (key = value)\n"
                "  --format  md (default, Markdown) or csv (';' separated, for spreadsheets)\n");
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const char* value = i + 1 < argc ? argv[i + 1] : "";
        if (arg == "--role") {
            const std::string r = value;
            ++i;
            if (r != "station" && r != "master" && r != "both") {
                std::fprintf(stderr, "invalid --role: %s\n", r.c_str());
                return 2;
            }
            o.station = r != "master";
            o.master = r != "station";
        } else if (arg == "--config") {
            o.config_file = value;
            ++i;
        } else if (arg == "--format") {
            const std::string f = value;
            ++i;
            if (f != "md" && f != "csv") {
                std::fprintf(stderr, "invalid --format: %s\n", f.c_str());
                return 2;
            }
            o.csv = f == "csv";
        } else if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else {
            std::fprintf(stderr, "unknown option %s (see --help)\n", arg.c_str());
            return 2;
        }
    }
    Parameters p;
    iec104_config_init(&p.config);
    if (!o.config_file.empty()) {
        int line = 0;
        const iec104_status st = iec104_config_load(o.config_file.c_str(), &p.config, &line);
        if (st != IEC104_OK) {
            std::fprintf(stderr, "%s: %s (line %d)\n", o.config_file.c_str(),
                         iec104_status_string(st), line);
            return 2;
        }
    }
    p.tls = p.config.use_tls != 0;

    const Writer w(o);
    print_header(w, o, p);
    print_system(w, o);
    print_application_layer(w, p);
    print_types(w, o);
    if (o.station) {
        print_matrix(w, o, true);
    }
    if (o.master) {
        print_matrix(w, o, false);
    }
    print_functions(w);
    print_parameters(w, o, p);
    return 0;
}
