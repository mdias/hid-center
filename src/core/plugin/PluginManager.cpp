// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/plugin/PluginManager.h"

#include "core/Logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QPluginLoader>
#include <QProcessEnvironment>

#ifndef HIDCENTER_PLUGIN_SUBDIR
#    define HIDCENTER_PLUGIN_SUBDIR "plugins"
#endif

namespace hidcenter {

PluginManager::PluginManager(QObject *parent)
    : QObject(parent)
{
}

PluginManager::~PluginManager() = default;

QStringList PluginManager::searchPaths()
{
    QStringList paths;

    const QString override =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("HIDCENTER_PLUGIN_PATH"));
    if (!override.isEmpty()) {
        for (const QString &entry : override.split(QDir::listSeparator(), Qt::SkipEmptyParts))
            paths << entry;
    }

    // Covers both the build tree (build/hid-center next to build/plugins) and a
    // relocatable install, without either needing special handling.
    paths << QCoreApplication::applicationDirPath() + QLatin1Char('/')
            + QLatin1String(HIDCENTER_PLUGIN_SUBDIR);

#ifdef HIDCENTER_INSTALL_PLUGIN_DIR
    paths << QLatin1String(HIDCENTER_INSTALL_PLUGIN_DIR);
#endif

    paths.removeDuplicates();
    return paths;
}

void PluginManager::loadPlugins()
{
    if (m_loaded)
        return;
    m_loaded = true;

    const QStringList paths = searchPaths();
    qCDebug(hidcenterPlugin) << "Plugin search paths:" << paths;

    for (const QString &path : paths)
        loadFromDirectory(path);

    qCInfo(hidcenterPlugin) << "Loaded" << m_plugins.size() << "device module(s) covering"
                         << m_byId.size() << "device id(s)";
}

void PluginManager::loadFromDirectory(const QString &path)
{
    QDir dir(path);
    if (!dir.exists()) {
        qCDebug(hidcenterPlugin) << "Skipping missing plugin directory" << path;
        return;
    }

    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (!QLibrary::isLibrary(entry.fileName()))
            continue;
        loadFile(entry.absoluteFilePath());
    }
}

void PluginManager::loadFile(const QString &filePath)
{
    const QString canonical = QFileInfo(filePath).canonicalFilePath();
    if (m_loadedFiles.contains(canonical)) {
        // The same module reachable through two search paths is not an error.
        qCDebug(hidcenterPlugin) << "Already loaded, skipping" << filePath;
        return;
    }

    auto *loader = new QPluginLoader(filePath, this);

    QObject *instance = loader->instance();
    if (!instance) {
        m_rejected.append({filePath, loader->errorString()});
        qCWarning(hidcenterPlugin) << "Not a usable plugin:" << filePath << "-"
                                << loader->errorString();
        delete loader;
        return;
    }

    auto *plugin = qobject_cast<IDevicePlugin *>(instance);
    if (!plugin) {
        const QString reason = QStringLiteral("does not implement %1")
                                   .arg(QLatin1String(HIDCENTER_DEVICE_PLUGIN_IID));
        m_rejected.append({filePath, reason});
        qCWarning(hidcenterPlugin) << filePath << reason;
        // Deliberately not unloaded: the library is already mapped and its
        // static objects constructed. Leaving it loaded is the safe choice.
        delete loader;
        return;
    }

    if (plugin->abiVersion() != kDevicePluginAbiVersion) {
        const QString reason = QStringLiteral("ABI version %1, expected %2 — rebuild the module")
                                   .arg(plugin->abiVersion())
                                   .arg(kDevicePluginAbiVersion);
        m_rejected.append({filePath, reason});
        qCWarning(hidcenterPlugin) << filePath << reason;
        delete loader;
        return;
    }

    LoadedPlugin loaded;
    loaded.plugin = plugin;
    loaded.fileName = filePath;
    loaded.familyName = plugin->familyName();
    loaded.supportedIds = plugin->supportedIds();

    if (loaded.supportedIds.isEmpty()) {
        qCWarning(hidcenterPlugin) << filePath << "claims no device ids and will never be used";
    }

    for (const UsbDeviceId &id : loaded.supportedIds) {
        if (IDevicePlugin *existing = m_byId.value(id)) {
            qCWarning(hidcenterPlugin)
                << "Device" << id.toString() << "is claimed by both" << existing->familyName()
                << "and" << loaded.familyName << "- keeping the first";
            continue;
        }
        m_byId.insert(id, plugin);
    }

    m_loaders.append(loader);
    m_loadedFiles.append(canonical);
    m_plugins.append(loaded);

    qCInfo(hidcenterPlugin) << "Loaded" << loaded.familyName << "from" << QFileInfo(filePath).fileName()
                         << "claiming" << loaded.supportedIds.size() << "device id(s)";
    for (const UsbDeviceId &id : loaded.supportedIds)
        qCDebug(hidcenterPlugin) << "  handles" << id.toString();
}

IDevicePlugin *PluginManager::pluginFor(const UsbDeviceId &id) const
{
    return m_byId.value(id, nullptr);
}

} // namespace hidcenter
