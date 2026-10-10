// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MainSettingsPage.h"

#include "../BrakeOptionsRead.h"
#include "../ConfigRead.h"
#include "CurveEditor.h"
#include "../PedalInput.h"
#include "../Pedals.h"
#include "RangeSlider.h"

#include "core/Logging.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <array>

#include <QStringList>

namespace hidcenter::simsonn_pedals {
namespace {

/// The deadzone slider's scale, and the smallest travel that can be left between
/// its thumbs. Percentages of the pedal's calibrated travel, which is what the
/// device stores them as.
constexpr int kDeadzoneMinimum = 0;
constexpr int kDeadzoneMaximum = 100;
constexpr int kDeadzoneLeastTravel = 1;

/// A curve's points on one line, for the log.
QString curveText(const QVector<double> &values)
{
    QStringList parts;
    parts.reserve(values.size());
    for (const double value : values)
        parts << QString::number(qRound(value));
    return parts.join(QLatin1Char(' '));
}

/// The filter level a pedal gets when its box is ticked with no level of its own
/// to go back to.
constexpr int kDefaultFilter = 2;

/// The throttle cut point, stored in tenths of a percent, as it is shown.
QString cutPointText(int tenths)
{
    return MainSettingsPage::tr("%1 %").arg(tenths / 10.0, 0, 'f', 1);
}

} // namespace

/// One pedal's settings: where its travel starts and stops counting, and what it
/// does with the part in between.
QGroupBox *MainSettingsPage::pedalBox(const QString &title, bool brake, Row *row)
{
    auto *box = new QGroupBox(title, this);
    auto *form = new QFormLayout(box);

    // One slider and not two spin boxes, because these are two ends of one
    // range: the pedal registers nothing below the low thumb and is at full
    // output above the high one. Two numbers that can be set past each other
    // would allow a pedal with no travel at all.
    auto *deadzones = new RangeSlider(box);
    deadzones->setScale(kDeadzoneMinimum, kDeadzoneMaximum);
    deadzones->setMinimumSeparation(kDeadzoneLeastTravel);
    deadzones->setValues(kDeadzoneMinimum, kDeadzoneMaximum);
    deadzones->setToolTip(tr(
        "Where the pedal starts and stops registering. Below the low thumb it reports "
        "0 %, above the high thumb 100 %, and the response curve maps the travel "
        "between them."));
    form->addRow(tr("Deadzones"), deadzones);
    row->deadzones = deadzones;

    // The box switches filtering on and off, and the slider beside it is how
    // much: clearing the box stores 0, and ticking it again brings back the last
    // level, 2 if there was none. The figure is the level itself.
    auto *filterEnabled = new QCheckBox(box);
    auto *filter = new QSlider(Qt::Horizontal, box);
    filter->setRange(1, ConfigRead::kFilterMaximum);
    filter->setPageStep(1);
    filter->setTickPosition(QSlider::TicksBelow);
    filter->setTickInterval(1);
    filter->setValue(kDefaultFilter);
    auto *filterText = new QLabel(QString::number(kDefaultFilter), box);
    filterText->setMinimumWidth(
        filterText->fontMetrics().horizontalAdvance(QString::number(ConfigRead::kFilterMaximum)));
    filterText->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    const QString filterTip = tr(
        "Smooths the pedal's input, from 1 to %1; clear the box for none. More filtering "
        "steadies a noisy sensor but adds a little lag.\n\n"
        "Saving a change rewrites the pedals' whole configuration.")
                                  .arg(ConfigRead::kFilterMaximum);
    filterEnabled->setToolTip(filterTip);
    filter->setToolTip(filterTip);
    filterText->setToolTip(filterTip);
    auto *filtering = new QHBoxLayout;
    filtering->setContentsMargins(0, 0, 0, 0);
    filtering->addWidget(filterEnabled);
    filtering->addWidget(filter, 1);
    filtering->addWidget(filterText);
    form->addRow(tr("Filtering"), filtering);
    row->filterEnabled = filterEnabled;
    row->filter = filter;
    row->filterText = filterText;
    connect(filterEnabled, &QCheckBox::toggled, this, [this, filter, filterText](bool on) {
        filter->setEnabled(on);
        filterText->setEnabled(on);
        refreshButtons();
    });
    connect(filter, &QSlider::valueChanged, this, [this, filterText](int level) {
        filterText->setText(QString::number(level));
        refreshButtons();
    });

    auto *curve = new CurveEditor(box);
    form->addRow(curve);
    row->curve = curve;
    connect(curve, &CurveEditor::changed, this, [this, title] {
        Q_EMIT curveChanged(title);
        refreshButtons();
    });
    connect(deadzones, &RangeSlider::valuesChanged, this, &MainSettingsPage::refreshButtons);

    // Below the curve, so the rows every pedal has line up across the boxes.
    if (brake)
        addBrakeRows(form, box);

    row->box = box;
    return box;
}

void MainSettingsPage::addBrakeRows(QFormLayout *form, QWidget *box)
{
    m_force = new QSpinBox(box);
    m_force->setSuffix(tr(" kg"));
    m_force->setRange(kBrakeForceMinimumKg, brakeForceMaximumKg(Elastomer::Hard));
    m_force->setToolTip(tr(
        "The force at which the brake reaches full output. Calibrating the brake sets it "
        "too. The largest allowed force depends on the elastomer."));
    form->addRow(tr("Maximum force"), m_force);
    connect(m_force, &QSpinBox::valueChanged, this, &MainSettingsPage::refreshButtons);

    m_elastomer = new QComboBox(box);
    m_elastomer->addItem(tr("Soft"), QVariant::fromValue(int(Elastomer::Soft)));
    m_elastomer->addItem(tr("Medium"), QVariant::fromValue(int(Elastomer::Medium)));
    m_elastomer->addItem(tr("Hard"), QVariant::fromValue(int(Elastomer::Hard)));
    m_elastomer->setToolTip(tr("The elastomer fitted to the brake. It limits the maximum force: "
                               "%1 kg soft, %2 kg medium, %3 kg hard.")
                                .arg(brakeForceMaximumKg(Elastomer::Soft))
                                .arg(brakeForceMaximumKg(Elastomer::Medium))
                                .arg(brakeForceMaximumKg(Elastomer::Hard)));
    form->addRow(tr("Elastomer"), m_elastomer);
    connect(m_elastomer, &QComboBox::currentIndexChanged, this, [this] {
        refreshForceRange();
        refreshButtons();
    });

    // The box, then the point it switches: the point means nothing with the box
    // clear, and is disabled to say so. A slider rather than
    // a number, because where along the brake's travel it falls is the thing to
    // judge; the figure beside it is for setting it exactly.
    m_cutEnabled = new QCheckBox(box);
    m_cutPoint = new QSlider(Qt::Horizontal, box);
    m_cutPoint->setRange(kThrottleCutMinimum, kThrottleCutMaximum);
    m_cutPoint->setSingleStep(1);
    m_cutPoint->setPageStep(50);
    m_cutPointText = new QLabel(box);
    m_cutPointText->setMinimumWidth(
        m_cutPointText->fontMetrics().horizontalAdvance(cutPointText(kThrottleCutMaximum)));
    m_cutPointText->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    const QString cutTip = tr(
        "When on, pressing the brake past this point cuts the throttle output, so the two "
        "are never applied at once. Range: %1 to %2.")
                               .arg(cutPointText(kThrottleCutMinimum),
                                    cutPointText(kThrottleCutMaximum));
    m_cutEnabled->setToolTip(cutTip);
    m_cutPoint->setToolTip(cutTip);
    m_cutPointText->setToolTip(cutTip);
    auto *cut = new QHBoxLayout;
    cut->setContentsMargins(0, 0, 0, 0);
    cut->addWidget(m_cutEnabled);
    cut->addWidget(m_cutPoint, 1);
    cut->addWidget(m_cutPointText);
    form->addRow(tr("Throttle cut"), cut);
    connect(m_cutEnabled, &QCheckBox::toggled, this, [this](bool on) {
        m_cutPoint->setEnabled(on);
        m_cutPointText->setEnabled(on);
        refreshButtons();
    });
    connect(m_cutPoint, &QSlider::valueChanged, this, [this](int tenths) {
        m_cutPointText->setText(cutPointText(tenths));
        refreshButtons();
    });
    m_cutPointText->setText(cutPointText(m_cutPoint->value()));

    // Until the device has said what they are: a placeholder that could be
    // edited would look like the pedals' own value.
    m_elastomer->setEnabled(false);
    m_cutEnabled->setEnabled(false);
    m_cutPoint->setEnabled(false);
    m_cutPointText->setEnabled(false);
}

MainSettingsPage::MainSettingsPage(const QString &modelName,
                                   const std::optional<UsbInterfaceInfo> &vendorInterface,
                                   PedalInput *pedals, ConfigRead *config,
                                   BrakeOptionsRead *brakeOptions, SettingsSave *save,
                                   QWidget *parent)
    : QWidget(parent)
    , m_pedals(pedals)
    , m_config(config)
    , m_brakeOptions(brakeOptions)
    , m_save(save)
    , m_vendorInterface(vendorInterface)
{
    auto *layout = new QVBoxLayout(this);

    auto *title = new QLabel(QStringLiteral("<h2>%1</h2>").arg(modelName.toHtmlEscaped()), this);
    layout->addWidget(title);

    auto *notice = new QLabel(
        tr("<b>These settings are stored on the pedals.</b> Changes apply only when you "
           "press <i>Save Settings</i>; <i>Cancel</i> restores what the pedals hold. "
           "Changing the brake's maximum force also changes its calibration."),
        this);
    notice->setWordWrap(true);
    layout->addWidget(notice);

    m_configNotice = new QLabel(this);
    m_configNotice->setWordWrap(true);
    m_configNotice->setEnabled(false);
    layout->addWidget(m_configNotice);

    m_positionNotice = new QLabel(this);
    m_positionNotice->setWordWrap(true);
    m_positionNotice->setEnabled(false);
    layout->addWidget(m_positionNotice);

    auto *boxes = new QHBoxLayout;
    for (int i = 0; i < kPedalCount; ++i) {
        Row row;
        boxes->addWidget(pedalBox(tr(kPedals[i].title), i == kBrakePedal, &row));
        m_rows.append(row);
    }
    layout->addLayout(boxes);

    // The same arrangement as on the Calibration page: both buttons always
    // there, only ever enabled and disabled, so nothing that writes to the
    // device can appear under a cursor aimed at something else.
    auto *buttons = new QHBoxLayout;

    m_saveButton = new QPushButton(tr("Save Settings"), this);
    m_saveButton->setToolTip(tr("Save the changes to the pedals and check they were stored."));
    connect(m_saveButton, &QPushButton::clicked, this, [this] {
        if (m_save && m_vendorInterface)
            m_save->save(*m_vendorInterface, current());
    });
    buttons->addWidget(m_saveButton);

    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_cancelButton->setToolTip(tr("Discard the changes and show what the pedals hold."));
    connect(m_cancelButton, &QPushButton::clicked, this, [this] { loadFromDevice(true); });
    buttons->addWidget(m_cancelButton);

    buttons->addStretch(1);
    layout->addLayout(buttons);

    m_saveMessage = new QLabel(this);
    m_saveMessage->setWordWrap(true);
    layout->addWidget(m_saveMessage);

    layout->addStretch(1);

    if (m_pedals) {
        connect(m_pedals, &PedalInput::positionsChanged, this,
                &MainSettingsPage::refreshPositions);
    }
    if (m_config) {
        connect(m_config, &ConfigRead::stateChanged, this,
                [this] { loadFromDevice(); });
    }
    if (m_brakeOptions) {
        connect(m_brakeOptions, &BrakeOptionsRead::stateChanged, this,
                [this] { loadFromDevice(); });
    }
    if (m_save) {
        connect(m_save, &SettingsSave::changed, this, [this] {
            // A finished read-back is the one moment the controls must follow
            // the device whatever they hold: on success they already match it,
            // and on a mismatch the message promises they now show the truth.
            const SettingsSave::State state = m_save->state();
            if (m_lastSaveState == SettingsSave::State::Verifying
                && state == SettingsSave::State::Idle)
                loadFromDevice(true);
            m_lastSaveState = state;
            refreshButtons();
        });
        m_lastSaveState = m_save->state();
    }
    refreshPositions();
    loadFromDevice();
}

// --- What the device holds ---------------------------------------------------

void MainSettingsPage::loadFromDevice(bool force)
{
    const std::optional<SettingsSave::Settings> stored =
        m_save ? m_save->stored() : std::nullopt;

    if (stored && (force || !isDirty())) {
        apply(*stored);
        m_shown = *stored;
        m_loaded = true;

        // Logged because the whole point of this page is that it shows what
        // the pedals hold, and a graph that looks like the default curve is
        // indistinguishable from one that was never loaded. This says which.
        for (int i = 0; i < m_rows.size(); ++i) {
            qCDebug(hidcenterDevice).noquote()
                << QStringLiteral("SIMSONN: %1 settings from the device: deadzones %2–%3 %, "
                                  "filter %4, curve %5")
                       .arg(tr(kPedals[i].title))
                       .arg(m_rows[i].deadzones->low())
                       .arg(m_rows[i].deadzones->high())
                       .arg(current().pedals[size_t(i)].filter)
                       .arg(curveText(m_rows[i].curve->values()));
        }
    } else if (stored && m_loaded) {
        // Edits are kept, but settings that were not there to edit are taken:
        // the brake's own read arriving after the blob, typically.
        SettingsSave::Settings shown = m_shown;
        bool arrived = false;
        if (!m_shown.brake.elastomer && stored->brake.elastomer) {
            shown.brake.elastomer = stored->brake.elastomer;
            arrived = true;
        }
        if (!m_shown.brake.throttleCut && stored->brake.throttleCut) {
            shown.brake.throttleCut = stored->brake.throttleCut;
            arrived = true;
        }
        if (arrived) {
            SettingsSave::Settings edited = current();
            edited.brake.elastomer = shown.brake.elastomer;
            edited.brake.throttleCut = shown.brake.throttleCut;
            m_shown = shown;
            apply(edited);
        }
    }

    refreshButtons();
}

void MainSettingsPage::apply(const SettingsSave::Settings &settings)
{
    for (int i = 0; i < m_rows.size() && i < kPedalCount; ++i) {
        const SettingsSave::PedalSettings &pedal = settings.pedals[i];
        QVector<double> values;
        values.reserve(ConfigRead::kCurvePoints);
        for (const qint8 point : pedal.curve)
            values << double(point);
        m_rows[i].curve->setValues(values);

        // The stored pair is two sizes, each measured inward from its own end;
        // the slider is two positions on one scale. So the start deadzone is the
        // low thumb as it stands, and the end deadzone has to be turned around.
        m_rows[i].deadzones->setValues(pedal.startDeadzone,
                                       kDeadzoneMaximum - pedal.endDeadzone);

        // A pedal with filtering off keeps whatever level its number showed, so
        // ticking the box again goes back to it.
        const QSignalBlocker blockBox(m_rows[i].filterEnabled);
        const QSignalBlocker blockLevel(m_rows[i].filter);
        m_rows[i].filterEnabled->setChecked(pedal.filter > 0);
        if (pedal.filter > 0)
            m_rows[i].filter->setValue(pedal.filter);
        m_rows[i].filter->setEnabled(pedal.filter > 0);
        m_rows[i].filterText->setText(QString::number(m_rows[i].filter->value()));
        m_rows[i].filterText->setEnabled(pedal.filter > 0);
    }

    const SettingsSave::BrakeSettings &brake = settings.brake;
    {
        const QSignalBlocker blocker(m_elastomer);
        const int index = brake.elastomer ? m_elastomer->findData(int(*brake.elastomer)) : -1;
        m_elastomer->setCurrentIndex(qMax(index, 0));
    }
    m_elastomer->setEnabled(brake.elastomer.has_value());
    refreshForceRange();
    {
        const QSignalBlocker blocker(m_force);
        m_force->setValue(brake.forceKg);
    }
    {
        const QSignalBlocker blockBox(m_cutEnabled);
        const QSignalBlocker blockPoint(m_cutPoint);
        m_cutEnabled->setChecked(brake.throttleCut && brake.throttleCut->enabled);
        m_cutPoint->setValue(brake.throttleCut ? brake.throttleCut->tenths
                                               : kThrottleCutMinimum);
        m_cutPointText->setText(cutPointText(m_cutPoint->value()));
    }
    m_cutEnabled->setEnabled(brake.throttleCut.has_value());
    m_cutPoint->setEnabled(brake.throttleCut && brake.throttleCut->enabled);
    m_cutPointText->setEnabled(m_cutPoint->isEnabled());
}

void MainSettingsPage::refreshForceRange()
{
    // The force is clamped under a
    // softer elastomer — which then counts as an edit, as it should: saving is
    // what would put the lower force on the pedals.
    const QSignalBlocker blocker(m_force);
    const int before = m_force->value();
    const Elastomer elastomer = Elastomer(char(m_elastomer->currentData().toInt()));
    m_force->setMaximum(m_elastomer->isEnabled() ? brakeForceMaximumKg(elastomer)
                                                 : brakeForceMaximumKg(Elastomer::Hard));
    if (m_force->value() != before && m_loaded)
        refreshButtons();
}

SettingsSave::Settings MainSettingsPage::current() const
{
    SettingsSave::Settings settings {};
    for (int i = 0; i < m_rows.size() && i < kPedalCount; ++i) {
        SettingsSave::PedalSettings &pedal = settings.pedals[i];
        const QVector<double> values = m_rows[i].curve->values();
        for (int p = 0; p < ConfigRead::kCurvePoints && p < values.size(); ++p)
            pedal.curve[size_t(p)] = qint8(qBound(-100, qRound(values[p]), 100));

        // Turned back around the way apply() turned it: the device measures the
        // end deadzone inward from the top.
        pedal.startDeadzone = quint8(m_rows[i].deadzones->low());
        pedal.endDeadzone = quint8(kDeadzoneMaximum - m_rows[i].deadzones->high());
        pedal.filter = m_rows[i].filterEnabled->isChecked() ? quint8(m_rows[i].filter->value())
                                                            : quint8(0);
    }

    settings.brake.forceKg = m_force->value();

    // Only what was read can have been edited: a setting the device never
    // reported stays unknown rather than taking the control's placeholder.
    if (m_shown.brake.elastomer)
        settings.brake.elastomer = Elastomer(char(m_elastomer->currentData().toInt()));
    if (m_shown.brake.throttleCut) {
        settings.brake.throttleCut =
            ThrottleCut {m_cutEnabled->isChecked(), m_cutPoint->value()};
    }
    return settings;
}

bool MainSettingsPage::isDirty() const
{
    // Rounded to what the device can store before comparing, so a point nudged
    // by less than one unit is not a change worth offering to save.
    return m_loaded && current() != m_shown;
}

void MainSettingsPage::refreshButtons()
{
    const bool busy = m_save && m_save->isRunning();
    const bool dirty = isDirty();

    m_saveButton->setEnabled(dirty && !busy && m_save && m_vendorInterface);
    m_cancelButton->setEnabled(dirty && !busy);

    // Nothing is editable while a save is being checked: the read-back replaces
    // the controls' contents when it lands, and would take a fresh edit with it.
    for (const Row &row : std::as_const(m_rows))
        row.box->setEnabled(!busy);

    const QString message = m_save ? m_save->message() : QString();
    m_saveMessage->setText(message);
    m_saveMessage->setVisible(!message.isEmpty());
    m_saveMessage->setStyleSheet(m_save && m_save->failed() ? QStringLiteral("color: #c03030;")
                                                            : QString());

    // Whether the controls are the device's or the user's is also what the
    // notice above them says.
    refreshConfigNotice();
}

void MainSettingsPage::refreshConfigNotice()
{
    QString text;
    if (m_loaded) {
        text = isDirty() ? tr("Unsaved changes.")
                         : tr("Showing the settings stored on the pedals.");
        if (m_brakeOptions && m_brakeOptions->state() == BrakeOptionsRead::State::Failed) {
            text += QLatin1Char(' ')
                    + tr("Could not read the brake's elastomer and throttle cut, so they "
                         "cannot be changed. %1")
                          .arg(m_brakeOptions->error());
        } else if (m_brakeOptions
                   && m_brakeOptions->state() == BrakeOptionsRead::State::Reading) {
            text += QLatin1Char(' ') + tr("Reading the brake's elastomer and throttle cut…");
        }
    } else if (!m_config) {
        text = tr("The pedals' settings are not being read: the controls show defaults.");
    } else {
        switch (m_config->state()) {
        case ConfigRead::State::Reading:
            text = tr("Reading the pedals' settings… (%1 of %2)")
                       .arg(m_config->packetsRead())
                       .arg(m_config->packetCount());
            break;
        case ConfigRead::State::Failed:
            text = tr("The pedals' settings could not be read, so the controls show defaults. "
                      "%1")
                       .arg(m_config->error());
            break;
        case ConfigRead::State::Idle:
        case ConfigRead::State::Ready:
            // Ready without m_loaded means the blob arrived but held nothing for
            // these axes, which is a device this application does not understand
            // rather than something the user can act on.
            text = tr("The pedals' settings have not been read: the controls show defaults.");
            break;
        }
    }

    m_configNotice->setText(text);
    m_configNotice->setVisible(!text.isEmpty());
}

// --- The live pedal positions -----------------------------------------------

QSize MainSettingsPage::minimumSizeHint() const
{
    // The wrapped notices and the response curves both get taller as the page
    // gets narrower, which the layout's own minimum does not count: it would let
    // the window come out too short and the controls overlap. Ask for the height
    // the layout really needs at the width it has.
    QSize hint = QWidget::minimumSizeHint();
    if (layout()) {
        hint.setHeight(qMax(hint.height(),
                            layout()->minimumHeightForWidth(qMax(width(), hint.width()))));
    }
    return hint;
}

void MainSettingsPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (event->size().width() != event->oldSize().width())
        updateGeometry();
}

void MainSettingsPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_pedals)
        m_pedals->setEnabled(true);
    // Whether the reading could be started at all is only known now, and it is
    // what the notice below the title has to say.
    refreshPositions();
}

void MainSettingsPage::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (m_pedals)
        m_pedals->setEnabled(false);
}

void MainSettingsPage::refreshPositions()
{
    for (int i = 0; i < m_rows.size(); ++i) {
        m_rows[i].curve->setInput(m_pedals ? m_pedals->position(kPedals[i].axis)
                                           : std::optional<double>());
    }

    const QString reason = m_pedals ? m_pedals->unavailableReason()
                                    : tr("The pedal positions are not being read.");
    m_positionNotice->setText(reason);
    m_positionNotice->setVisible(!reason.isEmpty());
}

} // namespace hidcenter::simsonn_pedals
