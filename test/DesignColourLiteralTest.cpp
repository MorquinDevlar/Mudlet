/***************************************************************************
 *   Copyright (C) 2026 by Vadim Peretokin - vadim.peretokin@mudlet.org    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

/*
 * Every colour on a surface that has adopted the design language comes from
 * uiDesign::themeTokens(), which mixes it from the palette in force. A colour
 * written out instead is right in whichever theme it was picked in and wrong in
 * the other one, and nothing says so until somebody opens the window in the
 * other appearance and reads grey on grey.
 *
 * So this reads the sources of those surfaces and fails on a colour that is
 * written rather than mixed. It links nothing: the src/ path arrives at
 * configure time through MUDLET_SRC_DIR, the way CMakeListsConsistencyTest and
 * DiscordTest take it.
 *
 * A colour that genuinely does not follow the theme - a well showing the colour
 * the user chose, a console's own ANSI table, another application's brand in a
 * picture of its window - carries "theme-fixed:" and the reason why, which
 * exempts it. The reason travels with the code rather than living in a list
 * here.
 *
 * Adding a window to the design language means adding its files to
 * scannedFiles() below.
 *
 * Run with: ctest -R DesignColourLiteralTest -V
 */

#include <QtTest/QtTest>

#include <cctype>

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

// QStringLiteral rather than utils.h's qsl(): like its two siblings this test
// links no Mudlet code, so that header is not on its include path.

class DesignColourLiteralTest : public QObject
{
    Q_OBJECT

    struct Hit
    {
        QString file;
        int line = 0;
        QString text;
        QString reason;
    };

    static QString srcDir() { return QStringLiteral(MUDLET_SRC_DIR); }

    // The surfaces the design language has reached, and nothing else: a window
    // that has not been through it still writes colours out, and failing on
    // those would say nothing about whether the design holds where it is meant
    // to. The .ui files are here because Designer writes colours as elements
    // and stylesheets of its own, which no C++ rule below would ever see.
    static QStringList scannedFiles()
    {
        QStringList files{QStringLiteral("uiDesign.cpp"),
                          QStringLiteral("uiDesign.h"),
                          QStringLiteral("dlgTriggerEditor.cpp"),
                          QStringLiteral("dlgTriggerEditor.h"),
                          QStringLiteral("dlgProfilePreferences.cpp"),
                          QStringLiteral("dlgProfilePreferences.h"),
                          QStringLiteral("EditorTreeDelegate.cpp"),
                          QStringLiteral("EditorTreeDelegate.h"),
                          QStringLiteral("SearchResultDelegate.cpp"),
                          QStringLiteral("SearchResultDelegate.h"),
                          QStringLiteral("SidebarItemDelegate.cpp"),
                          QStringLiteral("SidebarItemDelegate.h"),
                          QStringLiteral("GripSplitter.cpp"),
                          QStringLiteral("GripSplitter.h"),
                          QStringLiteral("EditorSidebarToggle.cpp"),
                          QStringLiteral("EditorSidebarToggle.h"),
                          QStringLiteral("EditorPlaceholderButton.cpp"),
                          QStringLiteral("EditorPlaceholderButton.h"),
                          QStringLiteral("dlgColorTrigger.cpp"),
                          QStringLiteral("TDetachedWindow.cpp"),
                          QStringLiteral("mudlet.cpp"),
                          QStringLiteral("ui/trigger_editor.ui"),
                          QStringLiteral("ui/triggers_main_area.ui"),
                          QStringLiteral("ui/trigger_pattern_edit.ui"),
                          QStringLiteral("ui/color_trigger.ui"),
                          QStringLiteral("ui/aliases_main_area.ui"),
                          QStringLiteral("ui/timers_main_area.ui"),
                          QStringLiteral("ui/scripts_main_area.ui"),
                          QStringLiteral("ui/keybindings_main_area.ui"),
                          QStringLiteral("ui/actions_main_area.ui"),
                          QStringLiteral("ui/vars_main_area.ui")};
        // The settings dialog is one .ui file today and may not stay one, so
        // its pages are taken by pattern rather than named
        const QDir uiDir(srcDir() + QStringLiteral("/ui"));
        for (const QString& page : uiDir.entryList({QStringLiteral("profile_preferences*.ui")}, QDir::Files, QDir::Name)) {
            files.append(QStringLiteral("ui/%1").arg(page));
        }
        return files;
    }

    // What exempts a line, and how far it reaches. Trailing on a line it is
    // that line's; on a line of its own it covers the run of lines under it, up
    // to the next blank one - which is the only way to mark a block such as a
    // table of ANSI defaults without repeating the reason on every row of it.
    static constexpr char scmMarker[] = "theme-fixed:";

    // The colour names a stylesheet can be written in. Not the whole SVG set:
    // the ones a person reaches for when they mean "make this grey", plus
    // enough of the rest that a copied rule does not slip through.
    static const QRegularExpression& cssColourName()
    {
        static const QRegularExpression pattern(
                QStringLiteral("\\b(?:black|white|gray|grey|darkgray|darkgrey|lightgray|lightgrey|dimgray|dimgrey|silver|red|darkred|crimson|salmon|tomato|coral|orange|gold|yellow|khaki|olive|"
                               "lime|green|darkgreen|teal|aqua|cyan|turquoise|azure|navy|blue|darkblue|indigo|violet|purple|fuchsia|magenta|orchid|plum|thistle|lavender|pink|brown|maroon|"
                               "sienna|peru|tan|wheat|beige|ivory|linen|snow)\\b"),
                QRegularExpression::CaseInsensitiveOption);
        return pattern;
    }

    // Qt::white, Qt::black and Qt::transparent are the ends of the scale rather
    // than colours of a theme, so they are let through where they are used as
    // such - see exemptEndpoints() - and caught everywhere else.
    static const QRegularExpression& qtColourName()
    {
        static const QRegularExpression pattern(
                QStringLiteral("Qt::(white|black|red|green|blue|cyan|magenta|yellow|gray|darkGray|lightGray|darkRed|darkGreen|darkBlue|darkCyan|darkMagenta|darkYellow)\\b"));
        return pattern;
    }

    // A hex colour is three, four, six or eight digits; anything else after a
    // "#" is an issue number, an object name or a CSS id
    static QString hexColourIn(const QString& text)
    {
        for (qsizetype start = text.indexOf(QLatin1Char('#')); start >= 0; start = text.indexOf(QLatin1Char('#'), start + 1)) {
            qsizetype end = start + 1;
            while (end < text.size() && std::isxdigit(static_cast<unsigned char>(text.at(end).toLatin1()))) {
                ++end;
            }
            const qsizetype digits = end - start - 1;
            if (digits == 3 || digits == 4 || digits == 6 || digits == 8) {
                return text.mid(start, end - start);
            }
        }
        return QString();
    }

    // What is inside the double quotes on a line, which is where a colour that
    // reaches a stylesheet has to be written. A line of a raw string literal is
    // handed here whole instead, by the caller.
    static QStringList stringLiteralsIn(const QString& line)
    {
        QStringList literals;
        bool open = false;
        qsizetype from = 0;
        for (qsizetype at = 0; at < line.size(); ++at) {
            const QChar character = line.at(at);
            if (character == QLatin1Char('\\') && open) {
                ++at;
                continue;
            }
            if (character != QLatin1Char('"')) {
                continue;
            }
            if (open) {
                literals.append(line.mid(from, at - from));
                open = false;
            } else {
                open = true;
                from = at + 1;
            }
        }
        return literals;
    }

    // A colour written into a stylesheet: the hex, or the name in a declaration
    // that is about colour at all - "black" on its own is as likely to be a
    // word as a colour.
    static QString colourInStrings(const QStringList& literals)
    {
        for (const QString& literal : literals) {
            if (const QString hex = hexColourIn(literal); !hex.isEmpty()) {
                return QStringLiteral("the colour %1 written into a string").arg(hex);
            }
            if (!literal.contains(QStringLiteral("color:")) && !literal.contains(QStringLiteral("background"))) {
                continue;
            }
            if (const QRegularExpressionMatch match = cssColourName().match(literal); match.hasMatch()) {
                return QStringLiteral("the colour name %1 written into a stylesheet").arg(match.captured(0));
            }
        }
        return QString();
    }

    // Qt::transparent is never a theme colour wherever it appears, and the two
    // ends of the lightness scale are not one where they are what something is
    // mixed towards or a mask is cleared with. Read off the line rather than
    // allowed outright, so that Qt::white as an ink is still caught.
    static QString exemptEndpoints(const QString& line)
    {
        QString probe = line;
        probe.replace(QStringLiteral("Qt::transparent"), QStringLiteral("themeFixedEndpoint"));
        if (probe.contains(QStringLiteral("blend(")) || probe.contains(QStringLiteral("fill("))) {
            probe.replace(QStringLiteral("Qt::white"), QStringLiteral("themeFixedEndpoint"));
            probe.replace(QStringLiteral("Qt::black"), QStringLiteral("themeFixedEndpoint"));
        }
        return probe;
    }

    static QString colourInCode(const QString& line)
    {
        const QString probe = exemptEndpoints(line);

        static const QRegularExpression fromQtName(QStringLiteral("QColor\\s*\\(\\s*Qt::"));
        if (fromQtName.match(probe).hasMatch()) {
            return QStringLiteral("a QColor built from a Qt colour name");
        }
        // A Qt colour name reaching anything that paints with it. The call and
        // the name are looked for separately because the two are routinely a
        // couple of nested calls apart - setForeground(0, QBrush(Qt::gray)).
        static const QRegularExpression painter(QStringLiteral("\\b(?:QColor|QBrush|QPen|setColor|setForeground|setBackground)\\s*\\("));
        if (const QRegularExpressionMatch named = qtColourName().match(probe); named.hasMatch() && painter.match(probe).hasMatch()) {
            return QStringLiteral("Qt::%1 handed to something that paints with it").arg(named.captured(1));
        }
        static const QRegularExpression fromString(QStringLiteral("QColor\\s*\\(\\s*(?:qsl|QStringLiteral|QLatin1String)?\\s*\\(?\\s*\""));
        if (fromString.match(probe).hasMatch()) {
            return QStringLiteral("a QColor built from a written colour");
        }
        // fromHslF() and fromHsvF() are deliberately not here: a semantic state
        // hue is made that way, with the lightness taken off the page it will
        // be drawn on
        static const QRegularExpression fromChannels(QStringLiteral("QColor::fromRgb\\s*\\(|QColor\\s*\\(\\s*\\d+\\s*,\\s*\\d+\\s*,\\s*\\d+"));
        if (fromChannels.match(probe).hasMatch()) {
            return QStringLiteral("a QColor built from written channel values");
        }
        static const QRegularExpression constant(QStringLiteral("QColorConstants::(\\w+)"));
        if (const QRegularExpressionMatch named = constant.match(probe); named.hasMatch() && named.captured(1) != QStringLiteral("Transparent")) {
            return QStringLiteral("QColorConstants::%1").arg(named.captured(1));
        }
        return QString();
    }

    // Which line an exemption for this one would have to be written on. A
    // colour inside a raw string literal cannot carry a comment of its own -
    // anything after the opening delimiter is content - so the literal is
    // marked where it opens, and one marker covers all of it.
    static int anchorOf(const QStringList& lines, const int line)
    {
        int anchor = line;
        bool insideRawString = false;
        for (int at = 1; at <= line; ++at) {
            const QString& text = lines.at(at - 1);
            if (insideRawString) {
                if (text.contains(QStringLiteral(")\""))) {
                    insideRawString = false;
                }
                continue;
            }
            const qsizetype opened = text.indexOf(QStringLiteral("R\"("));
            if (opened >= 0 && text.indexOf(QStringLiteral(")\""), opened + 3) < 0) {
                insideRawString = true;
                anchor = at;
            }
        }
        return insideRawString ? anchor : line;
    }

    static bool exempted(const QStringList& lines, const int line)
    {
        if (lines.at(line - 1).contains(QLatin1String(scmMarker))) {
            return true;
        }
        // Up from the marked line to the blank one that ends the run it is in
        for (int at = anchorOf(lines, line); at >= 1; --at) {
            const QString& text = lines.at(at - 1);
            if (text.trimmed().isEmpty()) {
                return false;
            }
            if (text.contains(QLatin1String(scmMarker))) {
                return true;
            }
        }
        return false;
    }

    static void scanSource(const QString& file, const QStringList& lines, QList<Hit>& hits)
    {
        bool insideRawString = false;
        for (int number = 1; number <= lines.size(); ++number) {
            const QString& raw = lines.at(number - 1);
            const bool wasInsideRawString = insideRawString;
            if (insideRawString) {
                insideRawString = !raw.contains(QStringLiteral(")\""));
            } else if (const qsizetype opened = raw.indexOf(QStringLiteral("R\"(")); opened >= 0 && raw.indexOf(QStringLiteral(")\""), opened + 3) < 0) {
                insideRawString = true;
            }

            // Inside a raw string every character is content, so the whole line
            // is read as one literal rather than picked apart by its quotes
            QString reason = colourInStrings(wasInsideRawString ? QStringList{raw} : stringLiteralsIn(raw));
            if (reason.isEmpty() && !wasInsideRawString) {
                reason = colourInCode(raw);
            }
            if (reason.isEmpty() || exempted(lines, number)) {
                continue;
            }
            hits.append({file, number, raw.trimmed(), reason});
        }
    }

    static void scanDesignerFile(const QString& file, const QStringList& lines, QList<Hit>& hits)
    {
        bool insideStyleSheet = false;
        for (int number = 1; number <= lines.size(); ++number) {
            const QString& raw = lines.at(number - 1);
            QString reason;
            // A colour Designer wrote as a palette entry, which is three
            // channel elements under a <color> of its own
            if (raw.contains(QStringLiteral("<color"))) {
                for (qsizetype ahead = number; ahead < std::min<qsizetype>(number + 5, lines.size()); ++ahead) {
                    if (lines.at(ahead).contains(QStringLiteral("<red>"))) {
                        reason = QStringLiteral("a colour set in Designer");
                        break;
                    }
                }
            }
            if (raw.contains(QStringLiteral("name=\"styleSheet\""))) {
                insideStyleSheet = true;
            } else if (insideStyleSheet) {
                if (raw.contains(QStringLiteral("</property>"))) {
                    insideStyleSheet = false;
                } else if (reason.isEmpty()) {
                    reason = colourInStrings({raw});
                }
            }
            if (reason.isEmpty() || exempted(lines, number)) {
                continue;
            }
            hits.append({file, number, raw.trimmed(), reason});
        }
    }

private slots:
    // One case, one list: a run that stopped at the first colour would take as
    // many runs to clear as there are colours
    void test_everyColourOnADesignedSurfaceComesFromTheTokens()
    {
        const QStringList files = scannedFiles();
        QVERIFY2(files.size() > 20, "the list of scanned files is too short to be the design language's surfaces");

        QList<Hit> hits;
        int scannedLines = 0;
        for (const QString& file : files) {
            QFile source(QStringLiteral("%1/%2").arg(srcDir(), file));
            QVERIFY2(source.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(QStringLiteral("src/%1 is on the scan list and could not be read - it has been renamed or removed").arg(file)));
            const QStringList lines = QString::fromUtf8(source.readAll()).split(QChar::LineFeed);
            scannedLines += lines.size();
            if (file.endsWith(QStringLiteral(".ui"))) {
                scanDesignerFile(QStringLiteral("src/%1").arg(file), lines, hits);
            } else {
                scanSource(QStringLiteral("src/%1").arg(file), lines, hits);
            }
        }

        qInfo().noquote() << QStringLiteral("  %1 lines over %2 files").arg(QString::number(scannedLines), QString::number(files.size()));

        // Printed one to a line before the failure as well as gathered into it:
        // QtTest cuts a failure message off at a few hundred characters, and a
        // run that named three of forty would take thirteen runs to clear
        for (const Hit& hit : hits) {
            qWarning().noquote() << QStringLiteral("%1:%2: %3   [%4]").arg(hit.file, QString::number(hit.line), hit.text, hit.reason);
        }
        QVERIFY2(hits.isEmpty(),
                 qPrintable(QStringLiteral("%1 colour(s) written out rather than taken from uiDesign::themeTokens() - listed above. Mix each from the tokens, or - if it is a value being shown "
                                           "rather than chrome - write \"// theme-fixed: <why>\" on the line.")
                                    .arg(QString::number(hits.size()))));
    }
};

QTEST_GUILESS_MAIN(DesignColourLiteralTest)

#include "DesignColourLiteralTest.moc"
