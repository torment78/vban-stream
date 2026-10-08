// SPDX-License-Identifier: GPL-2.0-or-later
#include "socket-platform.hpp"
#include "mouse-return.hpp"
#include "vban-transmitter.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace vban {
std::vector<uint8_t> mouse_packet(const std::string &name, uint32_t sequence, MouseAction action, int x, int y) {
    if (!valid_stream_name(name) || x < 0 || y < 0 || x >= 4096 || y >= 4096) return {};
    const char *command = nullptr;
    switch (action) {
    case MouseAction::move: command = "MOUSEMOVE"; break;
    case MouseAction::left_down: command = "LBUTTONDOWN"; break;
    case MouseAction::left_up: command = "LBUTTONUP"; break;
    case MouseAction::right_down: command = "RBUTTONDOWN"; break;
    case MouseAction::right_up: command = "RBUTTONUP"; break;
    }
    if (!command) return {};
    const auto text = std::string("System.Mouse=(") + command + ", " + std::to_string(x) + ", " + std::to_string(y) + ");";
    std::vector<uint8_t> packet(header_size + text.size());
    std::memcpy(packet.data(), "VBAN", 4);
    packet[4] = 0x52; packet[7] = 0x10; // 256000 bps, UTF-8 VBAN-TEXT, subchannel zero.
    std::memcpy(packet.data() + 8, name.data(), name.size());
    for (size_t i = 0; i < 4; ++i) packet[24+i] = uint8_t(sequence >> (8*i));
    std::memcpy(packet.data() + header_size, text.data(), text.size());
    return packet;
}
struct MouseReturn::Routing {
    struct Destination {
        net::Socket socket = net::invalid;
        MouseConfig config;
        std::string local_ip;
        ~Destination() { if (socket != net::invalid) net::close(socket); }
    };
    std::array<Destination, video_slot_count> destinations;
};
MouseReturn::MouseReturn() { if (net::startup()) throw std::runtime_error("Mouse return socket initialization failed."); }
MouseReturn::~MouseReturn() { release(); routing_.reset(); net::cleanup(); }
MouseReturn::Prepared MouseReturn::prepare(const MouseConfigs &config, const std::string &local_ip, std::string &error) {
    error.clear();
    LocalIPv4 adapter;
    if (!local_ip.empty() && std::any_of(config.begin(), config.end(), [](const auto &s) { return s.enabled; })) {
        const auto adapters = local_ipv4_addresses(error);
        if (!error.empty()) return {};
        const auto found = std::find_if(adapters.begin(), adapters.end(), [&](const auto &a) { return a.address == local_ip; });
        if (found == adapters.end()) { error = "Mouse return: the selected local IPv4 address is unavailable."; return {}; }
        adapter = *found;
    }
    auto next = std::make_shared<Routing>();
    for (size_t i = 0; i < video_slot_count; ++i) {
        auto &d = next->destinations[i]; d.config = config[i];
        if (!d.config.enabled) continue;
        const auto prefix = "Mouse return " + std::to_string(i+1) + ": ";
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(d.config.port);
        if (net::parse(AF_INET, d.config.destination_ip.c_str(), &address.sin_addr) != 1 ||
            address.sin_addr.s_addr == INADDR_ANY || address.sin_addr.s_addr == INADDR_BROADCAST ||
            (ntohl(address.sin_addr.s_addr) & 0xf0000000U) == 0xe0000000U) {
            error = prefix + "Enter the destination computer's unicast IPv4 address."; return {};
        }
        if (!d.config.port || !valid_stream_name(d.config.stream_name)) {
            error = prefix + "Use a port from 1 to 65535 and a stream name of 1 to 16 printable ASCII characters."; return {};
        }
        for (size_t other = 0; other < i; ++other) {
            const auto &existing = config[other];
            if (existing.enabled && existing.destination_ip == d.config.destination_ip &&
                existing.port == d.config.port && existing.stream_name == d.config.stream_name) {
                error = prefix + "Use a different destination or command stream for each video input."; return {};
            }
        }
        d.socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (d.socket == net::invalid || !net::nonblocking(d.socket) || !net::ignore_port_unreachable(d.socket)) {
            error = prefix + "Cannot create mouse return socket."; return {};
        }
        if (!local_ip.empty()) {
            sockaddr_in local{}; local.sin_family = AF_INET;
            if (net::parse(AF_INET, local_ip.c_str(), &local.sin_addr) != 1 || !net::select_interface(d.socket, adapter.interface_index) ||
                bind(d.socket, reinterpret_cast<const sockaddr *>(&local), sizeof(local))) {
                error = prefix + "Cannot bind the selected local IPv4 address."; return {};
            }
        }
        if (connect(d.socket, reinterpret_cast<const sockaddr *>(&address), sizeof(address))) {
            error = prefix + "Cannot select the destination route."; return {};
        }
        sockaddr_in local{}; net::Length size = sizeof(local);
        if (!getsockname(d.socket, reinterpret_cast<sockaddr *>(&local), &size)) {
            char ip[INET_ADDRSTRLEN]{};
            if (net::format(AF_INET, &local.sin_addr, ip, sizeof(ip))) d.local_ip = ip;
        }
    }
    return next;
}
void MouseReturn::activate(Prepared next) {
    std::lock_guard lock(mutex_);
    release_locked(); // Release to the OLD destination before changing sockets.
    routing_ = std::move(next);
    for (size_t i = 0; i < video_slot_count; ++i) {
        status_[i] = {};
        if (routing_) {
            status_[i].enabled = routing_->destinations[i].config.enabled;
            status_[i].local_ip = routing_->destinations[i].local_ip;
        }
    }
}
bool MouseReturn::send(size_t slot, MouseAction action, int x, int y) {
    if (slot >= video_slot_count || !routing_) return false;
    auto &d = routing_->destinations[slot]; auto &s = status_[slot];
    if (!d.config.enabled || d.socket == net::invalid) return false;
    const auto packet = mouse_packet(d.config.stream_name, sequences_[slot]++, action, x, y);
    if (packet.empty()) return false;
    if (::send(d.socket, reinterpret_cast<const char *>(packet.data()), static_cast<int>(packet.size()), 0) != static_cast<int>(packet.size())) {
        ++s.errors; s.error = "UDP send failed (socket " + std::to_string(net::error()) + ")."; return false;
    }
    ++s.packets; s.error.clear(); return true;
}
bool MouseReturn::press(size_t slot, bool right, int x, int y) {
    std::lock_guard lock(mutex_);
    release_locked();
    if (!send(slot, MouseAction::move, x, y)) return false;
    held_slot_ = static_cast<int>(slot); right_ = right; x_ = x; y_ = y;
    status_[slot].dragging = true;
    if (!send(slot, right ? MouseAction::right_down : MouseAction::left_down, x, y)) { release_locked(); return false; }
    return true;
}
bool MouseReturn::move(size_t slot, int x, int y) {
    std::lock_guard lock(mutex_);
    if (held_slot_ >= 0 && static_cast<size_t>(held_slot_) != slot) return false;
    if (!send(slot, MouseAction::move, x, y)) { release_locked(); return false; }
    x_ = x; y_ = y; return true;
}
void MouseReturn::release_locked() {
    if (held_slot_ < 0) return;
    const auto slot = static_cast<size_t>(held_slot_);
    // Releases are idempotent. Repetition reduces the risk of a lost UDP button-up.
    for (int i = 0; i < 3; ++i) send(slot, right_ ? MouseAction::right_up : MouseAction::left_up, x_, y_);
    status_[slot].dragging = false; held_slot_ = -1;
}
void MouseReturn::release() { std::lock_guard lock(mutex_); release_locked(); }
bool MouseReturn::dragging() const { std::lock_guard lock(mutex_); return held_slot_ >= 0; }
MouseStatus MouseReturn::status(size_t slot) const { std::lock_guard lock(mutex_); return slot < video_slot_count ? status_[slot] : MouseStatus{}; }
}
