// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <array>
#include <iostream>
#include <stdexcept>

// Exercise the loaded plugin's real dialog using the isolated Qt test backend.
// No desktop input, installed OBS settings, or physical audio devices are used.
static void check_settings_layout(QDialog *dialog, const QString &root) {
    const auto require = [](bool ok, const char *message) {
        if (!ok) throw std::runtime_error(message);
    };
    const auto settle = [] {
        for (int i = 0; i < 3; ++i) QApplication::processEvents();
    };
    auto *tabs = dialog->findChild<QTabWidget *>("vban_tabs");
    auto *buttons = dialog->findChild<QDialogButtonBox *>();
    require(tabs && tabs->count() == 4 && buttons, "Four settings tabs and action buttons");
    const std::array<const char *, 4> titles{
        "VBAN Audio", "VBAN Returns", "Video inputs", "OBS to VBAN Frame"};
    const std::array<const char *, 4> last_controls{
        "stream_7", "return_1_name", "mouse_1_name", "frame_output_limit"};
    const std::array<const char *, 4> saved_controls{
        "stream_0", "return_0_name", "video_0_name", "frame_output_name"};
    std::array<QString, 4> values;
    for (size_t i = 0; i < values.size(); ++i) {
        auto *field = dialog->findChild<QLineEdit *>(saved_controls[i]);
        require(field != nullptr, "Settings field exists");
        values[i] = field->text();
    }
    const auto original_size = dialog->size();
    const auto original_tab = tabs->currentIndex();
    require(dialog->minimumWidth() <= 640 && dialog->minimumHeight() <= 400,
            "Dialog minimum fits a compact display");
    require(dialog->isSizeGripEnabled(), "Window offers a resize grip");
    const auto check_actions = [&] {
        const QRect tabs_rect(tabs->mapTo(dialog, QPoint()), tabs->size());
        for (const auto action : {QDialogButtonBox::Ok, QDialogButtonBox::Apply, QDialogButtonBox::Cancel}) {
            auto *button = buttons->button(action);
            require(button && button->isVisibleTo(dialog), "Action button remains visible");
            const QRect rect(button->mapTo(dialog, QPoint()), button->size());
            require(dialog->rect().contains(rect) && !tabs_rect.intersects(rect),
                    "Action button is inside the window and outside scrolling content");
        }
    };
    for (const QSize size : {QSize(1040, 700), QSize(640, 420), QSize(560, 360)}) {
        dialog->resize(size);
        settle();
        if (dialog->size() != size)
            std::cerr << "Requested " << size.width() << "x" << size.height()
                      << ", got " << dialog->width() << "x" << dialog->height() << "\n";
        require(dialog->size() == size, "Settings window can shrink to the requested size");
        for (int i = 0; i < 4; ++i) {
            require(tabs->tabText(i) == titles[i], "Settings tab order and title");
            tabs->setCurrentIndex(i);
            settle();
            require(dialog->size() == size, "Switching to a taller tab does not grow the window");
            auto *scroll = qobject_cast<QScrollArea *>(tabs->widget(i));
            auto *field = dialog->findChild<QWidget *>(last_controls[i]);
            require(scroll && field && scroll->widget()->isAncestorOf(field),
                    "Each tab scrolls its own complete set of controls");
            check_actions();
            if (size.height() == 360) {
                require(scroll->verticalScrollBar()->maximum() > 0 ||
                        scroll->horizontalScrollBar()->maximum() > 0,
                        "A compact settings page offers scrolling");
            }
            // Move the bars as a user would. Qt's ensureWidgetVisible may prefer
            // a text field's cursor rectangle instead of the complete control.
            const QRect target(field->mapTo(scroll->widget(), QPoint()), field->size());
            scroll->horizontalScrollBar()->setValue(target.center().x() - scroll->viewport()->width() / 2);
            scroll->verticalScrollBar()->setValue(target.center().y() - scroll->viewport()->height() / 2);
            settle();
            const QRect field_rect(field->mapTo(scroll->viewport(), QPoint()), field->size());
            if (!scroll->viewport()->rect().contains(field_rect)) {
                std::cerr << "Tab " << i << " at " << size.width() << "x" << size.height()
                          << ": viewport " << scroll->viewport()->width() << "x" << scroll->viewport()->height()
                          << ", field " << field_rect.x() << "," << field_rect.y()
                          << " " << field_rect.width() << "x" << field_rect.height()
                          << ", scroll " << scroll->horizontalScrollBar()->value() << "/"
                          << scroll->horizontalScrollBar()->maximum() << ","
                          << scroll->verticalScrollBar()->value() << "/"
                          << scroll->verticalScrollBar()->maximum() << "\n";
                dialog->grab().save(root + "/layout-failure.png");
                throw std::runtime_error("The final control can be fully reached by scrolling");
            }
            check_actions();
            if (size.height() == 360)
                require(dialog->grab().save(root + QString("/compact-tab-%1.png").arg(i)),
                        "Save compact layout preview");
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
            scroll->horizontalScrollBar()->setValue(scroll->horizontalScrollBar()->maximum());
            settle();
            check_actions();
            scroll->verticalScrollBar()->setValue(0);
            scroll->horizontalScrollBar()->setValue(0);
        }
    }
    for (size_t i = 0; i < values.size(); ++i)
        require(dialog->findChild<QLineEdit *>(saved_controls[i])->text() == values[i],
                "Resizing, scrolling and changing tabs preserve unapplied settings");
    tabs->setCurrentIndex(original_tab);
    dialog->resize(original_size);
    settle();
    std::cout << "PASS: four settings tabs at 1040x700, 640x420 and 560x360; "
                 "controls reachable, actions visible, edits preserved.\n";
}
