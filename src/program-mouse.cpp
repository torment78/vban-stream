// SPDX-License-Identifier: GPL-2.0-or-later
#include "program-mouse.hpp"
#include <obs-frontend-api.h>
#include <graphics/matrix4.h>
#include <util/platform.h>
#include <QApplication>
#include <QGuiApplication>
#include <QLayout>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace vban {
namespace {
// Qt maps the physical Control key to MetaModifier on macOS.
#ifdef __APPLE__
constexpr auto control_modifier = Qt::MetaModifier;
constexpr auto control_key = Qt::Key_Meta;
#else
constexpr auto control_modifier = Qt::ControlModifier;
constexpr auto control_key = Qt::Key_Control;
#endif
struct Search { float x, y; int depth; MouseHit hit; std::vector<int64_t> path; };
bool visit(obs_scene_t *, obs_sceneitem_t *item, void *opaque) {
    auto &search = *static_cast<Search *>(opaque);
    if (!obs_sceneitem_visible(item)) return true;
    auto *source = obs_sceneitem_get_source(item);
    const auto width = obs_source_get_width(source), height = obs_source_get_height(source);
    if (!width || !height) return true;
    matrix4 draw{}, inverse{}; obs_sceneitem_get_draw_transform(item, &draw);
    if (!matrix4_inv(&inverse, &draw)) return true;
    vec3 point{}; vec3_set(&point, search.x, search.y, 0); vec3_transform(&point, &point, &inverse);
    obs_sceneitem_crop crop{}; obs_sceneitem_get_crop(item, &crop);
    const float visible_width = float(width) - crop.left - crop.right;
    const float visible_height = float(height) - crop.top - crop.bottom;
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0 || point.y < 0 ||
        point.x >= visible_width || point.y >= visible_height) return true;
    // Later items cover earlier ones. Never click through an unknown foreground source.
    search.hit = {};
    if ((obs_sceneitem_get_bounds_type(item) != OBS_BOUNDS_NONE && obs_sceneitem_get_bounds_crop(item)) || obs_source_filter_count(source)) return true;
    point.x += float(crop.left); point.y += float(crop.top);
    auto path = search.path; path.push_back(obs_sceneitem_get_id(item));
    auto *nested = obs_sceneitem_is_group(item) ? obs_sceneitem_group_get_scene(item) : obs_scene_from_source(source);
    if (nested && search.depth < 16) {
        Search child{point.x, point.y, search.depth+1, {}, std::move(path)};
        obs_scene_enum_items(nested, visit, &child); search.hit = std::move(child.hit); return true;
    }
    const auto *id = obs_source_get_unversioned_id(source);
    if (!id || std::strcmp(id, "vban_video_input")) return true;
    auto *settings = obs_source_get_settings(source);
    const auto slot = obs_data_has_user_value(settings, "slot") ? obs_data_get_int(settings, "slot") : -1;
    obs_data_release(settings);
    if (slot < 0 || slot >= int(video_slot_count)) return true;
    search.hit = {int(slot), int(std::floor(point.x)), int(std::floor(point.y)), int(width), int(height), std::move(path)};
    return true;
}
}
MouseHit mouse_hit(obs_source_t *source, float x, float y) {
    if (!source) return {};
    auto *scene = obs_scene_from_source(source);
    if (!scene) return {};
    Search search{x, y, 0, {}, {}};
    obs_scene_enum_items(scene, visit, &search);
    return search.hit;
}
bool program_coordinates(QSize pixels, qreal ratio, QPointF mouse, uint32_t width, uint32_t height, QPointF &canvas) {
    // OBS 31/32's Program display uses a 10-physical-pixel margin, independently of DPI.
    const int cx = pixels.width()-20, cy = pixels.height()-20;
    if (cx <= 0 || cy <= 0 || !width || !height || ratio <= 0) return false;
    const double aspect = double(width)/height;
    float scale; int newcx, newcy;
    if (double(cx)/cy > aspect) { scale = float(cy)/float(height); newcx = int(double(cy)*aspect); newcy = cy; }
    else { scale = float(cx)/float(width); newcx = cx; newcy = int(float(cx)/aspect); }
    const int offset_x = cx/2-newcx/2+10, offset_y = cy/2-newcy/2+10;
    canvas = QPointF((mouse.x()*ratio-offset_x)/scale, (mouse.y()*ratio-offset_y)/scale);
    return canvas.x() >= 0 && canvas.y() >= 0 && canvas.x() < width && canvas.y() < height;
}
ProgramMouse::ProgramMouse(std::shared_ptr<MouseReturn> mouse, std::shared_ptr<VideoReceiver> video)
    : mouse_(std::move(mouse)), video_(std::move(video)) {
    if (qApp) qApp->installEventFilter(this);
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        refresh_display();
        if (held_scene_) {
            MouseHit hit; obs_source_t *scene = nullptr;
            const bool keep = mouse_->dragging() && (QGuiApplication::keyboardModifiers() & control_modifier) &&
                locate(last_position_, hit, scene) && same_target(hit, scene) && live(hit);
            if (scene) obs_source_release(scene);
            if (!keep) cancel();
        }
    });
    timer->start(50); refresh_display();
}
ProgramMouse::~ProgramMouse() {
    if (qApp) qApp->removeEventFilter(this);
    cancel(); if (display_) display_->setMouseTracking(old_tracking_);
}
void ProgramMouse::cancel() {
    mouse_->release();
    if (held_scene_) obs_source_release(held_scene_);
    held_scene_ = nullptr; held_ = {}; button_ = Qt::NoButton; last_move_ = 0;
}
void ProgramMouse::refresh_display() {
    QWidget *found = nullptr;
    if (obs_frontend_preview_program_mode_active()) {
        auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
        auto *layout = main ? main->findChild<QLayout *>("previewLayout") : nullptr;
        unsigned matches = 0;
        if (layout) for (int i = 0; i < layout->count(); ++i) {
            auto *container = layout->itemAt(i)->widget();
            if (!container) continue;
            auto widgets = container->findChildren<QWidget *>(); widgets.push_back(container);
            for (auto *widget : widgets) {
                // The editing preview is OBSBasicPreview, never this exact class.
                if (!std::strcmp(widget->metaObject()->className(), "OBSQTDisplay")) { found = widget; ++matches; }
            }
        }
        if (matches != 1) found = nullptr; // Fail closed if an OBS update changes the layout.
    }
    if (display_ == found) return;
    cancel();
    if (display_) display_->setMouseTracking(old_tracking_);
    display_ = found;
    if (display_) { old_tracking_ = display_->hasMouseTracking(); display_->setMouseTracking(true); }
}
std::string ProgramMouse::availability() const {
    if (!obs_frontend_preview_program_mode_active()) return "Turn on OBS Studio Mode to use mouse return in Program.";
    return display_ ? "Hold Ctrl and click or drag inside a live VBAN picture in Program." : "Program view unavailable. Mouse return is inactive.";
}
obs_source_t *ProgramMouse::program_scene() const {
    auto *output = obs_get_output_source(0);
    if (!output) return nullptr;
    if (obs_source_get_type(output) == OBS_SOURCE_TYPE_TRANSITION) {
        const auto t = obs_transition_get_time(output);
        auto *active = (t > 0.0f && t < 1.0f) ? nullptr : obs_transition_get_active_source(output);
        obs_source_release(output); output = active;
    }
    return output;
}
bool ProgramMouse::locate(QPointF position, MouseHit &hit, obs_source_t *&scene) const {
    if (!display_ || !display_->isVisible() || !obs_frontend_preview_program_mode_active()) return false;
    obs_video_info info{}; if (!obs_get_video_info(&info)) return false;
    QPointF canvas;
    if (!program_coordinates(display_->size()*display_->devicePixelRatioF(), display_->devicePixelRatioF(), position,
                             info.base_width, info.base_height, canvas)) return false;
    scene = program_scene();
    hit = mouse_hit(scene, float(canvas.x()), float(canvas.y()));
    return hit.slot >= 0;
}
bool ProgramMouse::live(const MouseHit &hit) const {
    if (hit.slot < 0 || !mouse_->status(size_t(hit.slot)).enabled) return false;
    const auto status = video_->status(size_t(hit.slot));
    return status.receiving && status.width == hit.width && status.height == hit.height;
}
bool ProgramMouse::same_target(const MouseHit &hit, obs_source_t *scene) const {
    return scene == held_scene_ && hit.slot == held_.slot && hit.path == held_.path &&
        hit.width == held_.width && hit.height == held_.height;
}
bool ProgramMouse::eventFilter(QObject *object, QEvent *event) {
    const auto type = event->type();
    if (type == QEvent::ApplicationDeactivate || (type == QEvent::WindowDeactivate && held_scene_) ||
        ((type == QEvent::Hide || type == QEvent::Destroy || type == QEvent::Leave) && object == display_)) cancel();
    if (type == QEvent::KeyRelease || type == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape || (type == QEvent::KeyRelease && key->key() == control_key && !key->isAutoRepeat())) cancel();
    }
    if (type == QEvent::ContextMenu && object == display_ && suppress_context_menu_) {
        suppress_context_menu_ = false; return true;
    }
    const bool press = type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick;
    const bool release = type == QEvent::MouseButtonRelease;
    if (type != QEvent::MouseMove && !press && !release) return false;
    if (!qobject_cast<QWidget *>(object)) return false;
    auto *mouse_event = static_cast<QMouseEvent *>(event);
    if (press && object == display_) suppress_context_menu_ = false;
    if (held_scene_ && release && mouse_event->button() == button_) {
        MouseHit final_hit; obs_source_t *final_scene = nullptr;
        if (object == display_ && locate(mouse_event->position(), final_hit, final_scene) && same_target(final_hit, final_scene) && live(final_hit))
            mouse_->move(size_t(final_hit.slot), final_hit.x, final_hit.y);
        if (final_scene) obs_source_release(final_scene);
        cancel(); return object == display_;
    }
    if (object != display_) { if (press && held_scene_) cancel(); return false; }
    if (!(mouse_event->modifiers() & control_modifier)) { cancel(); return false; }
    if (release) return false;
    if (press && mouse_event->button() != Qt::LeftButton && mouse_event->button() != Qt::RightButton) return false;
    MouseHit hit; obs_source_t *scene = nullptr;
    const bool target = locate(mouse_event->position(), hit, scene) && live(hit);
    const bool was_dragging = held_scene_ != nullptr;
    if (!target || (held_scene_ && !same_target(hit, scene))) {
        cancel(); if (scene) obs_source_release(scene); return was_dragging;
    }
    last_position_ = mouse_event->position();
    if (press) {
        suppress_context_menu_ = mouse_event->button() == Qt::RightButton;
        cancel();
        if (mouse_->press(size_t(hit.slot), mouse_event->button() == Qt::RightButton, hit.x, hit.y)) {
            held_scene_ = scene; scene = nullptr; held_ = hit; button_ = mouse_event->button();
        }
    } else {
        const auto now = os_gettime_ns();
        if (now-last_move_ >= 8'000'000) { // Cap hover/drag traffic at 125 Hz.
            if (!mouse_->move(size_t(hit.slot), hit.x, hit.y)) cancel();
            last_move_ = now;
        }
    }
    if (scene) obs_source_release(scene);
    return true;
}
}
