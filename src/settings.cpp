// SPDX-License-Identifier: GPL-2.0-or-later
#include "settings.hpp"
#include <obs-module.h>
#include <util/bmem.h>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QIcon>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace vban {
static QString config_path() {
    char *path = obs_module_config_path("settings.json");
    const QString result = QString::fromUtf8(path ? path : "");
    bfree(path);
    return result;
}
Config read_config(std::string &error) {
    Config cfg;
    const auto path = config_path();
    if (path.isEmpty()) { error = "OBS module configuration path is unavailable."; return cfg; }
    QFile file(path);
    if (!file.exists()) return cfg;
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString().toStdString(); return cfg; }
    QJsonParseError parse{};
    auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        error = "Could not read VBAN settings: " + parse.errorString().toStdString();
        return cfg;
    }
    const auto root = doc.object();
    if (root.value("version").toInt() != 1) { error = "Unsupported VBAN settings version."; return cfg; }
    cfg.common_ip = root.value("common_ip").toBool(true);
    cfg.sender_ip = root.value("sender_ip").toString().toStdString();
    const int port = root.value("port").toInt(6980);
    if (port < 1 || port > 65535) { error = "Saved UDP port is invalid."; return cfg; }
    cfg.port = static_cast<uint16_t>(port);
    const auto slots = root.value("slots").toArray();
    for (int i = 0; i < std::min(slots.size(), qsizetype(slot_count)); ++i) {
        const auto s = slots[i].toObject();
        cfg.slots[i] = {s.value("enabled").toBool(), s.value("label").toString().toStdString(),
            s.value("sender_ip").toString().toStdString(), s.value("stream_name").toString().toStdString()};
    }
    const auto videos = root.value("videos").toArray();
    for (int i = 0; i < std::min(videos.size(), qsizetype(video_slot_count)); ++i) {
        const auto item = videos[i].toObject();
        cfg.videos[i] = {item.value("enabled").toBool(), item.value("label").toString().toStdString(),
            item.value("sender_ip").toString().toStdString(), item.value("stream_name").toString().toStdString()};
    }
    const auto out = root.value("frame_output").toObject();
    auto &o = cfg.frame_output; o.enabled = out.value("enabled").toBool(false);
    o.destination_ip = out.value("destination_ip").toString().toStdString();
    o.local_ip = out.value("local_ip").toString().toStdString();
    o.stream_name = out.value("stream_name").toString("OBS-PROGRAM").toStdString();
    o.format = out.value("format").toString("JPEG").toStdString();
    const int output_port = out.value("port").toInt(6980); o.port = uint16_t(output_port > 0 && output_port <= 65535 ? output_port : 0);
    o.max_width = out.value("max_width").toInt(1280); o.max_height = out.value("max_height").toInt(720);
    o.fps = out.value("fps").toInt(15); o.quality = out.value("quality").toInt(80); o.mbps = out.value("mbps").toInt(24);
    cfg.mouse_local_ip = root.value("mouse_local_ip").toString().toStdString();
    const auto mice = root.value("mouse_returns").toArray();
    for (int i = 0; i < std::min(mice.size(), qsizetype(video_slot_count)); ++i) {
        const auto m = mice[i].toObject(); const int port_value = m.value("port").toInt(6980);
        cfg.mice[i] = {m.value("enabled").toBool(false), m.value("destination_ip").toString().toStdString(),
            uint16_t(port_value > 0 && port_value <= 65535 ? port_value : 0),
            m.value("stream_name").toString("Command1").toStdString()};
    }
    const int buffer = root.value("return_buffer_ms").toInt(default_return_buffer_ms);
    cfg.return_buffer_ms = buffer >= int(min_return_buffer_ms) && buffer <= int(max_return_buffer_ms)
        ? static_cast<uint32_t>(buffer) : 0;
    cfg.return_local_ip = root.value("return_local_ip").toString().trimmed().toStdString();
    const auto returns = root.value("returns").toArray();
    for (int i = 0; i < std::min(returns.size(), qsizetype(return_count)); ++i) {
        const auto item = returns[i].toObject();
        auto &r = cfg.returns[i];
        r.enabled = item.value("enabled").toBool(false);
        r.destination_ip = item.value("destination_ip").toString().toStdString();
        const auto destination_port = item.value("destination_port").toInt(6980);
        r.destination_port = destination_port > 0 && destination_port <= 65535
            ? static_cast<uint16_t>(destination_port) : 0;
        r.stream_name = item.value("stream_name").toString(QString::fromStdString(r.stream_name)).toStdString();
        r.pcm_bits = item.value("pcm_bits").toInt(24);
    }
    return cfg;
}
bool write_config(const Config &cfg, std::string &error) {
    const auto path = config_path();
    if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) {
        error = "Cannot create the OBS module settings directory."; return false;
    }
    QJsonArray slots;
    for (const auto &s : cfg.slots) slots.append(QJsonObject{
        {"enabled", s.enabled}, {"label", QString::fromStdString(s.label)},
        {"sender_ip", QString::fromStdString(s.sender_ip)}, {"stream_name", QString::fromStdString(s.stream_name)}});
    QJsonArray videos;
    for (const auto &s : cfg.videos) videos.append(QJsonObject{
        {"enabled", s.enabled}, {"label", QString::fromStdString(s.label)},
        {"sender_ip", QString::fromStdString(s.sender_ip)}, {"stream_name", QString::fromStdString(s.stream_name)}});
    QJsonArray mice;
    for (const auto &m : cfg.mice) mice.append(QJsonObject{
        {"enabled", m.enabled}, {"destination_ip", QString::fromStdString(m.destination_ip)},
        {"port", m.port}, {"stream_name", QString::fromStdString(m.stream_name)}});
    QJsonArray returns;
    for (const auto &r : cfg.returns) returns.append(QJsonObject{
        {"enabled", r.enabled}, {"destination_ip", QString::fromStdString(r.destination_ip)},
        {"destination_port", r.destination_port}, {"stream_name", QString::fromStdString(r.stream_name)},
        {"pcm_bits", r.pcm_bits}});
    const auto &o = cfg.frame_output;
    const QJsonObject output{{"enabled",o.enabled},{"destination_ip",QString::fromStdString(o.destination_ip)},
        {"local_ip",QString::fromStdString(o.local_ip)},{"port",o.port},{"stream_name",QString::fromStdString(o.stream_name)},
        {"format",QString::fromStdString(o.format)},{"max_width",o.max_width},{"max_height",o.max_height},
        {"fps",o.fps},{"quality",o.quality},{"mbps",o.mbps}};
    const QJsonDocument doc(QJsonObject{
        {"version", 1}, {"common_ip", cfg.common_ip}, {"sender_ip", QString::fromStdString(cfg.sender_ip)},
        {"port", cfg.port}, {"slots", slots}, {"returns", returns}, {"videos", videos},
        {"frame_output",output},
        {"mouse_returns", mice}, {"mouse_local_ip", QString::fromStdString(cfg.mouse_local_ip)},
        {"return_local_ip", QString::fromStdString(cfg.return_local_ip)}, {"return_buffer_ms", int(cfg.return_buffer_ms)}});
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { error = file.errorString().toStdString(); return false; }
    const auto bytes = doc.toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        error = "Could not save settings: " + file.errorString().toStdString(); return false;
    }
    return true;
}
std::string status_text(const Receiver &receiver, int index) {
    if (index < 0 || index >= int(slot_count)) return "Choose a configured stream, or open VBAN settings.";
    const auto cfg = receiver.config();
    const auto s = receiver.status(static_cast<size_t>(index));
    QString text = QString("Status: %1<br>Sender: %2 &nbsp; UDP port: %3<br>Stream: %4")
        .arg(state_name(s.state), QString::fromStdString(cfg.ip(index)).toHtmlEscaped())
        .arg(cfg.port).arg(QString::fromStdString(cfg.slots[index].stream_name).toHtmlEscaped());
    if (s.format.rate) text += QString("<br>Format: %1 Hz / %2 ch / %3%4")
        .arg(s.format.rate).arg(s.format.channels).arg(format_name(s.format.type))
        .arg(s.format.channels == 7 ? " (channel 8 padded with silence)" : "");
    text += QString("<br>Packets: %1 &nbsp; Lost: %2 &nbsp; Duplicate: %3<br>"
                    "Reordered: %4 &nbsp; Late: %5 &nbsp; Invalid: %6 &nbsp; Unsupported: %7<br>"
                    "Underruns: %8 &nbsp; Overruns: %9 &nbsp; Clock corrections: %10")
        .arg(s.counters.received).arg(s.counters.lost).arg(s.counters.duplicates)
        .arg(s.counters.reordered).arg(s.counters.late).arg(s.counters.corrupt)
        .arg(s.counters.unsupported).arg(s.counters.underruns).arg(s.counters.overruns)
        .arg(s.counters.drift_corrections);
    if (!s.error.empty()) text += "<br>" + QString::fromStdString(s.error).toHtmlEscaped();
    return text.toStdString();
}
class SettingsDialog final : public QDialog {
public:
    SettingsDialog(QWidget *parent, std::shared_ptr<Receiver> receiver, Config initial,
                   std::shared_ptr<MonitorReturn> returns, std::shared_ptr<VideoReceiver> video, std::shared_ptr<MouseReturn> mouse, ProgramMouse *control, std::shared_ptr<ProgramOutput> output, std::function<bool(const Config &, std::string &)> apply)
        : QDialog(parent), receiver_(std::move(receiver)), returns_(std::move(returns)), video_(std::move(video)), mouse_(std::move(mouse)), control_(control), output_(std::move(output)), apply_(std::move(apply)) {
        setWindowTitle("VBAN Stream Settings");
        // Scope the dark appearance to this dialog; never change OBS's application palette.
        setStyleSheet(R"(
            QWidget { background-color: #101114; color: #eceef2; }
            QTabWidget::pane { border: 1px solid #363940; }
            QTabBar::tab { background: #1b1e24; border: 1px solid #363940; padding: 7px 14px; }
            QTabBar::tab:selected { background: #101114; border-bottom-color: #101114; color: #72c8f0; }
            QGroupBox { border: 1px solid #363940; border-radius: 5px; margin-top: 12px; padding-top: 8px; }
            QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: #72c8f0; }
            QLineEdit, QSpinBox, QComboBox { background: #08090b; border: 1px solid #41454e; border-radius: 3px; padding: 3px; selection-background-color: #246d91; }
            QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: #72c8f0; }
            QComboBox QAbstractItemView { background: #111318; color: #eceef2; selection-background-color: #246d91; }
            QPushButton { background: #262b33; border: 1px solid #4c5360; border-radius: 4px; padding: 6px 14px; }
            QPushButton:hover { background: #343d49; border-color: #72c8f0; }
            QPushButton:pressed { background: #1a435a; }
            QWidget:disabled { color: #777d88; }
            QToolTip { background: #20252c; color: #eceef2; border: 1px solid #4c5360; }
        )");
        char *icon = obs_module_file("vban-audio.png");
        if (icon) { setWindowIcon(QIcon(QString::fromUtf8(icon))); bfree(icon); }
        setAttribute(Qt::WA_DeleteOnClose);
        setMinimumWidth(900);
        auto *outer = new QVBoxLayout(this);
        auto *tabs = new QTabWidget(this); tabs->setObjectName("vban_tabs"); outer->addWidget(tabs);
        auto *audio_page = new QWidget(tabs);
        auto *layout = new QVBoxLayout(audio_page);
        tabs->addTab(audio_page, "Audio and returns");
        common_ = new QCheckBox("Use one sender IP for all audio streams", this);
        common_->setObjectName("common_ip"); common_->setChecked(initial.common_ip);
        layout->addWidget(common_);
        auto *form = new QFormLayout;
        ip_ = new QLineEdit(QString::fromStdString(initial.sender_ip), this);
        ip_->setPlaceholderText("192.168.1.50");
        ip_->setMaxLength(15);
        form->addRow("Common sender IPv4", ip_);
        port_ = new QSpinBox(this); port_->setRange(1,65535); port_->setValue(initial.port);
        form->addRow("UDP listen port", port_);
        layout->addLayout(form);
        auto *port_help = new QLabel("The UDP listen port is shared by audio and video inputs.", this);
        layout->addWidget(port_help);
        auto *grid = new QGridLayout;
        const char *headers[]{"Slot", "Enabled", "Friendly name", "VBAN stream name", "Sender IPv4", "Live status", "Channels", "Input format"};
        for (int c = 0; c < 8; ++c) grid->addWidget(new QLabel(headers[c], this), 0, c);
        for (size_t i = 0; i < slot_count; ++i) {
            const auto &s = initial.slots[i];
            const int row = static_cast<int>(i)+1;
            grid->addWidget(new QLabel(QString::number(row), this), row, 0);
            enabled_[i] = new QCheckBox(this); enabled_[i]->setChecked(s.enabled);
            labels_[i] = new QLineEdit(QString::fromStdString(s.label), this);
            labels_[i]->setMaxLength(64);
            names_[i] = new QLineEdit(QString::fromStdString(s.stream_name), this);
            names_[i]->setObjectName(QString("stream_%1").arg(i)); names_[i]->setMaxLength(16); names_[i]->setPlaceholderText("MIC");
            senders_[i] = new QLineEdit(QString::fromStdString(s.sender_ip), this);
            senders_[i]->setObjectName(QString("sender_%1").arg(i)); senders_[i]->setMaxLength(15);
            states_[i] = new QLabel(this);
            channels_[i] = new QLabel(this);
            channels_[i]->setObjectName(QString("input_channels_%1").arg(i));
            channels_[i]->setAlignment(Qt::AlignCenter);
            channels_[i]->setFrameShape(QFrame::StyledPanel);
            channels_[i]->setMargin(3);
            channels_[i]->setFixedWidth(channels_[i]->fontMetrics().horizontalAdvance("8") + 24);
            formats_[i] = new QLabel(this);
            formats_[i]->setObjectName(QString("input_format_%1").arg(i));
            formats_[i]->setMinimumWidth(formats_[i]->fontMetrics().horizontalAdvance("PCM 24-bit"));
            channels_[i]->setToolTip("Incoming VBAN channels before OBS downmixing. No live input is shown as a dash.");
            grid->addWidget(enabled_[i], row, 1);
            grid->addWidget(labels_[i], row, 2);
            grid->addWidget(names_[i], row, 3);
            grid->addWidget(senders_[i], row, 4);
            grid->addWidget(states_[i], row, 5);
            grid->addWidget(channels_[i], row, 6, Qt::AlignCenter);
            grid->addWidget(formats_[i], row, 7);
        }
        layout->addLayout(grid);
        receiving_ = new QLabel(this);
        receiving_->setObjectName("receiving_streams");
        layout->addWidget(receiving_);
        auto *help = new QLabel("Stream names are case-sensitive (1–16 printable ASCII characters). "
            "Live status reflects the applied settings. Each stream can carry 1–8 input channels; OBS may downmix them to its output layout. "
            "Multiple OBS sources can select the same slot.", this);
        help->setWordWrap(true); layout->addWidget(help);
        auto *returns_box = new QGroupBox("VBAN RETURNS", this);
        auto *returns_layout = new QVBoxLayout(returns_box);
        auto *network_form = new QFormLayout;
        return_local_ = new QComboBox(returns_box);
        return_local_->setObjectName("return_local_ip");
        return_local_->addItem("Automatic (Windows route)", QString());
        std::string adapter_error;
        for (const auto &adapter : local_ipv4_addresses(adapter_error))
            return_local_->addItem(QString::fromStdString(adapter.address + " - " + adapter.adapter_name),
                                   QString::fromStdString(adapter.address));
        if (!initial.return_local_ip.empty()) {
            auto index = return_local_->findData(QString::fromStdString(initial.return_local_ip));
            if (index < 0) {
                return_local_->addItem(QString::fromStdString(initial.return_local_ip + " (unavailable)"),
                                       QString::fromStdString(initial.return_local_ip));
                index = return_local_->count() - 1;
            }
            return_local_->setCurrentIndex(index);
        }
        return_local_->setToolTip(adapter_error.empty()
            ? "Choose this OBS computer's LAN address. Both returns use its adapter. Addresses refresh when this window opens."
            : QString::fromStdString(adapter_error));
        network_form->addRow("Send from this PC", return_local_);
        return_buffer_ = new QSpinBox(returns_box);
        return_buffer_->setObjectName("return_buffer_ms");
        return_buffer_->setRange(min_return_buffer_ms, max_return_buffer_ms);
        return_buffer_->setSingleStep(10); return_buffer_->setSuffix(" ms");
        return_buffer_->setValue(initial.return_buffer_ms ? initial.return_buffer_ms : default_return_buffer_ms);
        return_buffer_->setToolTip("Wait for source audio before sending. Start at 60 ms; try 100 ms if Late audio frames keep increasing. Higher values add monitor delay. Network reception buffering is set in VoiceMeeter.");
        network_form->addRow("Return audio buffer", return_buffer_);
        returns_layout->addLayout(network_form);
        auto *returns_grid = new QGridLayout;
        returns_layout->addLayout(returns_grid);
        const char *return_headers[]{"Return", "Enabled", "PCM format", "Destination IPv4", "UDP port", "VBAN stream name", "Status"};
        for (int c = 0; c < 7; ++c) returns_grid->addWidget(new QLabel(return_headers[c], returns_box), 0, c);
        for (size_t i = 0; i < return_count; ++i) {
            const auto &r = initial.returns[i];
            const int row = static_cast<int>(i) + 1;
            const auto prefix = QString("return_%1_").arg(i);
            return_enabled_[i] = new QCheckBox(returns_box);
            return_enabled_[i]->setObjectName(prefix + "enabled"); return_enabled_[i]->setChecked(r.enabled);
            return_formats_[i] = new QComboBox(returns_box);
            return_formats_[i]->setObjectName(prefix + "pcm_bits");
            return_formats_[i]->addItem("PCM 16-bit", 16);
            return_formats_[i]->addItem("PCM 24-bit", 24);
            return_formats_[i]->setCurrentIndex(return_formats_[i]->findData(r.pcm_bits == 16 ? 16 : 24));
            return_formats_[i]->setToolTip("Choose this return's PCM bit depth. The sample rate follows OBS (48 kHz when OBS is set to 48 kHz). Click Apply to use the selection.");
            return_ips_[i] = new QLineEdit(QString::fromStdString(r.destination_ip), returns_box);
            return_ips_[i]->setObjectName(prefix + "ip"); return_ips_[i]->setPlaceholderText("192.168.1.50");
            return_ports_[i] = new QSpinBox(returns_box);
            return_ports_[i]->setObjectName(prefix + "port"); return_ports_[i]->setRange(1, 65535);
            return_ports_[i]->setValue(r.destination_port ? r.destination_port : 6980);
            return_names_[i] = new QLineEdit(QString::fromStdString(r.stream_name), returns_box);
            return_names_[i]->setObjectName(prefix + "name");
            return_states_[i] = new QLabel(returns_box);
            return_states_[i]->setObjectName(prefix + "status");
            returns_grid->addWidget(new QLabel(QString("RETURN %1").arg(i+1), returns_box), row, 0);
            returns_grid->addWidget(return_enabled_[i], row, 1);
            returns_grid->addWidget(return_formats_[i], row, 2);
            returns_grid->addWidget(return_ips_[i], row, 3);
            returns_grid->addWidget(return_ports_[i], row, 4);
            returns_grid->addWidget(return_names_[i], row, 5);
            auto *status_layout = new QVBoxLayout;
            status_layout->addWidget(return_states_[i]);
            return_routes_[i] = new QLabel(returns_box);
            return_routes_[i]->setObjectName(prefix + "route");
            return_routes_[i]->setTextFormat(Qt::PlainText);
            return_routes_[i]->setWordWrap(true);
            return_routes_[i]->setMinimumWidth(200);
            return_routes_[i]->setMinimumHeight(2 * return_routes_[i]->fontMetrics().lineSpacing());
            status_layout->addWidget(return_routes_[i]);
            returns_grid->addLayout(status_layout, row, 6);
        }
        auto *return_help = new QLabel("Both returns send the same OBS headphone/monitor mix. "
            "Use Monitor Only or Monitor and Output in OBS to include a source. "
            "Do not route the returns back into the VBAN feeds entering OBS.", returns_box);
        return_help->setWordWrap(true); returns_grid->addWidget(return_help, 3, 0, 1, 7);
        layout->addWidget(returns_box);
        auto *video_scroll = new QScrollArea(tabs);
        video_scroll->setObjectName("video_settings_scroll");
        video_scroll->setWidgetResizable(true); video_scroll->setFrameShape(QFrame::NoFrame);
        auto *video_page = new QWidget(video_scroll);
        auto *video_layout = new QVBoxLayout(video_page);
        video_scroll->setWidget(video_page); tabs->addTab(video_scroll, "Video inputs");
        auto *video_help = new QLabel("Receive two VBAN-Frame streams from VoiceMeeter, Matrix or another compatible sender. "
            "Choose App View or a display and JPEG/PNG on the sender. Send to this PC using the UDP listen port on the Audio and returns tab. "
            "After Apply, add a VBAN Video source and select one of these inputs.", video_page);
        video_help->setWordWrap(true); video_layout->addWidget(video_help);
        auto *mouse_network = new QFormLayout;
        mouse_local_ = new QComboBox(video_page); mouse_local_->setObjectName("mouse_local_ip");
        mouse_local_->addItem("Automatic (system route)", QString());
        std::string mouse_adapter_error;
        for (const auto &a : local_ipv4_addresses(mouse_adapter_error))
            mouse_local_->addItem(QString::fromStdString(a.address + " - " + a.adapter_name), QString::fromStdString(a.address));
        if (!initial.mouse_local_ip.empty()) {
            auto n = mouse_local_->findData(QString::fromStdString(initial.mouse_local_ip));
            if (n < 0) { mouse_local_->addItem(QString::fromStdString(initial.mouse_local_ip + " (unavailable)"), QString::fromStdString(initial.mouse_local_ip)); n = mouse_local_->count()-1; }
            mouse_local_->setCurrentIndex(n);
        }
        mouse_network->addRow("Mouse control: send from this PC", mouse_local_); video_layout->addLayout(mouse_network);
        for (size_t i = 0; i < video_slot_count; ++i) {
            const auto &s = initial.videos[i];
            auto *box = new QGroupBox(QString("VIDEO %1").arg(i+1), video_page);
            auto *card = new QVBoxLayout(box);
            auto *fields = new QFormLayout;
            card->addLayout(fields);
            const auto prefix = QString("video_%1_").arg(i);
            video_enabled_[i] = new QCheckBox("Receive this video", box);
            video_enabled_[i]->setObjectName(prefix + "enabled"); video_enabled_[i]->setChecked(s.enabled);
            video_labels_[i] = new QLineEdit(QString::fromStdString(s.label), box);
            video_labels_[i]->setObjectName(prefix + "label"); video_labels_[i]->setMaxLength(64);
            video_ips_[i] = new QLineEdit(QString::fromStdString(s.sender_ip), box);
            video_ips_[i]->setObjectName(prefix + "ip"); video_ips_[i]->setMaxLength(15);
            video_ips_[i]->setPlaceholderText("192.168.1.11");
            video_names_[i] = new QLineEdit(QString::fromStdString(s.stream_name), box);
            video_names_[i]->setObjectName(prefix + "name"); video_names_[i]->setMaxLength(16);
            video_names_[i]->setPlaceholderText("VIDEO1");
            video_states_[i] = new QLabel(box); video_states_[i]->setObjectName(prefix + "status");
            video_states_[i]->setTextFormat(Qt::PlainText); video_states_[i]->setWordWrap(true);
            fields->addRow(video_enabled_[i]); fields->addRow("Friendly name", video_labels_[i]);
            fields->addRow("Sender IPv4", video_ips_[i]); fields->addRow("VBAN stream name", video_names_[i]);
            fields->addRow("Live status", video_states_[i]);
            const auto &m = initial.mice[i];
            auto *mouse_box = new QGroupBox(QString("Mouse control for Video %1").arg(i+1), box);
            auto *mouse_fields = new QFormLayout(mouse_box); const auto mouse_prefix = QString("mouse_%1_").arg(i);
            mouse_enabled_[i] = new QCheckBox("Enable mouse control", mouse_box);
            mouse_enabled_[i]->setObjectName(mouse_prefix+"enabled"); mouse_enabled_[i]->setChecked(m.enabled);
            mouse_ips_[i] = new QLineEdit(QString::fromStdString(m.destination_ip), mouse_box);
            mouse_ips_[i]->setObjectName(mouse_prefix+"ip"); mouse_ips_[i]->setMaxLength(15);
            mouse_ips_[i]->setPlaceholderText("IP of the computer sending this video");
            mouse_ports_[i] = new QSpinBox(mouse_box); mouse_ports_[i]->setObjectName(mouse_prefix+"port");
            mouse_ports_[i]->setRange(1,65535); mouse_ports_[i]->setValue(m.port ? m.port : 6980);
            mouse_names_[i] = new QLineEdit(QString::fromStdString(m.stream_name), mouse_box);
            mouse_names_[i]->setObjectName(mouse_prefix+"name"); mouse_names_[i]->setMaxLength(16);
            mouse_states_[i] = new QLabel(mouse_box); mouse_states_[i]->setObjectName(mouse_prefix+"status");
            mouse_states_[i]->setTextFormat(Qt::PlainText); mouse_states_[i]->setWordWrap(true);
            mouse_fields->addRow(mouse_enabled_[i]); mouse_fields->addRow("Destination IPv4", mouse_ips_[i]);
            mouse_fields->addRow("UDP port", mouse_ports_[i]); mouse_fields->addRow("VBAN-TEXT stream name", mouse_names_[i]);
            mouse_fields->addRow("Status", mouse_states_[i]); card->addWidget(mouse_box);
            video_layout->addWidget(box);
        }
        mouse_hint_ = new QLabel(video_page); mouse_hint_->setWordWrap(true); video_layout->addWidget(mouse_hint_);
        auto *mouse_help = new QLabel("Each mouse card controls the App View received by the video directly above it. "
            "On that sender, enable the matching incoming VBAN-TEXT stream and Manage Mouse command. "
            "In OBS Studio Mode, hold Ctrl while left/right-clicking or dragging in Program; Preview remains the scene editor.", video_page);
        mouse_help->setWordWrap(true); video_layout->addWidget(mouse_help);
        auto *video_note = new QLabel("JPEG/PNG and up to 4K are detected automatically; add a VBAN Stream source for audio. "
            "Mouse control supports ordinary scene cropping, scaling and rotation. Filtered sources and Crop to Bounding Box are not interactive. "
            "Control stops on video loss, scene changes or released Ctrl. Keyboard typing and mouse wheel are not forwarded.", video_page);
        video_note->setWordWrap(true); video_layout->addWidget(video_note); video_layout->addStretch();
        auto *output_page = new QWidget(tabs);auto *output_layout = new QVBoxLayout(output_page);
        tabs->addTab(output_page, "OBS to VBAN Frame");
        auto *output_help = new QLabel("Send the OBS Program picture to VBAN-Screen or another VBAN-Frame receiver. "
            "This follows your live Program scene, including scene switches and transitions, without the OBS controls. "
            "In Studio Mode, editing Preview does not change the outgoing picture until you transition it to Program.", output_page);
        output_help->setWordWrap(true); output_layout->addWidget(output_help);
        const auto &o = initial.frame_output;
        auto *output_box = new QGroupBox("PROGRAM VIDEO OUTPUT", output_page);auto *output_fields = new QFormLayout(output_box);
        output_enabled_ = new QCheckBox("Send OBS Program", output_box);output_enabled_->setObjectName("frame_output_enabled");output_enabled_->setChecked(o.enabled);
        output_ips_ = new QLineEdit(QString::fromStdString(o.destination_ip),output_box);output_ips_->setObjectName("frame_output_ip");output_ips_->setMaxLength(15);output_ips_->setPlaceholderText("IP of the receiving computer");
        output_local_ = new QComboBox(output_box);output_local_->setObjectName("frame_output_local");output_local_->addItem("Automatic (system route)",QString());
        std::string output_adapter_error;
        for(const auto &a:local_ipv4_addresses(output_adapter_error))output_local_->addItem(QString::fromStdString(a.address+" - "+a.adapter_name),QString::fromStdString(a.address));
        if(!o.local_ip.empty()) { auto n=output_local_->findData(QString::fromStdString(o.local_ip));
            if(n<0) {output_local_->addItem(QString::fromStdString(o.local_ip+" (unavailable)"),QString::fromStdString(o.local_ip));n=output_local_->count()-1;}output_local_->setCurrentIndex(n); }
        output_port_ = new QSpinBox(output_box);output_port_->setObjectName("frame_output_port");output_port_->setRange(1,65535);output_port_->setValue(o.port?o.port:6980);
        output_name_ = new QLineEdit(QString::fromStdString(o.stream_name),output_box);output_name_->setObjectName("frame_output_name");output_name_->setMaxLength(16);
        output_format_ = new QComboBox(output_box);output_format_->setObjectName("frame_output_format");output_format_->addItems({"JPEG","PNG"});output_format_->setCurrentText(QString::fromStdString(o.format));
        output_resolution_ = new QComboBox(output_box);output_resolution_->setObjectName("frame_output_resolution");
        output_resolution_->addItem("Up to 640 x 360",640);output_resolution_->addItem("Up to 1280 x 720",1280);output_resolution_->addItem("Up to 1920 x 1080",1920);
        const auto resolution_index=output_resolution_->findData(o.max_width);output_resolution_->setCurrentIndex(resolution_index<0?1:resolution_index);
        output_fps_ = new QSpinBox(output_box);output_fps_->setObjectName("frame_output_fps");output_fps_->setRange(1,30);output_fps_->setSuffix(" fps");output_fps_->setValue(o.fps);
        output_quality_ = new QSpinBox(output_box);output_quality_->setObjectName("frame_output_quality");output_quality_->setRange(1,100);output_quality_->setValue(o.quality);
        output_limit_ = new QComboBox(output_box);output_limit_->setObjectName("frame_output_limit");
        for(int rate:{12,24,48,84})output_limit_->addItem(QString("%1 Mbps").arg(rate),rate);
        const auto rate_index=output_limit_->findData(o.mbps);output_limit_->setCurrentIndex(rate_index<0?1:rate_index);
        output_status_ = new QLabel(output_box);output_status_->setObjectName("frame_output_status");output_status_->setWordWrap(true);output_status_->setTextFormat(Qt::PlainText);
        output_fields->addRow(output_enabled_);output_fields->addRow("Send from this PC",output_local_);
        output_fields->addRow("Destination IPv4",output_ips_);output_fields->addRow("UDP port",output_port_);output_fields->addRow("VBAN stream name",output_name_);
        output_fields->addRow("Image format",output_format_);output_fields->addRow("Resolution limit",output_resolution_);output_fields->addRow("Maximum frame rate",output_fps_);
        output_fields->addRow("JPEG quality",output_quality_);output_fields->addRow("Network limit",output_limit_);output_fields->addRow("Live status",output_status_);
        output_layout->addWidget(output_box);
        connect(output_format_,&QComboBox::currentTextChanged,this,[this](const QString &format){output_quality_->setEnabled(format=="JPEG");});
        output_quality_->setEnabled(o.format=="JPEG");
        auto *output_note = new QLabel("On the receiver, match this OBS computer's sender IP, the UDP port and stream name. "
            "Start with JPEG, 1280 x 720 and 15 fps. Aspect ratio is preserved and smaller pictures are not enlarged. "
            "PNG is lossless and can need more bandwidth. Skipped frames mean encoding or sending could not keep up; lower the resolution or frame rate. "
            "Video only: use the audio returns separately. SDR output is supported. Disable Send OBS Program before changing OBS video settings. "
            "Sending confirms local UDP writes, not reception on the other computer.", output_page);
        output_note->setWordWrap(true);output_layout->addWidget(output_note);output_layout->addStretch();
        error_ = new QLabel(this); error_->setWordWrap(true); error_->setTextFormat(Qt::PlainText);
        outer->addWidget(error_);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Apply | QDialogButtonBox::Ok, this);
        auto *donate = buttons->addButton("Donate on Ko-fi", QDialogButtonBox::ActionRole);
        donate->setObjectName("donate");
        donate->setToolTip("Support development at https://ko-fi.com/msffixit");
        connect(donate, &QPushButton::clicked, this, [] {
            QDesktopServices::openUrl(QUrl("https://ko-fi.com/msffixit"));
        });
        outer->addWidget(buttons);
        connect(common_, &QCheckBox::toggled, this, [this] { update_mode(); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, this, [this] { if (this->apply()) accept(); });
        connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] { this->apply(); });
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] { refresh(); });
        timer->start(500);
        update_mode(); refresh();
    }
private:
    void update_mode() {
        ip_->setEnabled(common_->isChecked());
        for (auto *sender : senders_) sender->setEnabled(!common_->isChecked());
    }
    void refresh() {
        const auto out = output_->status();
        QString output_text = !out.enabled ? "Disabled" : out.sending ? "Sending" : "Waiting for Program";
        if(out.enabled) output_text += QString(" | %1 x %2 | Frames: %3 | Skipped: %4 | Errors: %5 | From: %6")
            .arg(out.width).arg(out.height).arg(out.frames).arg(out.dropped).arg(out.errors).arg(QString::fromStdString(out.local_ip));
        if(!out.error.empty()) output_text += " | " + QString::fromStdString(out.error);
        output_status_->setText(output_text);
        output_status_->setToolTip(QString("Sent packets: %1\nSent bytes: %2").arg(out.packets).arg(out.bytes));
        mouse_hint_->setText(control_ ? QString::fromStdString(control_->availability()) : "Program control is unavailable.");
        for (size_t i = 0; i < video_slot_count; ++i) {
            const auto s = mouse_->status(i);
            auto text = !s.enabled ? QString("Disabled") : s.dragging ? QString("Dragging") : QString("Ready");
            if (s.enabled) text += QString(" | Sent: %1 | Send errors: %2 | From: %3").arg(s.packets).arg(s.errors).arg(QString::fromStdString(s.local_ip));
            if (!s.error.empty()) text += " | " + QString::fromStdString(s.error);
            mouse_states_[i]->setText(text);
        }
        for (size_t i = 0; i < video_slot_count; ++i) {
            const auto s = video_->status(i);
            QString text = !s.enabled ? "Disabled" : s.receiving ? "Receiving" : "Waiting";
            if (!s.error.empty()) text += " - " + QString::fromStdString(s.error);
            if (s.receiving) text += QString(" - %1 x %2 - %3").arg(s.width).arg(s.height).arg(QString::fromStdString(s.format));
            video_states_[i]->setText(text);
            video_states_[i]->setToolTip(QString("Decoded frames: %1\nIncomplete frames: %2\nInvalid packets: %3\n"
                "Duplicate/old packets: %4\nDecode errors: %5\nQueue drops: %6")
                .arg(s.decoded).arg(s.packets.incomplete).arg(s.packets.invalid)
                .arg(s.packets.duplicates).arg(s.decode_errors).arg(s.queue_drops));
        }
        unsigned receiving = 0;
        for (size_t i = 0; i < slot_count; ++i) {
            auto s = receiver_->status(i);
            states_[i]->setText(QString::fromUtf8(state_name(s.state)));
            const bool live = s.state == State::receiving;
            if (live) ++receiving;
            channels_[i]->setText(live ? QString::number(s.format.channels) : QString::fromUtf8("—"));
            formats_[i]->setText(live ? QString::fromUtf8(format_name(s.format.type)) : QString::fromUtf8("—"));
            states_[i]->setToolTip(QString::fromStdString(status_text(*receiver_, static_cast<int>(i))));
        }
        receiving_->setText(QString("Receiving: %1 / %2 streams").arg(receiving).arg(slot_count));
        for (size_t i = 0; i < return_count; ++i) {
            const auto s = returns_->status(i);
            QString text = return_state_name(s.state);
            if (s.state == ReturnState::sending && s.silence) text += " (silence)";
            return_states_[i]->setText(text);
            const auto route = s.source_ip.empty() ? QString() :
                QString("%1 -> %2:%3").arg(QString::fromStdString(s.source_ip),
                    QString::fromStdString(s.destination_ip)).arg(s.destination_port);
            return_routes_[i]->setText(route);
            auto diagnostic = QString(
                "Applied stream: %1\nSocket source: %2:%3\nDestination: %4:%5\n"
                "Packets sent: %6\nSocket errors: %7\nLast send: %8 ms ago\n"
                "Send gaps over 20 ms: %9\nLargest send gap: %10 ms\n"
                "Capture queue overflows: %11\nLate audio frames: %12\n"
                "Audio counters cover current source lifetimes; startup/monitor changes can add late frames.\n"
                "Socket source is before any router/NAT translation.\n%13")
                .arg(QString::fromStdString(s.stream_name), QString::fromStdString(s.source_ip))
                .arg(s.source_port).arg(QString::fromStdString(s.destination_ip)).arg(s.destination_port)
                .arg(s.packets).arg(s.errors).arg(s.last_send_age_ms, 0, 'f', 1)
                .arg(s.send_gaps).arg(s.max_send_gap_ms, 0, 'f', 1)
                .arg(s.capture_drops).arg(s.late_audio_frames).arg(QString::fromStdString(s.detail));
            diagnostic += QString("\nApplied format: Stereo PCM %1-bit").arg(s.pcm_bits);
            diagnostic += QString("\nReturn buffer: %1 ms\nCapture queue peak: %2 / %3 blocks (busiest source)\n"
                "Clock corrections: %4\nClock discontinuities: %5\nClipped samples: %6\nInvalid float samples: %7\n"
                "Audio worker priority: %8\nClipping means the combined mix exceeds full scale; lower source faders.")
                .arg(s.buffer_ms).arg(s.capture_queue_peak).arg(s.capture_queue_capacity)
                .arg(s.clock_corrections).arg(s.clock_discontinuities).arg(s.clipped_samples)
                .arg(s.nonfinite_samples).arg(s.audio_priority ? "Active" : "Unavailable");
            return_states_[i]->setToolTip(diagnostic);
            return_routes_[i]->setToolTip(diagnostic);
        }
    }
    bool apply() {
        Config cfg;
        auto &o = cfg.frame_output;o.enabled=output_enabled_->isChecked();o.destination_ip=output_ips_->text().trimmed().toStdString();
        o.local_ip=output_local_->currentData().toString().toStdString();o.port=static_cast<uint16_t>(output_port_->value());
        o.stream_name=output_name_->text().toStdString();o.format=output_format_->currentText().toStdString();
        o.max_width=output_resolution_->currentData().toInt();o.max_height=o.max_width*9/16;
        o.fps=output_fps_->value();o.quality=output_quality_->value();o.mbps=output_limit_->currentData().toInt();
        cfg.mouse_local_ip = mouse_local_->currentData().toString().toStdString();
        for (size_t i = 0; i < video_slot_count; ++i) cfg.mice[i] = {
            mouse_enabled_[i]->isChecked(), mouse_ips_[i]->text().trimmed().toStdString(),
            static_cast<uint16_t>(mouse_ports_[i]->value()), mouse_names_[i]->text().toStdString()};
        cfg.return_buffer_ms = static_cast<uint32_t>(return_buffer_->value());
        cfg.return_local_ip = return_local_->currentData().toString().toStdString();
        cfg.common_ip = common_->isChecked();
        cfg.sender_ip = ip_->text().trimmed().toStdString();
        cfg.port = static_cast<uint16_t>(port_->value());
        for (size_t i = 0; i < slot_count; ++i) cfg.slots[i] = {
            enabled_[i]->isChecked(), labels_[i]->text().toStdString(),
            senders_[i]->text().trimmed().toStdString(), names_[i]->text().toStdString()};
        for (size_t i = 0; i < return_count; ++i) cfg.returns[i] = {
            return_enabled_[i]->isChecked(), return_ips_[i]->text().trimmed().toStdString(),
            static_cast<uint16_t>(return_ports_[i]->value()), return_names_[i]->text().toStdString(),
            return_formats_[i]->currentData().toInt()};
        for (size_t i = 0; i < video_slot_count; ++i) cfg.videos[i] = {
            video_enabled_[i]->isChecked(), video_labels_[i]->text().toStdString(),
            video_ips_[i]->text().trimmed().toStdString(), video_names_[i]->text().toStdString()};
        std::string error;
        try {
            if (!apply_(cfg, error)) {
                error_->setText(QString::fromStdString(error)); return false;
            }
            error_->setText("Settings saved.");
            refresh(); return true;
        } catch (const std::exception &e) { error_->setText(QString::fromUtf8(e.what())); return false; }
    }
    std::shared_ptr<Receiver> receiver_;
    std::shared_ptr<MonitorReturn> returns_;
    std::shared_ptr<VideoReceiver> video_;
    std::shared_ptr<MouseReturn> mouse_;
    ProgramMouse *control_ = nullptr;
    std::shared_ptr<ProgramOutput> output_;
    QCheckBox *output_enabled_{};
    QLineEdit *output_ips_{}, *output_name_{};
    QComboBox *output_local_{}, *output_format_{}, *output_resolution_{}, *output_limit_{};
    QSpinBox *output_port_{}, *output_fps_{}, *output_quality_{};
    QLabel *output_status_{};
    QComboBox *mouse_local_{};
    QLabel *mouse_hint_{};
    std::array<QCheckBox *, video_slot_count> mouse_enabled_{};
    std::array<QLineEdit *, video_slot_count> mouse_ips_{}, mouse_names_{};
    std::array<QSpinBox *, video_slot_count> mouse_ports_{};
    std::array<QLabel *, video_slot_count> mouse_states_{};
    std::array<QCheckBox *, video_slot_count> video_enabled_{};
    std::array<QLineEdit *, video_slot_count> video_labels_{}, video_names_{}, video_ips_{};
    std::array<QLabel *, video_slot_count> video_states_{};
    std::function<bool(const Config &, std::string &)> apply_;
    QCheckBox *common_{};
    QLineEdit *ip_{};
    QSpinBox *port_{}, *return_buffer_{};
    QLabel *error_{}, *receiving_{};
    std::array<QCheckBox *, slot_count> enabled_{};
    std::array<QLineEdit *, slot_count> labels_{}, names_{}, senders_{};
    std::array<QLabel *, slot_count> states_{}, channels_{}, formats_{};
    std::array<QCheckBox *, return_count> return_enabled_{};
    std::array<QLineEdit *, return_count> return_ips_{}, return_names_{};
    std::array<QSpinBox *, return_count> return_ports_{};
    std::array<QComboBox *, return_count> return_formats_{};
    std::array<QLabel *, return_count> return_states_{}, return_routes_{};
    QComboBox *return_local_{};
};
QDialog *make_settings_dialog(QWidget *parent, std::shared_ptr<Receiver> receiver,
                             Config initial, std::shared_ptr<MonitorReturn> returns, std::shared_ptr<VideoReceiver> video, std::shared_ptr<MouseReturn> mouse, ProgramMouse *control, std::shared_ptr<ProgramOutput> output,
                             std::function<bool(const Config &, std::string &)> apply) {
    return new SettingsDialog(parent, std::move(receiver), std::move(initial), std::move(returns), std::move(video), std::move(mouse), control, std::move(output), std::move(apply));
}
}
