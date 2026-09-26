// SPDX-License-Identifier: GPL-2.0-or-later
#include "socket-platform.hpp"
#ifdef _WIN32
#include <iphlpapi.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#endif
#include "vban-transmitter.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace vban {
const char *return_state_name(ReturnState state) {
    switch (state) {
    case ReturnState::disabled: return "Disabled";
    case ReturnState::ready: return "Ready";
    case ReturnState::sending: return "Sending";
    case ReturnState::stalled: return "Stalled";
    case ReturnState::error: return "Error";
    }
    return "Error";
}
size_t encode_stereo_pcm(uint8_t *out, uint32_t rate, const std::string &name,
                         uint32_t sequence, const float *samples, size_t frames, int pcm_bits) {
    if (!out || !samples || (pcm_bits != 16 && pcm_bits != 24)) return 0;
    const size_t bytes_per_sample = static_cast<size_t>(pcm_bits / 8);
    const auto found = std::find(sample_rates.begin(), sample_rates.end(), rate);
    if (found == sample_rates.end() || !valid_stream_name(name) || !frames ||
        frames > 256 || header_size + frames * 2 * bytes_per_sample > max_datagram) return 0;
    std::memset(out, 0, header_size);
    std::memcpy(out, "VBAN", 4);
    out[4] = static_cast<uint8_t>(found - sample_rates.begin());
    out[5] = static_cast<uint8_t>(frames - 1); out[6] = 1;
    out[7] = pcm_bits == 16 ? 1 : 2; // VBAN signed little-endian PCM16 / PCM24.
    std::memcpy(out + 8, name.data(), name.size());
    for (size_t i = 0; i < 4; ++i) out[24+i] = static_cast<uint8_t>(sequence >> (i*8));
    const double scale = pcm_bits == 16 ? 32768.0 : 8388608.0;
    for (size_t i = 0; i < frames * 2; ++i) {
        const double value = std::isfinite(samples[i]) ? samples[i] : 0.0;
        const auto pcm = static_cast<int32_t>(std::clamp(std::round(value * scale), -scale, scale - 1.0));
        const auto bits = static_cast<uint32_t>(pcm);
        for (size_t b = 0; b < bytes_per_sample; ++b)
            out[header_size+i*bytes_per_sample+b] = static_cast<uint8_t>(bits >> (b*8));
    }
    return header_size + frames * 2 * bytes_per_sample;
}
std::vector<LocalIPv4> local_ipv4_addresses(std::string &error) {
    std::vector<LocalIPv4> result;
    #ifdef _WIN32
    ULONG size = 15000;
    std::vector<uint8_t> buffer(size);
    ULONG rc = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3 && rc == ERROR_BUFFER_OVERFLOW; ++attempt) {
        buffer.resize(size);
        rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
            GAA_FLAG_SKIP_DNS_SERVER, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data()), &size);
    }
    if (rc != NO_ERROR) {
        error = "Cannot enumerate local IPv4 adapters (Windows " + std::to_string(rc) + ").";
        return result;
    }
    for (auto *adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data()); adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || !adapter->IfIndex) continue;
        std::string label;
        if (adapter->FriendlyName) {
            const int length = WideCharToMultiByte(CP_UTF8, 0, adapter->FriendlyName, -1, nullptr, 0, nullptr, nullptr);
            if (length > 1) {
                label.resize(static_cast<size_t>(length));
                WideCharToMultiByte(CP_UTF8, 0, adapter->FriendlyName, -1, label.data(), length, nullptr, nullptr);
                label.pop_back();
            }
        }
        for (auto *entry = adapter->FirstUnicastAddress; entry; entry = entry->Next) {
            if (!entry->Address.lpSockaddr || entry->Address.lpSockaddr->sa_family != AF_INET ||
                entry->DadState != IpDadStatePreferred) continue;
            const auto *address = reinterpret_cast<const sockaddr_in *>(entry->Address.lpSockaddr);
            char ip[INET_ADDRSTRLEN]{};
            if (vban::net::format(AF_INET, &address->sin_addr, ip, sizeof(ip)))
                result.push_back({ip, label, adapter->IfIndex});
        }
    }
    #else
    ifaddrs *addresses = nullptr;
    if (getifaddrs(&addresses) != 0) {
        error = "Cannot enumerate local IPv4 adapters (Socket " + std::to_string(net::error()) + ").";
        return result;
    }
    struct AddressList { ifaddrs *value; ~AddressList() { freeifaddrs(value); } } owner{addresses};
    for (auto *entry=addresses; entry; entry=entry->ifa_next) {
        if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET || !(entry->ifa_flags & IFF_UP)) continue;
        const auto index=if_nametoindex(entry->ifa_name);
        if (!index) continue;
        const auto *address=reinterpret_cast<const sockaddr_in *>(entry->ifa_addr);
        char ip[INET_ADDRSTRLEN]{};
        if (net::format(AF_INET,&address->sin_addr,ip,sizeof(ip)))
            result.push_back({ip,entry->ifa_name,index});
    }
    #endif
    error.clear();
    return result;
}
struct Transmitter::Routing {
    struct Destination {
        vban::net::Socket socket = vban::net::invalid;
        sockaddr_in address{}, local{};
        ReturnConfig config;
        ~Destination() { if (socket != vban::net::invalid) vban::net::close(socket); }
    };
    std::array<Destination, return_count> destinations;
};
Transmitter::Transmitter() {
    if (vban::net::startup()) throw std::runtime_error("Return Socket initialization failed.");
}
Transmitter::~Transmitter() { routing_.reset(); vban::net::cleanup(); }
Transmitter::Prepared Transmitter::prepare(const ReturnConfigs &cfg, std::string &error, const std::string &local_ip) {
    LocalIPv4 selected;
    if (!local_ip.empty() && std::any_of(cfg.begin(), cfg.end(), [](const auto &r) { return r.enabled; })) {
        const auto adapters = local_ipv4_addresses(error);
        if (!error.empty()) return {};
        const auto found = std::find_if(adapters.begin(), adapters.end(),
            [&](const auto &adapter) { return adapter.address == local_ip; });
        if (found == adapters.end()) {
            error = "Local sender IPv4 " + local_ip + " is not available on an active adapter on this PC. Select an available address.";
            return {};
        }
        selected = *found;
    }
    auto next = std::make_shared<Routing>();
    for (size_t i = 0; i < return_count; ++i) {
        auto &d = next->destinations[i]; d.config = cfg[i];
        const auto prefix = "Return " + std::to_string(i+1) + ": ";
        if (d.config.pcm_bits != 16 && d.config.pcm_bits != 24) {
            error = prefix + "Choose PCM 16-bit or PCM 24-bit."; return {};
        }
        if (!d.config.enabled) continue;
        d.address.sin_family = AF_INET;
        d.address.sin_port = htons(d.config.destination_port);
        if (vban::net::parse(AF_INET, d.config.destination_ip.c_str(), &d.address.sin_addr) != 1 ||
            d.address.sin_addr.s_addr == INADDR_ANY) {
            error = prefix + "Enter a valid destination IPv4 address."; return {};
        }
        if (!d.config.destination_port) { error = prefix + "Port must be 1 to 65535."; return {}; }
        if (!valid_stream_name(d.config.stream_name)) {
            error = prefix + "Stream name must contain 1 to 16 printable ASCII characters."; return {};
        }
        d.socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        int broadcast = 1;
        if (d.socket == vban::net::invalid || !net::nonblocking(d.socket) ||
            setsockopt(d.socket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char *>(&broadcast),
                       sizeof(broadcast)) == vban::net::failure) {
            error = prefix + "Cannot create return socket (Socket " + std::to_string(vban::net::error()) + ").";
            return {};
        }
        if (!local_ip.empty()) {
            sockaddr_in local{}; local.sin_family = AF_INET; // Port zero keeps TX independent of RX.
            if (vban::net::parse(AF_INET, local_ip.c_str(), &local.sin_addr) != 1 ||
                !net::select_interface(d.socket, selected.interface_index) ||
                bind(d.socket, reinterpret_cast<const sockaddr *>(&local), sizeof(local))) {
                error = prefix + "Cannot use local sender " + local_ip + " (Socket " + std::to_string(vban::net::error()) + ").";
                return {};
            }
        }
        // UDP connect selects a route/local endpoint; it performs no handshake and sends no audio.
        if (connect(d.socket, reinterpret_cast<const sockaddr *>(&d.address), sizeof(d.address))) {
            error = prefix + "Cannot select destination route (Socket " + std::to_string(vban::net::error()) + ").";
            return {};
        }
        vban::net::Length local_size = sizeof(d.local);
        if (getsockname(d.socket, reinterpret_cast<sockaddr *>(&d.local), &local_size)) {
            error = prefix + "Cannot inspect the sender socket (Socket " + std::to_string(vban::net::error()) + ").";
            return {};
        }
        // A receiver can be offline without ICMP port-unreachable poisoning later sends.
        if (!net::ignore_port_unreachable(d.socket)) {
            error = prefix + "Cannot configure UDP socket error handling (Socket " + std::to_string(vban::net::error()) + ").";
            return {};
        }
    }
    error.clear(); return next;
}
void Transmitter::activate(Prepared next) {
    std::lock_guard lock(mutex_);
    routing_ = std::move(next);
    for (size_t i = 0; i < return_count; ++i) {
        status_[i] = {}; last_send_[i] = {};
        if (routing_) status_[i].pcm_bits = routing_->destinations[i].config.pcm_bits;
        if (routing_ && routing_->destinations[i].config.enabled) {
            const auto &d = routing_->destinations[i];
            auto &s = status_[i]; s.state = ReturnState::ready;
            char ip[INET_ADDRSTRLEN]{};
            vban::net::format(AF_INET, &d.local.sin_addr, ip, sizeof(ip));
            s.source_ip = ip; s.source_port = ntohs(d.local.sin_port);
            s.destination_ip = d.config.destination_ip; s.destination_port = d.config.destination_port;
            s.stream_name = d.config.stream_name;
        }
    }
}
bool Transmitter::enabled() const {
    std::lock_guard lock(mutex_);
    if (!routing_) return false;
    for (const auto &d : routing_->destinations) if (d.config.enabled) return true;
    return false;
}
void Transmitter::send(const float *stereo, size_t frames, uint32_t rate) {
    std::lock_guard lock(mutex_);
    if (!routing_) return;
    // Encode once per selected format; destinations using the same format share PCM.
    std::array<std::array<uint8_t, max_datagram>, 2> packets{};
    std::array<size_t, 2> sizes{};
    bool silence = true;
    uint64_t clipped = 0, nonfinite = 0;
    for (size_t f = 0; f < frames*2; ++f) {
        if (!std::isfinite(stereo[f])) { ++nonfinite; continue; }
        if (std::fabs(stereo[f]) > 0.0000001f) silence = false;
        if (stereo[f] > 1.0f || stereo[f] < -1.0f) ++clipped;
    }
    for (size_t i = 0; i < return_count; ++i) {
        const auto &d = routing_->destinations[i];
        if (!d.config.enabled) continue;
        auto &state = status_[i];
        const auto format = d.config.pcm_bits == 16 ? 0u : 1u;
        auto &packet = packets[format];
        auto &size = sizes[format];
        if (!size) size = encode_stereo_pcm(packet.data(), rate, "MONITOR", 0, stereo, frames, d.config.pcm_bits);
        if (!size) { state.state = ReturnState::error; state.detail = "Unsupported return sample rate or audio block size."; continue; }
        state.clipped_samples += clipped;
        state.nonfinite_samples += nonfinite;
        std::memset(packet.data()+8, 0, 16);
        std::memcpy(packet.data()+8, d.config.stream_name.data(), d.config.stream_name.size());
        const auto sequence = sequences_[i]++;
        for (size_t b = 0; b < 4; ++b) packet[24+b] = static_cast<uint8_t>(sequence >> (b*8));
        const auto sent = sendto(d.socket, reinterpret_cast<const char *>(packet.data()), static_cast<int>(size), 0,
                                 reinterpret_cast<const sockaddr *>(&d.address), sizeof(d.address));
        if (sent != static_cast<int>(size)) {
            ++state.errors; state.state = ReturnState::error;
            state.detail = "UDP send failed (Socket " + std::to_string(vban::net::error()) + ").";
        } else {
            const auto now = std::chrono::steady_clock::now();
            if (last_send_[i].time_since_epoch().count()) {
                const double gap = std::chrono::duration<double, std::milli>(now - last_send_[i]).count();
                state.max_send_gap_ms = std::max(state.max_send_gap_ms, gap);
                if (gap > 20.0) ++state.send_gaps;
            }
            last_send_[i] = now;
            ++state.packets; state.state = ReturnState::sending; state.silence = silence; state.detail.clear();
        }
    }
}
void Transmitter::fail(const std::string &error) {
    std::lock_guard lock(mutex_);
    for (size_t i = 0; i < return_count; ++i)
        if (routing_ && routing_->destinations[i].config.enabled) {
            status_[i].state = ReturnState::error; status_[i].detail = error;
        }
}
ReturnStatus Transmitter::status(size_t i) const {
    std::lock_guard lock(mutex_);
    if (i >= return_count) return {};
    auto result = status_[i];
    if (last_send_[i].time_since_epoch().count()) {
        result.last_send_age_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - last_send_[i]).count();
        if (result.state == ReturnState::sending && result.last_send_age_ms > 1000.0) {
            result.state = ReturnState::stalled; result.detail = "No UDP packet sent for more than one second.";
        }
    }
    return result;
}
}
