// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mouse-return.hpp"
#include "video-receiver.hpp"
#include <obs.h>
#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QWidget>
#include <vector>
namespace vban {
struct MouseHit {
    int slot = -1, x = 0, y = 0, width = 0, height = 0;
    std::vector<int64_t> path;
};
MouseHit mouse_hit(obs_source_t *scene, float x, float y);
bool program_coordinates(QSize pixels, qreal ratio, QPointF mouse, uint32_t width, uint32_t height, QPointF &canvas);
class ProgramMouse final : public QObject {
public:
    ProgramMouse(std::shared_ptr<MouseReturn>, std::shared_ptr<VideoReceiver>);
    ~ProgramMouse() override;
    void cancel();
    std::string availability() const;
protected:
    bool eventFilter(QObject *, QEvent *) override;
private:
    void refresh_display();
    obs_source_t *program_scene() const;
    bool locate(QPointF position, MouseHit &hit, obs_source_t *&scene) const;
    bool same_target(const MouseHit &, obs_source_t *) const;
    bool live(const MouseHit &) const;
    std::shared_ptr<MouseReturn> mouse_;
    std::shared_ptr<VideoReceiver> video_;
    QPointer<QWidget> display_;
    obs_source_t *held_scene_ = nullptr;
    MouseHit held_;
    QPointF last_position_;
    Qt::MouseButton button_ = Qt::NoButton;
    bool old_tracking_ = false, suppress_context_menu_ = false;
    uint64_t last_move_ = 0;
};
}
