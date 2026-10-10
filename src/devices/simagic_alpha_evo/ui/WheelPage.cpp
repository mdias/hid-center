// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WheelPage.h"

#include "AlphaEvoBlocks.h"
#include "AlphaEvoButtonInput.h"
#include "AlphaEvoSettings.h"
#include "ClutchPaddlesView.h"
#include "PressedButtonsView.h"
#include "WheelButtonControlDialog.h"
#include "WheelConfigController.h"
#include "WheelInputsControlDialog.h"
#include "WheelLayouts.h"
#include "WheelRpmLightsDialog.h"
#include "WheelSchematic.h"
#include "core/DeveloperMode.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedLayout>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

namespace hidcenter::simagic_alpha_evo {
namespace {

// What the picker's entries hold besides a WheelModel: the two pages shown when
// there is no wheel to draw, so a developer can look at them with any wheel on
// the base. An invalid value is the entry that follows the base.
constexpr int kPickNoWheel = -1;
constexpr int kPickUnknownWheel = -2;

} // namespace

WheelPage::WheelPage(WheelConfigController *wheel, AlphaEvoButtonInput *buttons,
                     AlphaEvoSettings *settings, const QString &probeError, QWidget *parent)
    : QWidget(parent)
    , m_wheel(wheel)
    , m_settings(settings)
    , m_buttons(buttons)
    , m_probeError(probeError)
{
    auto *layout = new QVBoxLayout(this);

    // Where the page says what has gone wrong with the wheel, if anything has,
    // so the sections below can stay about their settings. Hidden otherwise.
    m_problem = new QLabel(this);
    m_problem->setObjectName(QStringLiteral("wheelProblem"));
    m_problem->setWordWrap(true);
    layout->addWidget(m_problem);

    // The picker is for developers: everyone else gets the wheel the base reports.
    // Built either way, since the rest of the page asks it which wheel is shown.
    auto *pickRow = new QHBoxLayout;
    auto *pickLabel = new QLabel(tr("&Wheel"), this);
    pickRow->addWidget(pickLabel);

    m_model = new QComboBox(this);
    m_model->setObjectName(QStringLiteral("wheelModel"));
    // With a controller the first entry follows the base, and is the one a user
    // who cannot see the picker is always on. It is also what lets a developer
    // see the pages for no wheel and for an unknown one; picking a wheel by hand
    // overrides it. Without a controller there is nothing to follow.
    if (m_wheel)
        m_model->addItem(tr("Detected on the base"), QVariant());
    for (const WheelLayout &wheel : wheelLayouts()) {
        if (wheelLayoutReleased(wheel))
            m_model->addItem(wheel.title, int(wheel.model));
    }
    if (hidcenter::developerMode()) {
        m_model->addItem(tr("No wheel"), kPickNoWheel);
        m_model->addItem(tr("Unknown wheel"), kPickUnknownWheel);
    }
    pickLabel->setBuddy(m_model);
    pickRow->addWidget(m_model);
    pickRow->addStretch(1);
    pickLabel->setVisible(hidcenter::developerMode());
    m_model->setVisible(hidcenter::developerMode());
    layout->addLayout(pickRow);

    // The page is one of three: the wheel, or why there is none to show.
    m_pages = new QStackedLayout;
    layout->addLayout(m_pages, 1);

    auto *view = new QWidget(this);
    auto *viewLayout = new QVBoxLayout(view);
    viewLayout->setContentsMargins(0, 0, 0, 0);
    m_pages->addWidget(view);

    m_noWheelPage = makeNoticePage(QStringLiteral("noWheelPage"), tr("No wheel connected"),
                                   &m_noWheelDetail);
    m_unknownWheelPage = makeNoticePage(QStringLiteral("unknownWheelPage"), tr("Unknown wheel"),
                                        &m_unknownWheelDetail);
    m_pages->addWidget(m_noWheelPage);
    m_pages->addWidget(m_unknownWheelPage);

    // Said plainly, because a picture of a wheel on a device's page reads as a
    // picture of that device's wheel, and this one may not be.
    auto *hint = new QLabel(tr("Point at a control to identify it; click it to configure it."),
                            view);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    viewLayout->addWidget(hint);

    auto *body = new QHBoxLayout;
    viewLayout->addLayout(body, 1);

    // The page's spare room goes to the wheel, which draws itself as large as
    // it fits.
    m_schematic = new WheelSchematic(this);
    m_schematic->setObjectName(QStringLiteral("wheelSchematic"));
    m_schematic->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    body->addWidget(m_schematic, 1);

    // --- Settings of the wheel as a whole ------------------------------------
    // A column beside the wheel, for what belongs to no one control. Stacked
    // from the top, and as wide as its widest control wants. It scrolls when the
    // window is too short for it, rather than squeezing what is in it.
    auto *side = new QWidget(this);
    auto *column = new QVBoxLayout(side);
    column->setContentsMargins(0, 0, 0, 0);
    auto *sideScroll = new QScrollArea(this);
    sideScroll->setFrameShape(QFrame::NoFrame);
    sideScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sideScroll->setWidgetResizable(true);
    sideScroll->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
    body->addWidget(sideScroll);

    auto *leds = new QGroupBox(tr("LEDs"), this);
    auto *ledsLayout = new QVBoxLayout(leds);
    ledsLayout->addWidget(new QLabel(tr("Brightness"), leds));

    // Where the wheel was last set, which is the one thing known about it: its
    // brightness cannot be read back.
    const int initialBrightness =
        m_wheel ? m_wheel->ledBrightness() : m_schematic->ledBrightness();
    m_schematic->setLedBrightness(initialBrightness);

    auto *brightnessRow = new QHBoxLayout;
    auto *brightness = new QSlider(Qt::Horizontal, leds);
    brightness->setObjectName(QStringLiteral("ledBrightness"));
    brightness->setRange(0, kWheelLedBrightnessMax);
    brightness->setValue(initialBrightness);
    brightness->setMinimumWidth(140);
    brightnessRow->addWidget(brightness, 1);

    auto *brightnessValue = new QSpinBox(leds);
    brightnessValue->setObjectName(QStringLiteral("ledBrightnessValue"));
    brightnessValue->setRange(0, kWheelLedBrightnessMax);
    brightnessValue->setValue(initialBrightness);
    // Typing 35 would otherwise send 3 to the wheel on the way.
    brightnessValue->setKeyboardTracking(false);
    brightnessRow->addWidget(brightnessValue);
    ledsLayout->addLayout(brightnessRow);

    column->addWidget(leds);

    // --- Clutch paddles ------------------------------------------------------
    // Only for a wheel that has them; see showChosenWheel(). Like the
    // brightness, the same two bytes on every wheel, so set for the base rather
    // than per wheel.
    m_clutch = new QGroupBox(tr("Clutch paddles"), this);
    m_clutch->setObjectName(QStringLiteral("clutchPaddles"));
    auto *clutchLayout = new QVBoxLayout(m_clutch);

    auto *clutchRow = new QHBoxLayout;
    m_clutchSync = new QPushButton(tr("Sync"), m_clutch);
    m_clutchSync->setObjectName(QStringLiteral("clutchSync"));
    m_clutchSync->setCheckable(true);
    m_clutchSync->setToolTip(tr("Synced, both paddles work one clutch: either paddle alone "
                                "takes it as far as the ratio, both together all the way.\n"
                                "Not synced, each paddle is a clutch axis of its own."));
    clutchRow->addWidget(m_clutchSync);
    clutchRow->addStretch(1);
    auto *ratioLabel = new QLabel(tr("&Ratio"), m_clutch);
    clutchRow->addWidget(ratioLabel);
    m_clutchRatio = new QSpinBox(m_clutch);
    m_clutchRatio->setObjectName(QStringLiteral("clutchRatio"));
    m_clutchRatio->setRange(0, kWheelClutchRatioMax);
    m_clutchRatio->setSuffix(tr(" %"));
    m_clutchRatio->setKeyboardTracking(false);
    m_clutchRatio->setToolTip(tr("How far one synced paddle alone takes the clutch: the bite "
                                 "point."));
    ratioLabel->setBuddy(m_clutchRatio);
    clutchRow->addWidget(m_clutchRatio);
    clutchLayout->addLayout(clutchRow);

    m_clutchView = new ClutchPaddlesView(m_clutch);
    m_clutchView->setObjectName(QStringLiteral("clutchPaddlesView"));
    m_clutchView->setToolTip(m_clutchRatio->toolTip());
    clutchLayout->addWidget(m_clutchView);

    column->addWidget(m_clutch);

    // What the wheel was last set to, as with the brightness: neither can be
    // read back.
    const WheelConfig stock = defaultWheelConfig();
    m_previewClutchSynced = stock.clutchSynced();
    m_previewClutchRatio = stock.clutchRatio();
    m_clutchSync->setChecked(m_wheel ? m_wheel->clutchSynced() : m_previewClutchSynced);
    m_clutchRatio->setValue(m_wheel ? m_wheel->clutchRatio() : m_previewClutchRatio);
    m_clutchView->setRatio(m_clutchRatio->value());

    connect(m_clutchSync, &QPushButton::toggled, this, [this](bool synced) {
        if (m_wheel)
            m_wheel->setClutchSynced(synced);
        else
            m_previewClutchSynced = synced;
        showClutch();
    });
    connect(m_clutchRatio, &QSpinBox::valueChanged, this, [this](int percent) {
        m_clutchView->setRatio(percent);
        if (m_wheel)
            m_wheel->setClutchRatio(percent);
        else
            m_previewClutchRatio = percent;
    });
    // The spin box is what holds the value; dragging the handle sets it.
    connect(m_clutchView, &ClutchPaddlesView::ratioChanged, m_clutchRatio, &QSpinBox::setValue);

    // --- Buttons pressed -----------------------------------------------------
    // Read from the base's joystick report, so it shows what the game would
    // see, mappings and all.
    auto *pressed = new QGroupBox(tr("Pressed buttons"), this);
    auto *pressedLayout = new QVBoxLayout(pressed);
    m_pressed = new PressedButtonsView(pressed);
    m_pressed->setObjectName(QStringLiteral("pressedButtons"));
    pressedLayout->addWidget(m_pressed);
    m_pressedStatus = new QLabel(pressed);
    m_pressedStatus->setObjectName(QStringLiteral("pressedButtonsStatus"));
    m_pressedStatus->setWordWrap(true);
    m_pressedStatus->setEnabled(false);
    pressedLayout->addWidget(m_pressedStatus);
    column->addWidget(pressed);
    column->addStretch(1);

    // Set once the column is full, so its width is known: as wide as it wants,
    // and room for the scroll bar when there is one.
    sideScroll->setWidget(side);
    sideScroll->setMinimumWidth(side->sizeHint().width()
                                + style()->pixelMetric(QStyle::PM_ScrollBarExtent));

    if (m_buttons) {
        connect(m_buttons, &AlphaEvoButtonInput::pressedChanged, this,
                &WheelPage::showPressedButtons);
        connect(m_buttons, &AlphaEvoButtonInput::sourceStateChanged, this,
                &WheelPage::showPressedButtons);
        connect(m_buttons, &AlphaEvoButtonInput::clutchChanged, this, &WheelPage::showClutch);
    }
    showPressedButtons();
    showClutch();

    // The slider is the control; the spin box is there so the exact value can be
    // seen and typed, as on the FFB tab. The controller paces what reaches the
    // wheel, so the slider can report every step.
    connect(brightness, &QSlider::valueChanged, this, [this, brightnessValue](int value) {
        const QSignalBlocker blocker(brightnessValue);
        brightnessValue->setValue(value);
        m_schematic->setLedBrightness(value);
        if (m_wheel)
            m_wheel->setLedBrightness(value);
    });
    connect(brightnessValue, &QSpinBox::valueChanged, brightness, &QSlider::setValue);

    if (m_settings) {
        connect(m_settings, &AlphaEvoSettings::wheelConfigSent, this,
                &WheelPage::onWheelConfigSent);
    }
    if (m_wheel) {
        connect(m_wheel, &WheelConfigController::attachedWheelChanged, this,
                &WheelPage::showChosenWheel);
    }

    connect(m_model, &QComboBox::currentIndexChanged, this, &WheelPage::showChosenWheel);
    connect(m_schematic, &WheelSchematic::controlClicked, this, &WheelPage::editControl);
    showChosenWheel();
}

WheelPage::~WheelPage()
{
    // A page destroyed while shown gets no hide event.
    if (m_buttons)
        m_buttons->setEnabled(false);
}

void WheelPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_buttons)
        m_buttons->setEnabled(true);
}

void WheelPage::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (m_buttons)
        m_buttons->setEnabled(false);
}

void WheelPage::showPressedButtons()
{
    const QList<int> pressed = m_buttons ? m_buttons->pressed() : QList<int>();

    // The buttons a wheel's mappings can reach, and past them any the report
    // has been seen pressing, so a shifter or handbrake on a higher number
    // still shows once it is used. Never beyond what the report declares.
    const int declared = m_buttons ? m_buttons->buttonCount() : 0;
    int shown = qMax(m_pressed->buttonCount(), kWheelButtonMax);
    if (!pressed.isEmpty())
        shown = qMax(shown, pressed.last());
    if (declared > 0)
        shown = qMin(shown, qMax(declared, kWheelButtonMax));
    m_pressed->setButtonCount(shown);
    m_pressed->setPressed(pressed);

    const QString why = m_buttons ? m_buttons->sourceDescription()
                                  : tr("Preview only: no buttons are read.");
    m_pressedStatus->setText(why);
    m_pressedStatus->setVisible(!why.isEmpty());
}

const WheelLayout *WheelPage::wheelToShow() const
{
    // A wheel picked by hand, which only a developer can do.
    const QVariant picked = m_model->currentData();
    if (picked.isValid()) {
        return picked.toInt() < 0 ? nullptr
                                  : &wheelLayout(static_cast<WheelModel>(picked.toInt()));
    }

    // Otherwise the one on the base, if it is one the user is offered.
    if (!m_wheel)
        return nullptr;
    const WheelLayout *attached = wheelLayoutForId(m_wheel->attachedWheelId());
    return attached && wheelLayoutReleased(*attached) ? attached : nullptr;
}

QWidget *WheelPage::makeNoticePage(const QString &objectName, const QString &title,
                                   QLabel **detail)
{
    auto *page = new QWidget(this);
    page->setObjectName(objectName);
    auto *layout = new QVBoxLayout(page);
    layout->addStretch(1);

    auto *heading = new QLabel(title, page);
    QFont font = heading->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.3);
    heading->setFont(font);
    heading->setAlignment(Qt::AlignCenter);
    layout->addWidget(heading);

    *detail = new QLabel(page);
    (*detail)->setWordWrap(true);
    (*detail)->setAlignment(Qt::AlignCenter);
    (*detail)->setEnabled(false);
    layout->addWidget(*detail);

    layout->addStretch(2);
    return page;
}

void WheelPage::showClutch()
{
    const bool synced = m_clutchSync->isChecked();
    const qreal left = m_buttons ? m_buttons->leftClutch() : 0.0;
    const qreal right = m_buttons ? m_buttons->rightClutch() : 0.0;

    // Synced paddles are reported as one axis, the left one, and the report
    // says nothing of where each paddle is: both bars show what they make
    // together.
    if (synced) {
        m_clutchView->setLabels(tr("Synced"), tr("Synced"));
        m_clutchView->setValues(left, left);
    } else {
        m_clutchView->setLabels(tr("Left"), tr("Right"));
        m_clutchView->setValues(left, right);
    }

    // A ratio does nothing to paddles that are not synced.
    m_clutchView->setRatioEditable(synced);
    m_clutchRatio->setEnabled(synced);
}

void WheelPage::showChosenWheel()
{
    const WheelLayout *shownLayout = wheelToShow();
    if (!shownLayout) {
        m_schematic->setWheel(nullptr);

        // Which of the two it is comes from the picker when it was told, else from
        // what the base reports: 0 is no wheel, anything else is a wheel this
        // page has no face for.
        const QVariant picked = m_model->currentData();
        const int wheelId = m_wheel ? m_wheel->attachedWheelId() : 0;
        const bool unknown = picked.isValid() ? picked.toInt() == kPickUnknownWheel
                                              : wheelId != 0;
        if (unknown) {
            QString detail =
                tr("This wheel is not supported yet, so there is nothing to configure here.");
            if (!picked.isValid())
                detail += tr("\nThe base reports wheel ID %1.").arg(wheelId);
            m_unknownWheelDetail->setText(detail);
            m_pages->setCurrentWidget(m_unknownWheelPage);
        } else {
            QString detail = tr("Attach a steering wheel to the base to configure it here.");
            if (!m_probeError.isEmpty())
                detail = tr("The base did not answer, so the wheel on it is not known.");
            else if (m_settings && m_settings->isOffline())
                detail = tr("No wheelbase is connected.");
            m_noWheelDetail->setText(detail);
            m_pages->setCurrentWidget(m_noWheelPage);
        }
        refreshWarnings();
        refreshStatus();
        return;
    }
    m_pages->setCurrentIndex(0);

    const WheelModel chosen = shownLayout->model;
    const WheelLayout &layout = *shownLayout;
    m_schematic->setWheel(&layout);
    m_clutch->setVisible(std::any_of(layout.controls.cbegin(), layout.controls.cend(),
                                     [](const WheelControl &control) {
                                         return control.kind == WheelControlKind::ClutchPaddle;
                                     }));
    m_schematic->setControlColours(coloursFor(chosen));

    // Every button and lit label has a colour to set. The RPM lights have
    // something only where the controller holds that wheel's patterns, and an
    // encoder or a switch only where it holds the joystick button of one of its
    // actions.
    QSet<QString> editable;
    for (const WheelControl &control : layout.controls) {
        bool pressesButtons = false;
        for (qsizetype action = 0; m_wheel && action < control.inputs.size(); ++action) {
            if (m_wheel->controlButton(chosen, control.key, int(action)))
                pressesButtons = true;
        }

        switch (control.kind) {
        case WheelControlKind::Button:
        case WheelControlKind::LitLabel:
            editable.insert(control.key);
            break;
        case WheelControlKind::RpmLights:
            if (m_wheel && m_wheel->rpmLights(chosen))
                editable.insert(control.key);
            break;
        case WheelControlKind::RotaryEncoder:
        case WheelControlKind::ThumbEncoder:
        case WheelControlKind::SevenWaySwitch:
            if (pressesButtons)
                editable.insert(control.key);
            break;
        case WheelControlKind::ShiftPaddle:
        case WheelControlKind::ClutchPaddle:
            break;
        }
    }
    m_schematic->setEditableControls(editable);

    refreshWarnings();
    refreshStatus();
}

void WheelPage::editControl(int index)
{
    const WheelLayout *wheel = m_schematic->wheel();
    if (!wheel || index < 0 || index >= wheel->controls.size())
        return;

    const WheelControl &control = wheel->controls.at(index);
    switch (control.kind) {
    // A lit label has the same window as a button without the parts it has
    // nothing for: it is lit, and nothing on it presses a joystick button.
    case WheelControlKind::Button:
    case WheelControlKind::LitLabel: {
        // By model and key rather than by index, which is only good for the
        // wheel on screen. The window is modal, so that cannot change under it,
        // but nothing here has to rely on that.
        const WheelModel model = wheel->model;
        const QString key = control.key;

        WheelButtonSettings original;
        original.colour = m_schematic->controlColour(control);
        if (m_wheel) {
            original.trigger = m_wheel->controlTrigger(model, key);
            original.joystickButton = m_wheel->controlButton(model, key, 0);
        }
        // Worked out once: only this window changes a mapping while it is open,
        // and a button has no other action for a warning to be about.
        if (original.joystickButton) {
            for (int button = 1; button <= kWheelButtonMax; ++button) {
                const QString warning = sharedButtonWarning(model, key, 0, button);
                if (!warning.isEmpty())
                    original.joystickButtonWarnings.insert(button, warning);
            }
        }

        auto *dialog = new WheelButtonControlDialog(control, original, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog, &WheelButtonControlDialog::colourChanged, this,
                [this, model, key](const QColor &colour) { setControlColour(model, key, colour); });
        connect(dialog, &WheelButtonControlDialog::triggerChanged, this,
                [this, model, key](int trigger) { m_wheel->setControlTrigger(model, key, trigger); });
        connect(dialog, &WheelButtonControlDialog::joystickButtonChanged, this,
                [this, model, key](int button) { setControlButton(model, key, 0, button); });
        connect(dialog, &QDialog::rejected, this, [this, model, key, original] {
            setControlColour(model, key, original.colour);
            if (original.trigger)
                m_wheel->setControlTrigger(model, key, *original.trigger);
            if (original.joystickButton)
                setControlButton(model, key, 0, *original.joystickButton);
        });

        // open() rather than exec(): unplugging the base destroys this page, and
        // the window with it, which exec() would then return into.
        dialog->open();
        return;
    }
    case WheelControlKind::RpmLights: {
        const WheelModel model = wheel->model;
        const std::optional<RpmLights> original = m_wheel ? m_wheel->rpmLights(model) : std::nullopt;
        if (!original)
            return;

        auto *dialog = new WheelRpmLightsDialog(control, *original, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog, &WheelRpmLightsDialog::rpmLightsChanged, this,
                [this, model](const RpmLights &lights) { setRpmLights(model, lights); });
        connect(dialog, &QDialog::rejected, this,
                [this, model, original] { setRpmLights(model, *original); });
        dialog->open();
        return;
    }
    case WheelControlKind::RotaryEncoder:
    case WheelControlKind::ThumbEncoder:
    case WheelControlKind::SevenWaySwitch:
        editInputs(control, wheel->model);
        return;
    case WheelControlKind::ShiftPaddle:
    case WheelControlKind::ClutchPaddle:
        return;
    }
}

void WheelPage::editInputs(const WheelControl &control, WheelModel model)
{
    if (!m_wheel)
        return;

    const QString key = control.key;
    QList<std::optional<int>> original;
    bool any = false;
    for (qsizetype action = 0; action < control.inputs.size(); ++action) {
        original << m_wheel->controlButton(model, key, int(action));
        if (original.constLast())
            any = true;
    }
    if (!any)
        return;

    auto *dialog = new WheelInputsControlDialog(
        control, original,
        [this, model, key](int action, int button) {
            return sharedButtonWarning(model, key, action, button);
        },
        this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &WheelInputsControlDialog::joystickButtonChanged, this,
            [this, model, key](int action, int button) {
                setControlButton(model, key, action, button);
            });
    connect(dialog, &QDialog::rejected, this, [this, model, key, original] {
        for (qsizetype action = 0; action < original.size(); ++action) {
            if (original.at(action))
                setControlButton(model, key, int(action), *original.at(action));
        }
    });
    dialog->open();
}

void WheelPage::setRpmLights(WheelModel model, const RpmLights &lights)
{
    m_wheel->setRpmLights(model, lights);

    const WheelLayout *shown = m_schematic->wheel();
    if (shown && shown->model == model)
        m_schematic->setControlColours(coloursFor(model));
}

void WheelPage::onWheelConfigSent(bool ok, const QString &error, bool dryRun)
{
    Q_UNUSED(dryRun);

    // One signal for every report of the block, so the failure is the wheel's
    // rather than the brightness's or the colours'.
    if (ok)
        m_writeError.clear();
    else
        m_writeError = tr("Could not write to the wheel: %1").arg(error);
    refreshStatus();
}

void WheelPage::refreshStatus()
{
    // Only what is wrong. The write failure is logged where it happens, and
    // the offline and dry-run states are on the Status tab.
    QString problem;
    if (!m_probeError.isEmpty())
        problem = tr("The base did not answer, so changes to the wheel are not sent.");
    else if (!m_writeError.isEmpty())
        problem = m_writeError;
    m_problem->setText(problem);
    m_problem->setVisible(!problem.isEmpty());
}

QHash<QString, QColor> WheelPage::coloursFor(WheelModel model) const
{
    return m_wheel ? m_wheel->controlColours(model) : m_previewColours.value(int(model));
}

void WheelPage::setControlColour(WheelModel model, const QString &key, const QColor &colour)
{
    if (m_wheel)
        m_wheel->setControlColour(model, key, colour);
    else
        m_previewColours[int(model)].insert(key, colour);

    const WheelLayout *shown = m_schematic->wheel();
    if (shown && shown->model == model)
        m_schematic->setControlColours(coloursFor(model));
}

void WheelPage::setControlButton(WheelModel model, const QString &key, int action, int button)
{
    m_wheel->setControlButton(model, key, action, button);
    refreshWarnings();
}

QString WheelPage::sharedButtonWarning(WheelModel model, const QString &key, int action,
                                       int button) const
{
    if (!m_wheel)
        return {};
    const WheelConfigController::ButtonSharing sharing =
        m_wheel->buttonSharing(model, key, action, button);
    if (sharing.isEmpty())
        return {};

    // Quoted, since a control's name may have a comma of its own, and the
    // action after it for a control that has more than one.
    const WheelLayout &layout = wheelLayout(model);
    QStringList who;
    for (const WheelConfigController::SharedAction &other : sharing.controls) {
        for (const WheelControl &control : layout.controls) {
            if (control.key != other.controlKey)
                continue;
            const QString actionName = wheelControlActionName(control.kind, other.action);
            who << (actionName.isEmpty()
                        ? tr("“%1”").arg(wheelControlName(control))
                        : tr("“%1” (%2)").arg(wheelControlName(control), actionName));
        }
    }
    if (sharing.otherInputs == 1)
        who << tr("another control");
    else if (sharing.otherInputs > 1)
        who << tr("%1 other controls").arg(sharing.otherInputs);

    return tr("Button %1 is already mapped by %2.")
        .arg(button)
        .arg(QLocale().createSeparatedList(who));
}

void WheelPage::refreshWarnings()
{
    QHash<QString, QString> warnings;
    const WheelLayout *shown = m_schematic->wheel();
    if (m_wheel && shown) {
        for (const WheelControl &control : shown->controls) {
            // A line per action that shares, each named where there is more
            // than one to tell apart.
            QStringList lines;
            for (qsizetype action = 0; action < control.inputs.size(); ++action) {
                const std::optional<int> button =
                    m_wheel->controlButton(shown->model, control.key, int(action));
                const QString warning =
                    button ? sharedButtonWarning(shown->model, control.key, int(action), *button)
                           : QString();
                if (warning.isEmpty())
                    continue;
                const QString actionName = wheelControlActionName(control.kind, int(action));
                lines << (actionName.isEmpty() ? warning : tr("%1: %2").arg(actionName, warning));
            }
            if (!lines.isEmpty())
                warnings.insert(control.key, lines.join(u'\n'));
        }
    }
    m_schematic->setControlWarnings(warnings);
}

} // namespace hidcenter::simagic_alpha_evo
