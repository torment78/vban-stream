// SPDX-License-Identifier: GPL-2.0-or-later
#include "receiver.hpp"
#include "settings.hpp"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>
#include <QAction>
#include <QDialog>
#include <QPointer>
#include <algorithm>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-vban-audio", "en-US")
MODULE_EXPORT const char *obs_module_description(void) {
    return "VBAN receiver with eight audio inputs, two video inputs, Program mouse control and two monitor returns.";
}
MODULE_EXPORT const char *obs_module_name(void) { return "VBAN Stream"; }

namespace {
using namespace vban;
std::shared_ptr<Receiver> receiver;
std::shared_ptr<MonitorReturn> monitor_return;
std::shared_ptr<VideoReceiver> video_receiver;
std::shared_ptr<MouseReturn> mouse_return;
std::unique_ptr<ProgramMouse> program_mouse;
std::shared_ptr<ProgramOutput> program_output;
Config desired_config;
std::string startup_error;
QPointer<QDialog> settings_dialog;
QPointer<QAction> settings_action;
struct WeakSource {
    obs_weak_source_t *weak{};
    uint64_t last_end = 0; // Accessed only by the shared audio worker.
    explicit WeakSource(obs_source_t *s) : weak(obs_source_get_weak_source(s)) {}
    ~WeakSource() { obs_weak_source_release(weak); }
};
std::mutex sources_mutex;
std::vector<std::weak_ptr<WeakSource>> sources;
struct Source {
    std::shared_ptr<Receiver> manager;
    std::shared_ptr<Receiver::Consumer> consumer;
    std::shared_ptr<WeakSource> weak;
};
speaker_layout layout(uint8_t channels) {
    switch (channels) {
    case 1: return SPEAKERS_MONO;
    case 2: return SPEAKERS_STEREO;
    case 3: return SPEAKERS_2POINT1;
    case 4: return SPEAKERS_4POINT0;
    case 5: return SPEAKERS_4POINT1;
    case 6: return SPEAKERS_5POINT1;
    case 7: case 8: return SPEAKERS_7POINT1;
    default: return SPEAKERS_UNKNOWN;
    }
}
int selected(obs_data_t *settings) {
    const auto slot = obs_data_get_int(settings, "slot");
    return slot >= 0 && slot < int(slot_count) ? static_cast<int>(slot) : -1;
}
const char *source_name(void *) { return "VBAN Stream"; }
bool default_source_name(const std::string &name) {
    // Preserve automatic naming for unselected sources saved before the product rename.
    for (const std::string base : {std::string(source_name(nullptr)), std::string("VBAN Audio")}) {
        if (name == base) return true;
        const std::string prefix = base + " ";
        if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0 &&
            std::all_of(name.begin() + prefix.size(), name.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return true;
    }
    return false;
}
void update(void *data, obs_data_t *settings) {
    if (!data) return;
    auto *source = static_cast<Source *>(data);
    const int slot = selected(settings);
    const int previous = source->consumer->slot.exchange(slot);
    if (slot < 0) return;

    // Rename only after creation, when OBS has registered the source by name.
    auto *s = obs_weak_source_get_source(source->weak->weak);
    if (!s) return;
    auto *metadata = obs_source_get_private_settings(s);
    const std::string current = obs_source_get_name(s);
    const std::string last_auto = obs_data_get_string(metadata, "vban_auto_name");
    const bool automatic = last_auto.empty() ? default_source_name(current) : current == last_auto;
    if (automatic && (previous != slot || last_auto.empty())) {
        const auto label = source->manager->config().slots[slot].label;
        if (!label.empty()) {
            // OBS makes duplicate names unique and signals the mixer/source list.
            obs_source_set_name(s, label.c_str());
            obs_data_set_string(metadata, "vban_auto_name", obs_source_get_name(s));
        }
    }
    obs_data_release(metadata);
    obs_source_release(s);
}
void *create(obs_data_t *settings, obs_source_t *obs_source) {
    try {
        if (!receiver) return nullptr;
        auto source = std::make_unique<Source>();
        source->manager = receiver;
        source->weak = std::make_shared<WeakSource>(obs_source);
        source->consumer = receiver->subscribe(selected(settings), [weak = source->weak](const AudioBlock &block) {
            // A strong OBS reference guards output; no Source* crosses into the worker.
            obs_source_t *s = obs_weak_source_get_source(weak->weak);
            if (!s) return;
            obs_source_audio audio{};
            audio.data[0] = reinterpret_cast<const uint8_t *>(block.samples.data());
            audio.frames = block.frames;
            audio.speakers = layout(block.format.channels);
            audio.format = AUDIO_FORMAT_FLOAT;
            audio.samples_per_sec = block.format.rate;
            audio.timestamp = std::max(block.timestamp, weak->last_end);
            weak->last_end = audio.timestamp + frames_to_ns(block.frames, block.format.rate);
            obs_source_output_audio(s, &audio);
            obs_source_release(s);
        });
        { std::lock_guard lock(sources_mutex); sources.emplace_back(source->weak); }
        return source.release();
    } catch (const std::exception &e) {
        blog(LOG_ERROR, "[obs-vban-audio] Source creation failed: %s", e.what()); return nullptr;
    }
}
void destroy(void *data) {
    auto *source = static_cast<Source *>(data);
    if (!source) return;
    source->manager->unsubscribe(source->consumer);
    delete source;
}
void defaults(obs_data_t *settings) { obs_data_set_default_int(settings, "slot", -1); }
void refresh_properties() {
    std::vector<std::shared_ptr<WeakSource>> live;
    {
        std::lock_guard lock(sources_mutex);
        sources.erase(std::remove_if(sources.begin(), sources.end(), [&live](const auto &s) {
            auto weak = s.lock(); if (!weak) return true;
            live.push_back(std::move(weak)); return false;
        }), sources.end());
    }
    for (const auto &weak : live) {
        auto *s = obs_weak_source_get_source(weak->weak);
        if (s) { obs_source_update_properties(s); obs_source_release(s); }
    }
}
void show_settings() {
    if (!receiver) return;
    if (!settings_dialog) {
        settings_dialog = make_settings_dialog(static_cast<QWidget *>(obs_frontend_get_main_window()),
            receiver, desired_config, monitor_return, video_receiver, mouse_return, program_mouse.get(), program_output, [](const Config &cfg, std::string &error) {
                auto output_prepared = program_output->prepare(cfg.frame_output, error);
                if (!output_prepared) return false;
                auto mouse_prepared = mouse_return->prepare(cfg.mice, cfg.mouse_local_ip, error);
                if (!mouse_prepared) return false;
                auto prepared = monitor_return->prepare(cfg.returns, error, cfg.return_local_ip, cfg.return_buffer_ms);
                if (!prepared || !receiver->configure(cfg, error, write_config)) return false;
                program_output->activate(std::move(output_prepared));
                program_mouse->cancel(); mouse_return->activate(std::move(mouse_prepared));
                monitor_return->activate(std::move(prepared), cfg.return_buffer_ms);
                for (size_t i = 0; i < return_count; ++i) {
                    const auto s = monitor_return->status(i);
                    if (s.state != ReturnState::disabled)
                        blog(LOG_INFO, "[obs-vban-audio] Return %zu '%s': %s:%u -> %s:%u",
                            i + 1, s.stream_name.c_str(), s.source_ip.c_str(), s.source_port,
                            s.destination_ip.c_str(), s.destination_port);
                }
                desired_config = cfg; startup_error.clear(); refresh_properties();
                return true;
            });
    }
    settings_dialog->show(); settings_dialog->raise(); settings_dialog->activateWindow();
}
bool open_settings(obs_properties_t *, obs_property_t *, void *) { show_settings(); return false; }
bool change_selection(void *, obs_properties_t *props, obs_property_t *, obs_data_t *settings) {
    if (receiver) {
        auto text = status_text(*receiver, selected(settings));
        obs_property_set_description(obs_properties_get(props, "status"), text.c_str());
    }
    return true;
}
bool refresh_status(obs_properties_t *props, obs_property_t *, void *data) {
    if (receiver) {
        const int slot = data ? static_cast<Source *>(data)->consumer->slot.load() : -1;
        auto text = status_text(*receiver, slot);
        obs_property_set_description(obs_properties_get(props, "status"), text.c_str());
    }
    return true;
}
obs_properties_t *properties(void *data) {
    auto *props = obs_properties_create();
    auto *list = obs_properties_add_list(props, "slot", "VBAN Stream", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(list, "Select a stream", -1);
    const int current = data ? static_cast<Source *>(data)->consumer->slot.load() : -1;
    if (receiver) {
        const auto cfg = receiver->config();
        for (size_t i = 0; i < slot_count; ++i) {
            const auto &s = cfg.slots[i];
            if (!s.enabled && current != int(i)) continue;
            std::string title = "Stream " + std::to_string(i+1) + " — " +
                (s.label.empty() ? s.stream_name : s.label + " (" + s.stream_name + ")");
            if (!s.enabled) title += " [Disabled]";
            const size_t item = obs_property_list_add_int(list, title.c_str(), static_cast<long long>(i));
            if (!s.enabled) obs_property_list_item_disable(list, item, true);
        }
    }
    obs_property_set_modified_callback2(list, change_selection, nullptr);
    std::string text = receiver ? status_text(*receiver, current) : "Receiver is unavailable.";
    if (!startup_error.empty()) text += "<br>Saved settings could not be activated. Open VBAN Settings.";
    obs_properties_add_text(props, "status", text.c_str(), OBS_TEXT_INFO);
    obs_properties_add_button(props, "refresh", "Refresh status", refresh_status);
    obs_properties_add_button(props, "settings", "Open VBAN Settings", open_settings);
    return props;
}

int video_selected(obs_data_t *settings) {
    const auto slot = obs_data_get_int(settings, "slot");
    return slot >= 0 && slot < int(video_slot_count) ? static_cast<int>(slot) : -1;
}
struct VideoSource {
    std::shared_ptr<VideoReceiver> video;
    std::shared_ptr<Receiver> manager;
    std::shared_ptr<WeakSource> weak;
    obs_source_t *source = nullptr; // OBS owns the source throughout its callbacks.
    std::mutex mutex;
    int slot = -1;
    uint64_t serial = 0;
    int width = 1280, height = 720;
    bool showing_black = false;
};
const char *video_name(void *) { return "VBAN Video"; }
void *video_create(obs_data_t *settings, obs_source_t *source) {
    if (!video_receiver || !receiver) return nullptr;
    auto data = std::make_unique<VideoSource>();
    data->video = video_receiver; data->manager = receiver; data->source = source;
    data->slot = video_selected(settings); data->weak = std::make_shared<WeakSource>(source);
    { std::lock_guard lock(sources_mutex); sources.emplace_back(data->weak); }
    obs_source_set_async_unbuffered(source, true);
    return data.release();
}
void video_destroy(void *data) { delete static_cast<VideoSource *>(data); }
void video_update(void *opaque, obs_data_t *settings) {
    auto &data = *static_cast<VideoSource *>(opaque);
    const int slot = video_selected(settings);
    {
        std::lock_guard lock(data.mutex);
        if (data.slot != slot) {
            data.slot = slot; data.serial = 0; data.showing_black = false;
        }
    }
    if (slot < 0) return;
    auto *metadata = obs_source_get_private_settings(data.source);
    const std::string current = obs_source_get_name(data.source);
    const std::string previous = obs_data_get_string(metadata, "vban_video_auto_name");
    const std::string prefix = "VBAN Video ";
    const bool default_name = current == "VBAN Video" || (current.size() > prefix.size() &&
        current.compare(0, prefix.size(), prefix) == 0 &&
        std::all_of(current.begin() + prefix.size(), current.end(), [](char c) { return c >= '0' && c <= '9'; }));
    if ((!previous.empty() && current == previous) || (previous.empty() && default_name)) {
        const auto cfg = data.manager->config().videos[slot];
        const auto label = cfg.label.empty() ? cfg.stream_name : cfg.label;
        if (!label.empty()) {
            obs_source_set_name(data.source, label.c_str());
            obs_data_set_string(metadata, "vban_video_auto_name", obs_source_get_name(data.source));
        }
    }
    obs_data_release(metadata);
}
void video_tick(void *opaque, float) {
    auto &data = *static_cast<VideoSource *>(opaque);
    std::lock_guard lock(data.mutex);
    const auto snapshot = data.video->snapshot(data.slot);
    if (snapshot.image.isNull()) {
        if (data.showing_black) return;
        // Keep the source usable in a scene before reception and on signal loss.
        // Preserve its last dimensions, with an opaque black waiting picture.
        QImage black(data.width, data.height, QImage::Format_RGBA8888);
        if (black.isNull()) return;
        black.fill(Qt::black);
        obs_source_frame frame{};
        frame.data[0] = black.bits(); frame.linesize[0] = static_cast<uint32_t>(black.bytesPerLine());
        frame.width = static_cast<uint32_t>(data.width); frame.height = static_cast<uint32_t>(data.height);
        frame.format = VIDEO_FORMAT_RGBA; frame.full_range = true; frame.timestamp = os_gettime_ns();
        obs_source_output_video(data.source, &frame);
        data.serial = 0; data.showing_black = true;
        return;
    }
    if (snapshot.serial == data.serial) return;
    obs_source_frame frame{};
    frame.data[0] = const_cast<uint8_t *>(snapshot.image.constBits());
    frame.linesize[0] = static_cast<uint32_t>(snapshot.image.bytesPerLine());
    frame.width = static_cast<uint32_t>(snapshot.image.width());
    frame.height = static_cast<uint32_t>(snapshot.image.height());
    frame.format = VIDEO_FORMAT_RGBA; frame.full_range = true;
    frame.timestamp = snapshot.timestamp;
    obs_source_output_video(data.source, &frame);
    data.serial = snapshot.serial; data.showing_black = false;
    data.width = snapshot.image.width(); data.height = snapshot.image.height();
}
std::string video_status_text(int slot) {
    if (slot < 0 || !video_receiver) return "Choose a video configured in Tools > VBAN Stream Settings > Video inputs.";
    const auto s = video_receiver->status(static_cast<size_t>(slot));
    std::string text = !s.enabled ? "Disabled" : s.receiving ? "Receiving" : "Waiting";
    if (s.receiving) text += " - " + std::to_string(s.width) + " x " + std::to_string(s.height) + " - " + s.format;
    // Plain status only; network-supplied decode errors are shown as plain text in the main settings.
    text += ". Decoded frames: " + std::to_string(s.decoded) + "; incomplete: " +
        std::to_string(s.packets.incomplete) + "; decode errors: " + std::to_string(s.decode_errors) + ".";
    return text;
}
bool video_selection_changed(void *, obs_properties_t *props, obs_property_t *, obs_data_t *settings) {
    obs_property_set_description(obs_properties_get(props, "status"), video_status_text(video_selected(settings)).c_str());
    return true;
}
bool video_refresh(obs_properties_t *props, obs_property_t *, void *opaque) {
    int slot = -1;
    if (opaque) { auto &data = *static_cast<VideoSource *>(opaque); std::lock_guard lock(data.mutex); slot = data.slot; }
    obs_property_set_description(obs_properties_get(props, "status"), video_status_text(slot).c_str());
    return true;
}
obs_properties_t *video_properties(void *opaque) {
    auto *props = obs_properties_create();
    auto *list = obs_properties_add_list(props, "slot", "VBAN video input", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(list, "Select a video", -1);
    int current = -1;
    if (opaque) { auto &data = *static_cast<VideoSource *>(opaque); std::lock_guard lock(data.mutex); current = data.slot; }
    if (receiver) {
        const auto cfg = receiver->config();
        for (size_t i = 0; i < video_slot_count; ++i) {
            const auto &v = cfg.videos[i];
            if (!v.enabled && current != int(i)) continue;
            auto title = "Video " + std::to_string(i + 1) + " - " +
                (v.label.empty() ? v.stream_name : v.label + " (" + v.stream_name + ")");
            if (!v.enabled) title += " [Disabled]";
            const auto entry = obs_property_list_add_int(list, title.c_str(), static_cast<long long>(i));
            if (!v.enabled) obs_property_list_item_disable(list, entry, true);
        }
    }
    obs_property_set_modified_callback2(list, video_selection_changed, nullptr);
    obs_properties_add_text(props, "status", video_status_text(current).c_str(), OBS_TEXT_INFO);
    obs_properties_add_button(props, "refresh", "Refresh status", video_refresh);
    obs_properties_add_button(props, "settings", "Open VBAN Settings", open_settings);
    return props;
}

void close_ui() {
    delete settings_dialog.data();
    delete settings_action.data();
}
void frontend_event(obs_frontend_event event, void *) {
    if (program_mouse && (event == OBS_FRONTEND_EVENT_SCENE_CHANGED || event == OBS_FRONTEND_EVENT_TRANSITION_CHANGED ||
        event == OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED || event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING)) program_mouse->cancel();
    if (event == OBS_FRONTEND_EVENT_EXIT) {
        program_mouse.reset();
        if (program_output) program_output->shutdown();
        close_ui();
        if (monitor_return) monitor_return->shutdown();
        if (video_receiver) video_receiver->shutdown();
    }
}
}

bool obs_module_load(void) {
    try {
        video_receiver = std::make_shared<VideoReceiver>([] { return os_gettime_ns(); });
        receiver = std::make_shared<vban::Receiver>([] { return os_gettime_ns(); },
            [](bool warning, const std::string &message) {
                blog(warning ? LOG_WARNING : LOG_INFO, "[obs-vban-audio] %s", message.c_str());
            }, video_receiver);
        desired_config = vban::read_config(startup_error);
        if (startup_error.empty()) receiver->configure(desired_config, startup_error);
        if (!startup_error.empty()) blog(LOG_WARNING, "[obs-vban-audio] %s", startup_error.c_str());
        program_output = std::make_shared<ProgramOutput>();
        std::string output_error;
        auto prepared_output = program_output->prepare(desired_config.frame_output, output_error);
        if (prepared_output) program_output->activate(std::move(prepared_output));
        else blog(LOG_WARNING, "[obs-vban-audio] Program output: %s", output_error.c_str());
        mouse_return = std::make_shared<MouseReturn>();
        std::string mouse_error;
        auto prepared_mouse = mouse_return->prepare(desired_config.mice, desired_config.mouse_local_ip, mouse_error);
        if (prepared_mouse) mouse_return->activate(std::move(prepared_mouse));
        else blog(LOG_WARNING, "[obs-vban-audio] Mouse return: %s", mouse_error.c_str());
        program_mouse = std::make_unique<ProgramMouse>(mouse_return, video_receiver);
        monitor_return = std::make_shared<MonitorReturn>();
        std::string return_error;
        auto prepared_returns = monitor_return->prepare(desired_config.returns, return_error, desired_config.return_local_ip, desired_config.return_buffer_ms);
        if (prepared_returns) {
            monitor_return->activate(std::move(prepared_returns), desired_config.return_buffer_ms);
            for (size_t i = 0; i < return_count; ++i) {
                const auto s = monitor_return->status(i);
                if (s.state != ReturnState::disabled)
                    blog(LOG_INFO, "[obs-vban-audio] Return %zu '%s': %s:%u -> %s:%u",
                        i + 1, s.stream_name.c_str(), s.source_ip.c_str(), s.source_port,
                        s.destination_ip.c_str(), s.destination_port);
            }
        }
        else blog(LOG_WARNING, "[obs-vban-audio] Returns: %s", return_error.c_str());
        monitor_return->start();
        obs_source_info info{};
        info.id = "vban_audio_input";
        info.type = OBS_SOURCE_TYPE_INPUT;
        info.output_flags = OBS_SOURCE_AUDIO;
        info.get_name = source_name; info.create = create; info.destroy = destroy;
        info.get_defaults = defaults; info.get_properties = properties; info.update = update;
        obs_register_source(&info);
        obs_source_info video_info{};
        video_info.id = "vban_video_input"; video_info.type = OBS_SOURCE_TYPE_INPUT;
        video_info.output_flags = OBS_SOURCE_ASYNC_VIDEO;
        video_info.get_name = video_name; video_info.create = video_create; video_info.destroy = video_destroy;
        video_info.get_defaults = defaults; video_info.get_properties = video_properties;
        video_info.update = video_update; video_info.video_tick = video_tick;
        obs_register_source(&video_info);
        settings_action = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction("VBAN Stream Settings"));
        QObject::connect(settings_action, &QAction::triggered, settings_action, [] { show_settings(); });
        obs_frontend_add_event_callback(frontend_event, nullptr);
        blog(LOG_INFO, "[obs-vban-audio] VBAN Stream version %s", VBAN_PLUGIN_VERSION);
        return true;
    } catch (const std::exception &e) {
        blog(LOG_ERROR, "[obs-vban-audio] Load failed: %s", e.what());
        close_ui(); program_mouse.reset(); mouse_return.reset(); program_output.reset(); monitor_return.reset(); receiver.reset(); video_receiver.reset(); return false;
    }
}
void obs_module_unload(void) {
    obs_frontend_remove_event_callback(frontend_event, nullptr);
    close_ui();
    program_mouse.reset(); mouse_return.reset(); program_output.reset();
    if (monitor_return) monitor_return->shutdown();
    monitor_return.reset();
    if (receiver) receiver->shutdown();
    receiver.reset();
    if (video_receiver) video_receiver->shutdown();
    video_receiver.reset();
    blog(LOG_INFO, "[obs-vban-audio] Unloaded");
}
