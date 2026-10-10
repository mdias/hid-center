// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>

class QVBoxLayout;

namespace hidcenter::simagic_alpha_evo {

struct WheelControl;

/// What every window for a steering wheel control's settings has in common: the
/// control's name as its title, the settings, and OK and Cancel under them.
///
/// Only a base. Each kind of control has its own subclass with the settings that
/// kind has, such as WheelButtonControlDialog for a button.
///
/// **Changes apply as they are made.** A subclass reports each one as the user
/// makes it, so the wheel drawing behind the window follows along. OK therefore
/// only closes the window. Cancel, Esc and closing the window all reject it, and
/// it is whoever opened the window that remembers what the control was before and
/// puts it back on rejected(). The window holds nothing to revert.
class WheelControlDialog : public QDialog
{
    Q_OBJECT

protected:
    /// `control` is only read here, for its name, and is not kept.
    explicit WheelControlDialog(const WheelControl &control, QWidget *parent = nullptr);

    /// Where a subclass puts its settings, above OK and Cancel.
    QVBoxLayout *contentLayout() const { return m_content; }

private:
    QVBoxLayout *m_content = nullptr;
};

} // namespace hidcenter::simagic_alpha_evo
