// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/usb/UsbDeviceInfo.h"
#include "core/plugin/IDevicePlugin.h"
#include "hidcenter/core_export.h"

class QPluginLoader;

namespace hidcenter {

/// Finds and loads device-family modules, and maps VID/PID pairs onto them.
///
/// Modules are never unloaded. Their widgets, QObjects and vtables stay
/// referenced for as long as the application runs, and unloading a plugin whose
/// objects are still alive is a reliable way to crash at exit.
class HIDCENTER_CORE_EXPORT PluginManager : public QObject
{
    Q_OBJECT

public:
    struct LoadedPlugin
    {
        IDevicePlugin *plugin = nullptr;
        QString fileName;
        QString familyName;
        QVector<UsbDeviceId> supportedIds;
    };

    /// A module that was found but rejected, kept so the Home page can explain
    /// why a device the user expected is missing.
    struct RejectedPlugin
    {
        QString fileName;
        QString reason;
    };

    explicit PluginManager(QObject *parent = nullptr);
    ~PluginManager() override;

    /// Directories searched, in order:
    ///   1. every entry of $HIDCENTER_PLUGIN_PATH
    ///   2. <application dir>/plugins   (the build tree, and a relocatable install)
    ///   3. the configured install prefix
    static QStringList searchPaths();

    /// Load everything found. Safe to call once; later calls are ignored.
    void loadPlugins();

    const QVector<LoadedPlugin> &plugins() const { return m_plugins; }
    const QVector<RejectedPlugin> &rejected() const { return m_rejected; }

    /// The module claiming this VID/PID, or nullptr when the device is unknown.
    IDevicePlugin *pluginFor(const UsbDeviceId &id) const;

    bool handles(const UsbDeviceId &id) const { return m_byId.contains(id); }

private:
    void loadFromDirectory(const QString &path);
    void loadFile(const QString &filePath);

    QVector<LoadedPlugin> m_plugins;
    QVector<RejectedPlugin> m_rejected;
    QHash<UsbDeviceId, IDevicePlugin *> m_byId;
    QVector<QPluginLoader *> m_loaders;
    QStringList m_loadedFiles;
    bool m_loaded = false;
};

} // namespace hidcenter
