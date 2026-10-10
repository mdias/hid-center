// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/telemetry/plugin/TelemetryPluginManager.h"

#include "core/Logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QPluginLoader>
#include <QProcessEnvironment>

#ifndef HIDCENTER_TELEMETRY_PLUGIN_SUBDIR
#    define HIDCENTER_TELEMETRY_PLUGIN_SUBDIR "plugins/telemetry"
#endif

namespace hidcenter {

TelemetryPluginManager::TelemetryPluginManager(QObject *parent)
    : QObject(parent)
{
}

TelemetryPluginManager::~TelemetryPluginManager() = default;

QStringList TelemetryPluginManager::searchPaths()
{
    QStringList paths;

    const QString override = QProcessEnvironment::systemEnvironment().value(
        QStringLiteral("HIDCENTER_TELEMETRY_PLUGIN_PATH"));
    if (!override.isEmpty()) {
        for (const QString &entry : override.split(QDir::listSeparator(), Qt::SkipEmptyParts))
            paths << entry;
    }

    // Covers both the build tree (build/hid-center next to build/plugins/telemetry)
    // and a relocatable install, without either needing special handling.
    paths << QCoreApplication::applicationDirPath() + QLatin1Char('/')
            + QLatin1String(HIDCENTER_TELEMETRY_PLUGIN_SUBDIR);

#ifdef HIDCENTER_INSTALL_TELEMETRY_PLUGIN_DIR
    paths << QLatin1String(HIDCENTER_INSTALL_TELEMETRY_PLUGIN_DIR);
#endif

    paths.removeDuplicates();
    return paths;
}

QVector<ITelemetrySourcePlugin *> TelemetryPluginManager::sourcePlugins() const
{
    QVector<ITelemetrySourcePlugin *> plugins;
    plugins.reserve(m_plugins.size());
    for (const LoadedSourcePlugin &loaded : m_plugins)
        plugins.append(loaded.plugin);
    return plugins;
}

void TelemetryPluginManager::loadPlugins()
{
    if (m_loaded)
        return;
    m_loaded = true;

    const QStringList paths = searchPaths();
    qCDebug(hidcenterTelemetry) << "Telemetry source search paths:" << paths;

    for (const QString &path : paths)
        loadFromDirectory(path);

    if (m_plugins.isEmpty()) {
        // Worth a warning rather than an info line: with none of these, the
        // application has no way to learn anything about a car at all.
        qCWarning(hidcenterTelemetry).noquote()
            << QStringLiteral("No telemetry source modules were loaded; nothing will be "
                              "listened for. Searched:\n%1")
                   .arg(paths.join(QLatin1Char('\n')));
        return;
    }

    qCInfo(hidcenterTelemetry) << "Loaded" << m_plugins.size() << "telemetry source module(s)";
}

void TelemetryPluginManager::loadFromDirectory(const QString &path)
{
    QDir dir(path);
    if (!dir.exists()) {
        qCDebug(hidcenterTelemetry) << "Skipping missing telemetry source directory" << path;
        return;
    }

    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (!QLibrary::isLibrary(entry.fileName()))
            continue;
        loadFile(entry.absoluteFilePath());
    }
}

void TelemetryPluginManager::loadFile(const QString &filePath)
{
    const QString canonical = QFileInfo(filePath).canonicalFilePath();
    if (m_loadedFiles.contains(canonical)) {
        // The same module reachable through two search paths is not an error.
        qCDebug(hidcenterTelemetry) << "Already loaded, skipping" << filePath;
        return;
    }

    auto *loader = new QPluginLoader(filePath, this);

    QObject *instance = loader->instance();
    if (!instance) {
        qCWarning(hidcenterTelemetry) << "Not a usable telemetry source:" << filePath << "-"
                                   << loader->errorString();
        delete loader;
        return;
    }

    auto *plugin = qobject_cast<ITelemetrySourcePlugin *>(instance);
    if (!plugin) {
        qCWarning(hidcenterTelemetry)
            << filePath << "does not implement" << HIDCENTER_TELEMETRY_SOURCE_PLUGIN_IID;
        // Deliberately not unloaded: the library is already mapped and its
        // static objects constructed. Leaving it loaded is the safe choice.
        delete loader;
        return;
    }

    if (plugin->abiVersion() != kTelemetrySourcePluginAbiVersion) {
        qCWarning(hidcenterTelemetry)
            << filePath << "has ABI version" << plugin->abiVersion() << ", expected"
            << kTelemetrySourcePluginAbiVersion << "- rebuild the module";
        delete loader;
        return;
    }

    LoadedSourcePlugin loaded;
    loaded.plugin = plugin;
    loaded.fileName = filePath;
    loaded.sourceId = plugin->sourceId();
    loaded.displayName = plugin->displayName();

    for (const LoadedSourcePlugin &existing : m_plugins) {
        if (existing.sourceId != loaded.sourceId)
            continue;

        // Two modules for one sim would both bind the same port, and the second
        // would fail in a way that reads like a running game holding it.
        qCWarning(hidcenterTelemetry) << "Telemetry source" << loaded.sourceId
                                   << "is claimed by both" << existing.displayName << "and"
                                   << loaded.displayName << "- keeping the first";
        delete loader;
        return;
    }

    m_loaders.append(loader);
    m_loadedFiles.append(canonical);
    m_plugins.append(loaded);

    qCInfo(hidcenterTelemetry) << "Loaded telemetry source" << loaded.displayName << "from"
                            << QFileInfo(filePath).fileName();
}

} // namespace hidcenter
