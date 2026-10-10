// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace hidcenter {

class TelemetryRouter;
class TelemetrySourceModule;

/// Every loaded telemetry source, and what each of them is doing.
///
/// The Telemetry page's header can only ever name one source — the one being
/// listened to — and its arrow only counts that one's packets. This is where the
/// rest becomes visible: a second sim installed and waiting its turn, or a
/// source that loaded but could not take its port, which from the header alone
/// is indistinguishable from a game nobody has started.
class TelemetrySourceList : public QWidget
{
    Q_OBJECT

public:
    /// A null router is the honest "no sources at all" case and is shown as one,
    /// not treated as an error.
    explicit TelemetrySourceList(TelemetryRouter *router, QWidget *parent = nullptr);

private Q_SLOTS:
    /// Re-read every module's state. Cheap, and there are never many of them, so
    /// the whole list is refreshed rather than the one row that changed.
    void refresh();

private:
    void addRow(QVBoxLayout *layout, TelemetrySourceModule *module);

    TelemetryRouter *m_router = nullptr;

    /// One state label per module, which is the only part of a row that ever
    /// changes: the set of loaded sources is fixed for the life of the process.
    QHash<TelemetrySourceModule *, QLabel *> m_states;
};

} // namespace hidcenter
