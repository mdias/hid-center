// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QStringList>
#include <QVector>

#include "core/telemetry/plugin/ITelemetrySourcePlugin.h"
#include "hidcenter/core_export.h"

class QPluginLoader;

namespace hidcenter {

/// Finds and loads the telemetry-source modules.
///
/// A second loader beside PluginManager rather than a generalisation of it, and
/// with its own directory — `plugins/telemetry/` — so the two never see each
/// other's files. Sharing a directory would mean every telemetry module failing
/// the device loader's cast and every device module failing this one's, which is
/// a lot of noise to produce in order to save one directory.
///
/// Modules are never unloaded, for the reason PluginManager gives: their
/// vtables and QObjects stay referenced for as long as the application runs.
///
/// A module that cannot be used is logged and dropped. There is no rejected()
/// list to match PluginManager's: a telemetry module is not something a user
/// picks or expects by name, so the log is where a failed one belongs.
class HIDCENTER_CORE_EXPORT TelemetryPluginManager : public QObject
{
    Q_OBJECT

public:
    struct LoadedSourcePlugin
    {
        ITelemetrySourcePlugin *plugin = nullptr;
        QString fileName;     ///< Full path, for the log.
        QString sourceId;     ///< Cached, so the vtable is not re-entered to sort a list.
        QString displayName;
    };

    explicit TelemetryPluginManager(QObject *parent = nullptr);
    ~TelemetryPluginManager() override;

    /// Where modules are looked for, in order: $HIDCENTER_TELEMETRY_PLUGIN_PATH,
    /// then `<application dir>/plugins/telemetry`, then the install prefix.
    ///
    /// Static so the "nothing loaded" case can say where it looked without
    /// having a manager to hand.
    static QStringList searchPaths();

    /// Load everything found. Idempotent; the second call does nothing.
    void loadPlugins();

    const QVector<LoadedSourcePlugin> &plugins() const { return m_plugins; }

    /// The loaded interfaces alone, which is all TelemetryRouter wants.
    QVector<ITelemetrySourcePlugin *> sourcePlugins() const;

private:
    void loadFromDirectory(const QString &path);
    void loadFile(const QString &filePath);

    QVector<LoadedSourcePlugin> m_plugins;
    QVector<QPluginLoader *> m_loaders;
    QStringList m_loadedFiles;  ///< Canonical paths, for the dedupe.
    bool m_loaded = false;
};

} // namespace hidcenter
