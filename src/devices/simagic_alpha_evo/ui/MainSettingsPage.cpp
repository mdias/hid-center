// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MainSettingsPage.h"

#include "core/DeveloperMode.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace hidcenter::simagic_alpha_evo {
namespace {

/// Keyboard and mouse-wheel changes have no "released" event to write on, so
/// they settle for this long first. Long enough to coalesce a run of arrow-key
/// presses, short enough to feel immediate.
constexpr int kCommitDebounceMs = 250;

} // namespace

int MainSettingsPage::Control::value() const
{
    switch (kind) {
    case ControlKind::Slider:
        return slider ? slider->value() : 0;
    case ControlKind::Choice:
        return combo ? combo->currentData().toInt() : 0;
    case ControlKind::Toggle:
        return check && check->isChecked() ? 1 : 0;
    }
    return 0;
}

void MainSettingsPage::Control::setValueSilently(int value) const
{
    switch (kind) {
    case ControlKind::Slider: {
        const QSignalBlocker sliderBlocker(slider);
        const QSignalBlocker spinBlocker(spin);
        slider->setValue(value);
        spin->setValue(value);
        break;
    }
    case ControlKind::Choice: {
        const QSignalBlocker blocker(combo);
        const int index = combo->findData(value);
        if (index >= 0)
            combo->setCurrentIndex(index);
        break;
    }
    case ControlKind::Toggle: {
        const QSignalBlocker blocker(check);
        check->setChecked(value != 0);
        break;
    }
    }
}

bool MainSettingsPage::Control::isBusy() const
{
    switch (kind) {
    case ControlKind::Slider:
        return slider->isSliderDown() || spin->hasFocus();
    case ControlKind::Choice:
        // A combo with its popup open must not have the selection pulled out
        // from under the user mid-choice.
        return combo->view() && combo->view()->isVisible();
    case ControlKind::Toggle:
        return false;
    }
    return false;
}

MainSettingsPage::MainSettingsPage(const QString &modelName, AlphaEvoSettings *settings,
                                   const QString &probeError, QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
{
    Q_UNUSED(modelName); // The device name lives in the header, above the tabs.

    m_commitTimer = new QTimer(this);
    m_commitTimer->setSingleShot(true);
    m_commitTimer->setInterval(kCommitDebounceMs);
    connect(m_commitTimer, &QTimer::timeout, this, &MainSettingsPage::commitPending);

    auto *layout = new QVBoxLayout(this);

    // --- Dry run -----------------------------------------------------------
    m_dryRun = new QCheckBox(tr("Dry run — log writes instead of sending them"), this);
    m_dryRun->setObjectName(QStringLiteral("dryRunToggle"));
    m_dryRun->setToolTip(tr("While ticked, changes are only logged, not sent. Untick it to "
                            "configure the wheelbase."));

    // State before the handler, so initialising the box cannot run a slot that
    // touches widgets this constructor has not built yet.
    m_dryRun->setChecked(m_settings->isDryRun());

    connect(m_dryRun, &QCheckBox::toggled, this, [this](bool checked) {
        m_settings->setDryRun(checked);
        // After setDryRun(), so the line describes the state that now holds
        // rather than re-deriving it from the box that was just clicked.
        setStatus(idleStatusText());
    });
    // Still built when hidden: the offline branch below drives it, and the
    // settings object is the one holding the actual gate.
    if (hidcenter::developerMode())
        layout->addWidget(m_dryRun);
    else
        m_dryRun->hide();

    if (m_settings->isOffline()) {
        // The gate is moot with no device on the other end, and an unticked box
        // would promise a write that cannot happen.
        //
        // Told to the settings object explicitly rather than left to its default.
        // While the default was a dry run this line was a no-op that happened to
        // look correct; now it is the only thing making the box and the gate
        // agree.
        m_settings->setDryRun(true);

        // Blocked because this is initialising a control, not a user ticking it,
        // and the handler above writes to a status label built further down this
        // constructor — it does not exist yet.
        const QSignalBlocker blocker(m_dryRun);
        m_dryRun->setChecked(true);
        m_dryRun->setEnabled(false);
        m_dryRun->setToolTip(
            tr("No wheelbase connected: changes are logged and kept here only."));
    }

    auto *hint = new QLabel(
        m_settings->isOffline()
            ? tr("Wheelbase not connected. Changes are logged on the Home page, but nothing "
                 "is sent or read back.")
            : tr("Settings are read from the wheelbase continuously, so changes made "
                 "elsewhere appear here too. Writes are logged on the Home page."),
        this);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    layout->addWidget(hint);

    // --- Controls ----------------------------------------------------------
    // Seventeen controls do not fit a window, so the groups scroll. They are
    // arranged the way the protocol groups them, which is also how they behave.
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    m_content = new QWidget(scroll);
    auto *columns = new QHBoxLayout(m_content);

    auto *left = new QVBoxLayout;
    left->addWidget(buildGroup(tr("Force feedback"),
                               {Setting::FfStrength, Setting::MechanicalCentering,
                                Setting::MechanicalDamper, Setting::MechanicalFriction,
                                Setting::MechanicalInertia, Setting::CenterDamper}));
    left->addWidget(buildGroup(tr("Game effects"),
                               {Setting::GameCentering, Setting::GameInertia, Setting::GameDamper,
                                Setting::GameFriction}));
    left->addStretch(1);

    auto *right = new QVBoxLayout;
    right->addWidget(buildGroup(tr("Rotation"),
                                {Setting::MaxAngle, Setting::AngleLock,
                                 Setting::AngleLockStrength, Setting::WheelRotationSpeed,
                                 Setting::SteeringTorqueAssist}));
    right->addWidget(buildGroup(tr("Signal"), {Setting::DynamicPredictionLevel,
                                               Setting::FeedbackDetail, Setting::FilterLevel,
                                               Setting::SlewRateControl}));
    right->addStretch(1);

    columns->addLayout(left, 1);
    columns->addLayout(right, 1);

    scroll->setWidget(m_content);
    layout->addWidget(scroll, 1);

    // --- Status ------------------------------------------------------------
    auto *statusRow = new QHBoxLayout;

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    statusRow->addWidget(m_status, 1);

    // Indeterminate: a write is a handful of ioctls with no progress to report,
    // and the only useful thing to say is that one is still going.
    m_progress = new QProgressBar(this);
    m_progress->setObjectName(QStringLiteral("writeProgress"));
    m_progress->setRange(0, 0);
    m_progress->setTextVisible(false);
    m_progress->setFixedWidth(90);
    m_progress->setMaximumHeight(m_status->sizeHint().height());
    m_progress->setVisible(false);
    statusRow->addWidget(m_progress);

    layout->addLayout(statusRow);

    if (!probeError.isEmpty()) {
        // Nothing can be read or written, so leave the controls visible — they
        // document what this base can do — but inert.
        m_content->setEnabled(false);
        m_dryRun->setEnabled(false);
        setStatus(tr("The wheelbase did not answer, so its settings cannot be shown or "
                     "changed.\n\n%1\n\nThe Protocol Analyzer tab still works.")
                      .arg(probeError),
                  true);
        return;
    }

    m_operable = true;

    connect(m_settings, &AlphaEvoSettings::statusRefreshed, this,
            &MainSettingsPage::onStatusRefreshed);
    connect(m_settings, &AlphaEvoSettings::readFailed, this, &MainSettingsPage::onReadFailed);
    connect(m_settings, &AlphaEvoSettings::applyStarted, this, &MainSettingsPage::onApplyStarted);
    connect(m_settings, &AlphaEvoSettings::applyFinished, this,
            &MainSettingsPage::onApplyFinished);

    onStatusRefreshed();
    setStatus(idleStatusText());
}

QString MainSettingsPage::idleStatusText() const
{
    // Offline first: it is always in a dry run, but "there is nothing there" is
    // the more useful of the two truths.
    if (m_settings->isOffline())
        return tr("Not connected: changes are kept here only.");

    return m_settings->isDryRun() ? tr("Dry run: writes are logged only.")
                                  : tr("Changes are sent to the wheelbase.");
}

QWidget *MainSettingsPage::buildGroup(const QString &title, const QVector<Setting> &settings)
{
    auto *box = new QGroupBox(title, this);
    auto *form = new QFormLayout(box);
    for (Setting setting : settings)
        addControl(form, box, setting);
    return box;
}

void MainSettingsPage::addControl(QFormLayout *form, QWidget *parent, Setting setting)
{
    const SettingSpec *spec = specFor(setting);
    if (!spec)
        return;

    switch (spec->kind) {
    case ControlKind::Slider:
        addSlider(form, parent, *spec);
        break;
    case ControlKind::Choice:
        addChoice(form, parent, *spec);
        break;
    case ControlKind::Toggle:
        addToggle(form, parent, *spec);
        break;
    }
}

void MainSettingsPage::addSlider(QFormLayout *form, QWidget *parent, const SettingSpec &spec)
{
    auto *row = new QWidget(parent);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    auto *slider = new QSlider(Qt::Horizontal, row);
    slider->setRange(spec.minimum, spec.maximum);
    slider->setMinimumWidth(140);
    rowLayout->addWidget(slider, 1);

    auto *spin = new QSpinBox(row);
    spin->setRange(spec.minimum, spec.maximum);
    spin->setMinimumWidth(70);
    rowLayout->addWidget(spin);

    // The slider is the control; the spin box exists so the exact value is both
    // visible and typeable, which a slider alone is not.
    const Setting setting = spec.id;
    connect(slider, &QSlider::valueChanged, this, [this, setting, spin](int value) {
        const QSignalBlocker blocker(spin);
        spin->setValue(value);
        queueChange(setting, value);
    });
    connect(spin, &QSpinBox::valueChanged, this, [this, setting, slider](int value) {
        const QSignalBlocker blocker(slider);
        slider->setValue(value);
        queueChange(setting, value);
    });
    // Dragging queues without writing; letting go writes once.
    connect(slider, &QSlider::sliderReleased, this, &MainSettingsPage::commitPending);

    Control control;
    control.kind = ControlKind::Slider;
    control.slider = slider;
    control.spin = spin;
    m_controls.insert(setting, control);
    form->addRow(tr(spec.label), row);
}

void MainSettingsPage::addChoice(QFormLayout *form, QWidget *parent, const SettingSpec &spec)
{
    auto *combo = new QComboBox(parent);
    for (int value = spec.minimum; value <= spec.maximum; ++value) {
        const char *label = spec.choices ? spec.choices[value - spec.minimum] : nullptr;
        combo->addItem(label ? tr(label) : QString::number(value), value);
    }

    const Setting setting = spec.id;
    connect(combo, &QComboBox::currentIndexChanged, this, [this, setting, combo] {
        queueChange(setting, combo->currentData().toInt());
    });

    Control control;
    control.kind = ControlKind::Choice;
    control.combo = combo;
    m_controls.insert(setting, control);
    form->addRow(tr(spec.label), combo);
}

void MainSettingsPage::addToggle(QFormLayout *form, QWidget *parent, const SettingSpec &spec)
{
    auto *check = new QCheckBox(tr("Enabled"), parent);

    const Setting setting = spec.id;
    connect(check, &QCheckBox::toggled, this,
            [this, setting](bool checked) { queueChange(setting, checked ? 1 : 0); });

    Control control;
    control.kind = ControlKind::Toggle;
    control.check = check;
    m_controls.insert(setting, control);
    form->addRow(tr(spec.label), check);
}

void MainSettingsPage::updateAngleLockRange()
{
    const Control lock = m_controls.value(Setting::AngleLock);
    if (!lock.slider)
        return;

    // Prefer an unwritten edit to the maximum rotation over the device's current
    // value, so the lock's ceiling tracks the slider the user is moving now.
    const int maximum = m_pending.contains(Setting::MaxAngle)
                            ? m_pending.value(Setting::MaxAngle)
                            : m_settings->effectiveMaximum(Setting::AngleLock);

    if (lock.slider->maximum() == maximum)
        return;

    const QSignalBlocker sliderBlocker(lock.slider);
    const QSignalBlocker spinBlocker(lock.spin);
    lock.slider->setMaximum(maximum);
    lock.spin->setMaximum(maximum);
}

void MainSettingsPage::queueChange(Setting setting, int value)
{
    m_pending.insert(setting, value);

    if (setting == Setting::MaxAngle)
        updateAngleLockRange();

    // Mid-drag there is no point writing: sliderReleased will do it once.
    const Control control = m_controls.value(setting);
    if (control.kind == ControlKind::Slider && control.slider->isSliderDown())
        return;

    m_commitTimer->start();
}

void MainSettingsPage::commitPending()
{
    m_commitTimer->stop();
    if (m_pending.isEmpty())
        return;

    if (m_settings->isBusy()) {
        // The debounce can fire just as a write starts. Hold the edits and try
        // again rather than dropping them or stacking a second write behind it.
        m_commitTimer->start();
        return;
    }

    const SettingValues changes = m_pending;
    m_pending.clear();

    m_settings->applyAsync(changes);
}

void MainSettingsPage::onApplyStarted()
{
    // An offline apply finishes inside this same call, so there is no interval
    // to lock the page for and nothing to lock it against.
    if (m_settings->isOffline())
        return;

    setBusy(true);
    // A dry run still reads the wheelbase — it has to, to compose from a current
    // status — so it is not instantaneous and should not claim to be.
    setStatus(m_settings->isDryRun() ? tr("Dry run: reading the wheelbase…")
                                     : tr("Writing to the wheelbase…"));
}

void MainSettingsPage::onApplyFinished(bool ok, const QString &error, int blocksWritten,
                                       bool dryRun)
{
    setBusy(false);

    if (!ok) {
        // The edits are dropped rather than retried: something that has already
        // failed once should not be sent again behind the user's back.
        //
        // Put the device's values back by hand. Waiting for the poll would not
        // do it — a failed write leaves the status unchanged, so no refresh is
        // ever emitted, and the controls would go on showing a value the
        // wheelbase never took.
        setStatus(tr("Could not write settings: %1").arg(error), true);
        onStatusRefreshed();
        return;
    }

    // A successful dry run deliberately leaves the controls where the user put
    // them: nothing was written, but seeing what you would have set is the whole
    // point of the mode, and snapping back after every edit would defeat it.
    if (m_settings->isOffline()) {
        setStatus(tr("Not connected: %n settings block(s) logged, nothing sent.", nullptr,
                     blocksWritten));
    } else if (dryRun) {
        setStatus(tr("Dry run: %n settings block(s) logged, nothing sent.", nullptr,
                     blocksWritten));
    } else {
        setStatus(tr("Wrote %n settings block(s).", nullptr, blocksWritten));
    }
}

void MainSettingsPage::setBusy(bool busy)
{
    if (!m_operable)
        return;

    // Disabling the whole content widget is what stops a second edit arriving
    // while a write is in flight, which is the point: two overlapping
    // read-modify-writes would each undo part of the other.
    m_content->setEnabled(!busy);
    // Offline, the box is permanently ticked and disabled; re-enabling it after a
    // write would quietly offer a mode that does not exist here.
    m_dryRun->setEnabled(!busy && !m_settings->isOffline());
    m_progress->setVisible(busy);
}

void MainSettingsPage::onStatusRefreshed()
{
    updateAngleLockRange();

    for (auto it = m_controls.cbegin(); it != m_controls.cend(); ++it) {
        const Setting setting = it.key();
        const Control &control = it.value();

        // Do not fight the user: skip anything being manipulated and anything
        // with an edit that has not been written yet.
        if (control.isBusy() || m_pending.contains(setting))
            continue;

        const int value = m_settings->value(setting);
        if (control.value() == value)
            continue;

        // Silent, so refreshing the display never looks like a user edit and
        // cannot queue a write of the value we just read.
        control.setValueSilently(value);
    }
}

void MainSettingsPage::onReadFailed(const QString &error)
{
    setStatus(tr("Lost contact with the wheelbase: %1").arg(error), true);
}

void MainSettingsPage::setStatus(const QString &message, bool isError)
{
    m_status->setText(message);
    m_status->setStyleSheet(isError ? QStringLiteral("color: #c03030;") : QString());
}

} // namespace hidcenter::simagic_alpha_evo
