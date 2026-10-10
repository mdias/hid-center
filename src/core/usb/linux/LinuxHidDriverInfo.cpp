// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/usb/linux/LinuxHidDriverInfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cctype>

namespace hidcenter {
namespace {

/// Whether a capabilities bitmask names any effect at all.
///
/// The kernel prints these as space-separated hex longs, most significant group
/// first — "1047f0000 0" for a wheelbase with effects, "0" for a device with
/// none. Testing for a character other than '0' answers the only question being
/// asked without parsing hex or assuming how many groups there are, both of
/// which vary with the word size and the length of the bitmask.
bool namesAnyEffect(const QByteArray &mask)
{
    for (const char c : mask) {
        if (c != '0' && !std::isspace(static_cast<unsigned char>(c)))
            return true;
    }
    return false;
}

/// Whether the kernel exposes force-feedback effects for this HID node.
///
/// A node carries one input device per top-level collection it turns into one,
/// and only one of them is the wheel, so any is enough.
bool hasForceFeedback(const QString &hidNode)
{
    const QDir inputs(hidNode + QStringLiteral("/input"));
    const QStringList names =
        inputs.entryList({QStringLiteral("input*")}, QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &name : names) {
        QFile file(inputs.filePath(name) + QStringLiteral("/capabilities/ff"));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        if (namesAnyEffect(file.readAll()))
            return true;
    }

    // No input devices at all is a perfectly ordinary state — an unbound node,
    // or one whose driver exposes nothing but hidraw — and means the same thing
    // for our purposes as an empty mask.
    return false;
}

} // namespace

HidDriverInfo readHidDriver(const QString &hidSysPath)
{
    if (hidSysPath.isEmpty())
        return {};  // Nothing to look at; not the same as nothing bound.

    HidDriverInfo info;
    info.known = true;

    // Absent symlink means the node is bound to nothing. That is an answer, so
    // the driver name stays empty and `known` stays true.
    const QString target = QFileInfo(hidSysPath + QStringLiteral("/driver")).symLinkTarget();
    if (!target.isEmpty())
        info.driver = QFileInfo(target).fileName();

    info.forceFeedback = hasForceFeedback(hidSysPath);
    return info;
}

} // namespace hidcenter
