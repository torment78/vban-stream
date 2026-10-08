// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QWidget>
// Match OBS's public Qt widget identity without its private class or ABI.
class OBSQTDisplay : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
};
class OBSBasicPreview : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
};
