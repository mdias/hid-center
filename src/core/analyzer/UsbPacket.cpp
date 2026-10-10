// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/analyzer/UsbPacket.h"

namespace hidcenter {
namespace {

/// -1 for anything that is not a hex digit.
int hexValue(QChar c)
{
    if (c >= u'0' && c <= u'9')
        return c.unicode() - u'0';
    if (c >= u'a' && c <= u'f')
        return c.unicode() - u'a' + 10;
    if (c >= u'A' && c <= u'F')
        return c.unicode() - u'A' + 10;
    return -1;
}

} // namespace

QByteArray parseHexBytes(const QString &text, QString *error)
{
    if (error)
        error->clear();

    // Untranslated, like every other error string core hands up. They reach the
    // user through the same route IUsbDeviceHidInterface::lastError() does.
    const auto fail = [error](QStringView token, const char *what) {
        if (error)
            *error = QStringLiteral("\"%1\" %2.").arg(token.toString(), QLatin1String(what));
        return QByteArray();
    };

    QByteArray bytes;

    qsizetype at = 0;
    while (at < text.size()) {
        // Hand-rolled rather than QString::split, which splits on one separator:
        // hex arrives pasted, and a paste carries tabs and newlines as readily
        // as spaces.
        while (at < text.size() && text.at(at).isSpace())
            ++at;
        if (at >= text.size())
            break;

        const qsizetype start = at;
        while (at < text.size() && !text.at(at).isSpace())
            ++at;
        const QStringView token = QStringView(text).sliced(start, at - start);

        QStringView digits = token;
        if (digits.size() > 2 && digits.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
            digits = digits.sliced(2);

        // A lone digit is a byte, so "f" and "0f" agree. Past that a byte is two
        // digits, and an odd count means a digit went missing rather than that
        // half a byte was meant.
        if (digits.size() == 1) {
            const int only = hexValue(digits.at(0));
            if (only < 0)
                return fail(token, "is not a hex byte");
            bytes.append(char(only));
            continue;
        }

        if (digits.size() % 2 != 0)
            return fail(token, "has an odd number of hex digits");

        for (qsizetype i = 0; i < digits.size(); i += 2) {
            const int high = hexValue(digits.at(i));
            const int low = hexValue(digits.at(i + 1));
            if (high < 0 || low < 0)
                return fail(token, "is not a hex byte");
            bytes.append(char((high << 4) | low));
        }
    }

    return bytes;
}

} // namespace hidcenter
