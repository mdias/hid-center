// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WheelbaseLedPage.h"

#include "AlphaEvoSettings.h"
#include "LedRing.h"
#include "WheelLedController.h"

#include <QCheckBox>
#include <QComboBox>
#include <QConicalGradient>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolBox>
#include <QVBoxLayout>

namespace hidcenter::simagic_alpha_evo {
namespace {

/// The widget's ceiling must not exceed what a packet can carry. They are
/// declared apart on purpose — LedRing is self-contained so it can be reused
/// without the protocol — and this is the one place that sees both.
static_assert(kMaxLedStops >= kMaxRingColours,
              "the ring can hold more colours than the LED packet can carry");

/// How often the on-screen band is repainted while the page is visible.
///
/// The same twenty-five a second the wheel is capped at, and for a different
/// reason: past that a fade is smooth to the eye, and each repaint runs the
/// diffuser's blur over the whole ring.
constexpr int kPreviewTickMs = 40;

/// The swatch beside each collapsed mode, so an accordion that is mostly shut
/// still says what each mode looks like.
constexpr int kSwatchPx = 20;

/// How long a ring-light edit settles before it is written, and how long a
/// write waits before trying again when another was already in flight. The
/// same figure as the FFB Settings page, for the same reasons.
constexpr int kRingLightCommitMs = 250;

/// Every animation, in the order the combo offers them.
constexpr std::array<LedAnimation, 4> kAnimationOrder{
    LedAnimation::Static,
    LedAnimation::Breathing,
    LedAnimation::Blinking,
    LedAnimation::Rotating,
};

QString animationTitle(LedAnimation animation)
{
    switch (animation) {
    case LedAnimation::Static:    return WheelbaseLedPage::tr("Static");
    case LedAnimation::Breathing: return WheelbaseLedPage::tr("Breathing");
    case LedAnimation::Rotating:  return WheelbaseLedPage::tr("Rotating");
    case LedAnimation::Blinking:  return WheelbaseLedPage::tr("Blinking");
    }
    return {};
}

/// What one cycle is called, so the speed reads as a thing rather than a number.
QString speedLabel(LedAnimation animation, int periodMs)
{
    const QString seconds = QStringLiteral("%1").arg(periodMs / 1000.0, 0, 'f', 2);
    switch (animation) {
    case LedAnimation::Breathing: return WheelbaseLedPage::tr("%1 s per breath").arg(seconds);
    case LedAnimation::Blinking:  return WheelbaseLedPage::tr("%1 s per blink").arg(seconds);
    case LedAnimation::Rotating:  return WheelbaseLedPage::tr("%1 s per turn").arg(seconds);
    case LedAnimation::Static:    break;
    }
    return WheelbaseLedPage::tr("%1 s per cycle").arg(seconds);
}

/// A round swatch of a mode's colours, fading between them the way the ring does.
///
/// Unblurred, unlike the ring's band: at twenty pixels the diffuser's spread is
/// wider than the whole swatch, and what is wanted here is which colours a mode
/// uses rather than what it looks like on the wheel.
QIcon modeSwatch(const LedModeSettings &mode)
{
    const QList<QColor> colours = toRingColours(mode.colours);
    if (colours.isEmpty())
        return {};

    QPixmap pixmap(kSwatchPx, kSwatchPx);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QPainterPath disc;
    disc.addEllipse(QRectF(0.5, 0.5, kSwatchPx - 1.0, kSwatchPx - 1.0));

    const QPointF centre(kSwatchPx / 2.0, kSwatchPx / 2.0);
    if (colours.size() == 1) {
        painter.fillPath(disc, colours.first());
    } else {
        QConicalGradient fade(centre, 90.0 - kRingStartAngleDeg);
        fade.setStops(ringGradientStops(colours));
        painter.fillPath(disc, fade);
    }

    return QIcon(pixmap);
}

} // namespace

WheelbaseLedPage::WheelbaseLedPage(WheelLedController *controller, AlphaEvoSettings *settings,
                                   const QString &probeError, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_settings(settings)
    , m_probeError(probeError)
{
    buildUi();

    if (m_controller) {
        connect(m_controller, &WheelLedController::activeModeChanged, this,
                &WheelbaseLedPage::onActiveModeChanged);
    }
    if (m_settings) {
        connect(m_settings, &AlphaEvoSettings::ledColoursSent, this,
                &WheelbaseLedPage::onLedColoursSent);
        connect(m_settings, &AlphaEvoSettings::dryRunChanged, this,
                &WheelbaseLedPage::onDryRunChanged);

        if (m_probeError.isEmpty()) {
            connect(m_settings, &AlphaEvoSettings::statusRefreshed, this,
                    &WheelbaseLedPage::loadRingLight);
            connect(m_settings, &AlphaEvoSettings::applyFinished, this,
                    &WheelbaseLedPage::onApplyFinished);
        }
    }
}

void WheelbaseLedPage::buildUi()
{
    auto *layout = new QVBoxLayout(this);

    // --- Status --------------------------------------------------------------
    // First on the page, before the controls it qualifies, so a user about to
    // pick colours knows up front whether they are going anywhere.
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("ledStatus"));
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    layout->addSpacing(12);

    auto *row = new QHBoxLayout;

    // --- Ring ----------------------------------------------------------------
    // The whole control, count and all, is the widget. This page keeps no colour
    // state of its own: the controller is the model, and the ring is read on
    // every edit.
    m_ringBox = new QGroupBox(this);
    m_ringBox->setObjectName(QStringLiteral("ledRingBox"));
    auto *ringLayout = new QVBoxLayout(m_ringBox);

    m_ring = new LedRing(m_ringBox);
    m_ring->setObjectName(QStringLiteral("ringColours"));

    // Filling its box rather than sitting at its size hint between two
    // stretches. The ring draws the largest centred circle it can, so giving it
    // the whole box is what centres it — and it is the control being clicked,
    // so the page's spare height belongs to it rather than to empty box.
    //
    // Set here rather than on the widget: LedRing is meant to be reusable, and
    // whether it should grow is the layout's business, not its own.
    m_ring->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ringLayout->addWidget(m_ring);

    row->addWidget(m_ringBox, 1);

    // --- Modes ---------------------------------------------------------------
    // An accordion rather than a second row of tabs: the window already has a
    // sidebar and a tab bar, and a third thing that looks like navigation would
    // be the one that gets misread. It also grows down the page as modes are
    // added, which is why it is on this side of the ring.
    auto *column = new QVBoxLayout;

    // The switch for the whole ring sits above the modes it governs: with it
    // off, none of them show.
    m_ringLight = new QCheckBox(tr("Ring light enabled"), this);
    m_ringLight->setObjectName(QStringLiteral("ringLightToggle"));
    column->addWidget(m_ringLight);

    auto *brightnessRow = new QHBoxLayout;
    brightnessRow->addWidget(new QLabel(tr("Ring light brightness"), this));

    const SettingSpec *brightnessSpec = specFor(Setting::RingLightBrightness);
    m_ringBrightness = new QSlider(Qt::Horizontal, this);
    m_ringBrightness->setObjectName(QStringLiteral("ringLightBrightness"));
    m_ringBrightness->setRange(brightnessSpec->minimum, brightnessSpec->maximum);
    brightnessRow->addWidget(m_ringBrightness, 1);

    m_ringBrightnessValue = new QLabel(this);
    m_ringBrightnessValue->setObjectName(QStringLiteral("ringLightBrightnessValue"));
    // Wide enough for "100" so the slider does not shift as the number grows.
    m_ringBrightnessValue->setMinimumWidth(
        m_ringBrightnessValue->fontMetrics().horizontalAdvance(QStringLiteral("100")));
    m_ringBrightnessValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    brightnessRow->addWidget(m_ringBrightnessValue);

    column->addLayout(brightnessRow);

    m_ringLightCommit = new QTimer(this);
    m_ringLightCommit->setSingleShot(true);
    m_ringLightCommit->setInterval(kRingLightCommitMs);
    connect(m_ringLightCommit, &QTimer::timeout, this, &WheelbaseLedPage::commitRingLight);

    m_modes = new QToolBox(this);
    m_modes->setObjectName(QStringLiteral("ledModes"));

    for (const LedModeSpec &spec : ledModeSpecs()) {
        QWidget *widget = buildSection(spec);
        // The title and the swatch are filled in by refreshSectionHeaders(),
        // which has to run whenever the live mode or a mode's colours change and
        // so is the one place that writes them.
        m_sections[int(spec.mode)].index = m_modes->addItem(widget, QString());
    }

    connect(m_modes, &QToolBox::currentChanged, this, &WheelbaseLedPage::onSectionChanged);

    column->addWidget(m_modes, 1);
    row->addLayout(column, 1);
    layout->addLayout(row, 1);

    // --- Preview clock -------------------------------------------------------
    // Started by showEvent, so a page nobody is looking at costs nothing.
    m_animation = new QTimer(this);
    m_animation->setInterval(kPreviewTickMs);
    connect(m_animation, &QTimer::timeout, this, &WheelbaseLedPage::animate);

    // A probe failure is not the same as being offline. Offline keeps the
    // controls live, because the colours are still the user's to choose and the
    // composed packet is still worth logging. A base that is present but would
    // not open has nothing useful to offer, so the controls are disabled and the
    // status says why.
    if (!m_probeError.isEmpty()) {
        m_ring->setEnabled(false);
        m_modes->setEnabled(false);
        m_ringLight->setEnabled(false);
        m_ringBrightness->setEnabled(false);
    }
    if (!m_settings) {
        m_ringLight->setEnabled(false);
        m_ringBrightness->setEnabled(false);
    }
    loadRingLight();

    // The accordion opens on whichever mode the controller was already editing,
    // which on a fresh page is Idle.
    if (m_controller) {
        const Section section = m_sections.value(int(m_controller->editingMode()));
        if (section.index >= 0)
            m_modes->setCurrentIndex(section.index);
        for (const LedModeSpec &spec : ledModeSpecs())
            loadSection(spec.mode);
        loadRing();
    }

    refreshSectionHeaders();
    setStatus(statusText(), !m_probeError.isEmpty());

    connect(m_ring, &LedRing::coloursChanged, this, &WheelbaseLedPage::onRingColoursChanged);
    connect(m_ringLight, &QCheckBox::toggled, this,
            [this](bool checked) { queueRingLight(Setting::RingLight, checked ? 1 : 0); });
    connect(m_ringBrightness, &QSlider::valueChanged, this, [this](int value) {
        m_ringBrightnessValue->setNum(value);
        queueRingLight(Setting::RingLightBrightness, value);
    });
    // Dragging queues without writing; letting go writes once.
    connect(m_ringBrightness, &QSlider::sliderReleased, this,
            &WheelbaseLedPage::commitRingLight);
}

QWidget *WheelbaseLedPage::buildSection(const LedModeSpec &spec)
{
    const QString key = QString::fromLatin1(spec.key);

    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);

    auto *blurb = new QLabel(ledModeBlurb(spec.mode), widget);
    blurb->setWordWrap(true);
    blurb->setEnabled(false);
    layout->addWidget(blurb);

    Section section;

    // --- Animation -----------------------------------------------------------
    auto *animationRow = new QHBoxLayout;
    animationRow->addWidget(new QLabel(tr("Animation"), widget));

    section.animation = new QComboBox(widget);
    section.animation->setObjectName(QStringLiteral("ledAnimation_%1").arg(key));
    for (LedAnimation animation : kAnimationOrder)
        section.animation->addItem(animationTitle(animation), int(animation));

    // Rotating is offered and refused. Hiding it would leave no sign that it is
    // coming; letting it be picked would give a mode that silently does nothing.
    const int rotating = section.animation->findData(int(LedAnimation::Rotating));
    if (auto *model = qobject_cast<QStandardItemModel *>(section.animation->model())) {
        if (QStandardItem *item = model->item(rotating))
            item->setEnabled(false);
    }
    section.animation->setItemData(rotating,
                                   tr("Not supported yet."),
                                   Qt::ToolTipRole);

    animationRow->addWidget(section.animation, 1);
    layout->addLayout(animationRow);

    // --- Speed ---------------------------------------------------------------
    // Its own widget so the whole row can be hidden for Static, which has
    // nothing to pace. A disabled slider would have said "there is a speed here
    // and you may not have it", which is not what is meant.
    section.speedRow = new QWidget(widget);
    auto *speedLayout = new QHBoxLayout(section.speedRow);
    speedLayout->setContentsMargins(0, 0, 0, 0);
    speedLayout->addWidget(new QLabel(tr("Speed"), section.speedRow));

    section.speed = new QSlider(Qt::Horizontal, section.speedRow);
    section.speed->setObjectName(QStringLiteral("ledSpeed_%1").arg(key));
    section.speed->setRange(0, kLedSpeedSliderMax);
    section.speed->setMinimumWidth(120);
    speedLayout->addWidget(section.speed, 1);

    section.speedValue = new QLabel(section.speedRow);
    section.speedValue->setObjectName(QStringLiteral("ledSpeedValue_%1").arg(key));
    speedLayout->addWidget(section.speedValue);

    layout->addWidget(section.speedRow);
    layout->addStretch(1);

    const LedMode mode = spec.mode;
    connect(section.animation, &QComboBox::currentIndexChanged, this,
            [this, mode] { commitSection(mode); });
    connect(section.speed, &QSlider::valueChanged, this, [this, mode] { commitSection(mode); });

    m_sections.insert(int(mode), section);
    return widget;
}

// ---------------------------------------------------------------------------
// Model in, model out
// ---------------------------------------------------------------------------

void WheelbaseLedPage::commitSection(LedMode mode)
{
    if (m_loading || !m_controller)
        return;

    const Section section = m_sections.value(int(mode));
    if (!section.animation || !section.speed)
        return;

    LedModeSettings settings = m_controller->mode(mode);
    settings.animation = static_cast<LedAnimation>(section.animation->currentData().toInt());
    settings.periodMs = quint16(sliderToPeriodMs(section.speed->value()));

    m_controller->setMode(mode, settings);

    // The slider's label and the speed row's visibility both follow the value
    // that was just committed, so they are refreshed from the model rather than
    // from the widgets that changed.
    loadSection(mode);
    refreshSectionHeaders();
    setStatus(statusText());
}

void WheelbaseLedPage::loadSection(LedMode mode)
{
    if (!m_controller)
        return;

    const Section section = m_sections.value(int(mode));
    if (!section.animation || !section.speed)
        return;

    const LedModeSettings &settings = m_controller->mode(mode);

    const QSignalBlocker animationBlocker(section.animation);
    const QSignalBlocker speedBlocker(section.speed);

    const int index = section.animation->findData(int(settings.animation));
    if (index >= 0)
        section.animation->setCurrentIndex(index);

    section.speed->setValue(periodMsToSlider(settings.periodMs));
    section.speedValue->setText(speedLabel(settings.animation, settings.periodMs));
    section.speedRow->setVisible(ledAnimationIsPaced(settings.animation));
}

void WheelbaseLedPage::loadRing()
{
    if (!m_controller || !m_ring)
        return;

    const LedMode mode = m_controller->editingMode();

    m_loading = true;
    m_ring->setColours(toRingColours(m_controller->mode(mode).colours));
    m_loading = false;

    // Named, so it is never in doubt which mode the ring on the left belongs to
    // the section on the right.
    m_ringBox->setTitle(tr("Colours — %1").arg(ledModeTitle(mode)));

    // Straight away rather than on the next tick, so switching mode does not
    // leave the previous mode's frame on screen for a frame or two.
    animate();
}

void WheelbaseLedPage::onRingColoursChanged()
{
    if (m_loading || !m_controller)
        return;

    const LedMode mode = m_controller->editingMode();
    LedModeSettings settings = m_controller->mode(mode);
    settings.colours = toWireColours(m_ring->colours());

    m_controller->setMode(mode, settings);
    refreshSectionHeaders();
}

void WheelbaseLedPage::onSectionChanged(int index)
{
    if (!m_controller)
        return;

    for (auto it = m_sections.cbegin(); it != m_sections.cend(); ++it) {
        if (it.value().index != index)
            continue;

        const auto mode = static_cast<LedMode>(it.key());
        m_controller->setEditingMode(mode);
        loadSection(mode);
        loadRing();
        setStatus(statusText());
        return;
    }
}

void WheelbaseLedPage::refreshSectionHeaders()
{
    if (!m_controller)
        return;

    for (const LedModeSpec &spec : ledModeSpecs()) {
        const Section section = m_sections.value(int(spec.mode));
        if (section.index < 0)
            continue;

        // The marker goes in the item's own text so it is readable with the
        // section collapsed, which is where most modes are most of the time.
        const QString title = ledModeTitle(spec.mode);
        m_modes->setItemText(section.index, m_controller->activeMode() == spec.mode
                                                ? tr("%1  •  on the wheel now").arg(title)
                                                : title);
        m_modes->setItemIcon(section.index, modeSwatch(m_controller->mode(spec.mode)));
    }
}

// ---------------------------------------------------------------------------
// Ring light on/off
// ---------------------------------------------------------------------------

void WheelbaseLedPage::queueRingLight(Setting setting, int value)
{
    m_ringLightPending.insert(setting, value);

    // Mid-drag there is no point writing: sliderReleased will do it once.
    if (m_ringBrightness->isSliderDown())
        return;

    // A click on the box is one deliberate edit and goes straight out; the
    // slider's keyboard and wheel steps settle first.
    if (setting == Setting::RingLight)
        commitRingLight();
    else
        m_ringLightCommit->start();
}

void WheelbaseLedPage::commitRingLight()
{
    m_ringLightCommit->stop();
    if (m_ringLightPending.isEmpty() || !m_settings)
        return;

    if (m_settings->isBusy()) {
        // Another page's write is in flight; hold the edit rather than drop it.
        m_ringLightCommit->start();
        return;
    }

    const SettingValues changes = m_ringLightPending;
    m_ringLightPending.clear();
    m_settings->applyAsync(changes);
}

void WheelbaseLedPage::loadRingLight()
{
    if (!m_settings || !m_ringLight)
        return;

    // Silent, so a value read back is never mistaken for an edit to write. Each
    // control is skipped while it has an edit of its own still to go.
    if (!m_ringLightPending.contains(Setting::RingLight)) {
        const QSignalBlocker blocker(m_ringLight);
        m_ringLight->setChecked(m_settings->value(Setting::RingLight) != 0);
    }
    if (!m_ringLightPending.contains(Setting::RingLightBrightness)
        && !m_ringBrightness->isSliderDown()) {
        const QSignalBlocker blocker(m_ringBrightness);
        const int brightness = m_settings->value(Setting::RingLightBrightness);
        m_ringBrightness->setValue(brightness);
        m_ringBrightnessValue->setNum(brightness);
    }
}

void WheelbaseLedPage::onApplyFinished(bool ok, const QString &error, int blocksWritten,
                                       bool dryRun)
{
    Q_UNUSED(error);
    Q_UNUSED(blocksWritten);
    Q_UNUSED(dryRun);

    // The controls are not disabled for the length of a write: that would take
    // the keyboard focus off the slider being adjusted. commitRingLight() already
    // holds an edit back while a write is in flight, so nothing overlaps.

    // A failed write leaves the status unchanged, so no refresh will come to
    // put the box back to what the base actually has.
    if (!ok)
        loadRingLight();
}

// ---------------------------------------------------------------------------
// The preview
// ---------------------------------------------------------------------------

void WheelbaseLedPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

    if (m_controller)
        m_controller->setPreviewing(true);
    if (m_animation)
        m_animation->start();

    animate();
    setStatus(statusText(), !m_probeError.isEmpty() || !m_writeError.isEmpty());
}

void WheelbaseLedPage::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);

    // Both directions, so a preview cannot outlive the tab being looked at and a
    // page nobody has open costs nothing to keep.
    if (m_controller)
        m_controller->setPreviewing(false);
    if (m_animation)
        m_animation->stop();
}

void WheelbaseLedPage::animate()
{
    if (!m_controller || !m_ring)
        return;

    m_ring->setPreviewColours(toRingColours(m_controller->previewFrame()));
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

void WheelbaseLedPage::onActiveModeChanged(LedMode mode)
{
    Q_UNUSED(mode);
    refreshSectionHeaders();
    setStatus(statusText());
}

void WheelbaseLedPage::onLedColoursSent(bool ok, const QString &error, int framesSent,
                                        int framesTotal, bool dryRun)
{
    Q_UNUSED(dryRun);

    if (ok) {
        m_writeError.clear();
    } else if (framesSent > 0 && framesSent < framesTotal) {
        // Materially different from sending nothing: the wheel has part of a
        // message and there is no read-back to find out what it did with it.
        m_writeError = tr("Sent %1 of %2 LED reports: %3. The wheel may not match what is shown.")
                           .arg(framesSent)
                           .arg(framesTotal)
                           .arg(error);
    } else {
        m_writeError = tr("Could not set the LED colours: %1").arg(error);
    }

    setStatus(statusText(), !m_writeError.isEmpty());
}

void WheelbaseLedPage::onDryRunChanged(bool dryRun)
{
    Q_UNUSED(dryRun);
    if (m_ring && m_ring->isEnabled())
        setStatus(statusText());
}

QString WheelbaseLedPage::statusText() const
{
    // A base that would not open has nothing else worth saying about it, and a
    // standing write failure means that whatever the wheel is meant to be
    // showing, it is not showing it. Either outranks the rest of this.
    if (!m_probeError.isEmpty())
        return m_probeError;
    if (!m_writeError.isEmpty())
        return m_writeError;

    QString base;
    if (!m_settings) {
        base = tr("Preview only — these colours are not sent to the wheel.");
    } else if (m_settings->isOffline()) {
        // Offline first: it is always in a dry run, but "there is nothing there"
        // is the more useful of the two truths.
        base = tr("Not connected: colours are logged only.");
    } else if (m_settings->isDryRun()) {
        base = tr("Dry run: LED writes are logged only, so the wheel keeps its own colours.");
    } else {
        // "Held", not "sent": the base drops LED colours seconds after it stops
        // being told them, so this page is restating them for as long as it is
        // open rather than writing once and leaving.
        base = tr("These colours are held on the wheel while the app is running.");
    }

    // The one thing that needs explaining: the ring is an editor and the wheel
    // is not obliged to agree with it.
    if (m_controller && m_controller->isOverridden()) {
        base += QLatin1Char(' ');
        base += tr("The wheel is showing %1 right now.")
                    .arg(ledModeTitle(m_controller->activeMode()));
    }

    return base;
}

void WheelbaseLedPage::setStatus(const QString &text, bool warning)
{
    if (!m_status)
        return;
    m_status->setText(warning ? QStringLiteral("<b>%1</b>").arg(text.toHtmlEscaped()) : text);
}

} // namespace hidcenter::simagic_alpha_evo
