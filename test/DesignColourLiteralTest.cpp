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
 * written rather than mixed, in whichever shape it was written in: a hex, the
 * channels of an rgb() or rgba(), a Qt colour name, a QColor built from any of
 * those, a CSS colour word inside a declaration, or a literal that is a colour
 * name and nothing else - which is how one reaches a sheet through .arg(). It
 * links nothing: the src/ path arrives at configure time through
 * MUDLET_SRC_DIR, the way CMakeListsConsistencyTest and DiscordTest take it.
 *
 * A colour that genuinely does not follow the theme - a well showing the colour
 * the user chose, a console's own ANSI table, another application's brand in a
 * picture of its window - carries "theme-fixed:" and the reason why, which
 * exempts it. The reason travels with the code rather than living in a list
 * here.
 *
 * The second case reads the same surfaces for the other half of the same rule.
 * A sheet names a font size only through uiDesign::typeSize(), which reaches it
 * as a pt value: Qt's stylesheet parser reads pt and px for font-size and
 * nothing else, so a percentage is a rule that reads as though it sets a size
 * and sets none - and a written number is a size somebody picked rather than
 * one of the four steps the design has. The same "theme-fixed:" marker exempts
 * a block that is a picture of another application, whose sizes are as fixed as
 * its colours.
 *
 * Adding a window to the design language means adding its files to
 * scannedFiles() below.
 *
 * Run with: ctest -R DesignColourLiteralTest -V
 */

#include <QtTest/QtTest>

#include <cctype>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>

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
                          QStringLiteral("EditorTreeRowMetrics.cpp"),
                          QStringLiteral("EditorTreeRowMetrics.h"),
                          QStringLiteral("VariableTreeDelegate.cpp"),
                          QStringLiteral("VariableTreeDelegate.h"),
                          QStringLiteral("SearchResultDelegate.cpp"),
                          QStringLiteral("SearchResultDelegate.h"),
                          QStringLiteral("SidebarItemDelegate.cpp"),
                          QStringLiteral("SidebarItemDelegate.h"),
                          QStringLiteral("TTabBar.cpp"),
                          QStringLiteral("TTabBar.h"),
                          QStringLiteral("GripSplitter.cpp"),
                          QStringLiteral("GripSplitter.h"),
                          QStringLiteral("ChipRow.cpp"),
                          QStringLiteral("ChipRow.h"),
                          QStringLiteral("FlowLayout.cpp"),
                          QStringLiteral("FlowLayout.h"),
                          QStringLiteral("SidebarToggle.cpp"),
                          QStringLiteral("SidebarToggle.h"),
                          QStringLiteral("EditorPlaceholderButton.cpp"),
                          QStringLiteral("EditorPlaceholderButton.h"),
                          QStringLiteral("dlgAboutDialog.cpp"),
                          QStringLiteral("dlgAboutDialog.h"),
                          QStringLiteral("AboutLinkButton.cpp"),
                          QStringLiteral("AboutLinkButton.h"),
                          QStringLiteral("AboutSupporterBanner.cpp"),
                          QStringLiteral("AboutSupporterBanner.h"),
                          QStringLiteral("dlgConnectionProfiles.cpp"),
                          QStringLiteral("dlgConnectionProfiles.h"),
                          QStringLiteral("dlgNotepad.cpp"),
                          QStringLiteral("dlgNotepad.h"),
                          QStringLiteral("dlgColorTrigger.cpp"),
                          QStringLiteral("dlgSystemMessageArea.cpp"),
                          QStringLiteral("TDetachedWindow.cpp"),
                          QStringLiteral("mudlet.cpp"),
                          QStringLiteral("ui/trigger_editor.ui"),
                          QStringLiteral("ui/triggers_main_area.ui"),
                          QStringLiteral("ui/trigger_pattern_edit.ui"),
                          QStringLiteral("ui/color_trigger.ui"),
                          QStringLiteral("ui/connection_profiles.ui"),
                          QStringLiteral("ui/system_message_area.ui"),
                          QStringLiteral("ui/aliases_main_area.ui"),
                          QStringLiteral("ui/timers_main_area.ui"),
                          QStringLiteral("ui/scripts_main_area.ui"),
                          QStringLiteral("ui/keybindings_main_area.ui"),
                          QStringLiteral("ui/actions_main_area.ui"),
                          QStringLiteral("ui/vars_main_area.ui"),
                          QStringLiteral("ui/notes_editor.ui"),
                          QStringLiteral("ui/about_dialog.ui")};
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

    // Every colour name Qt itself knows, taken off QColor at run time rather
    // than written out here: a list kept by hand is a list of the names
    // somebody thought of, and "lightsalmon" is a colour whether or not anybody
    // did. "transparent" is not one of them - it is the absence of a colour,
    // and every sheet in the tree that clears a background says it.
    static const QSet<QString>& namedColours()
    {
        static const QSet<QString> names = [] {
            QSet<QString> gathered;
            for (const QString& name : QColor::colorNames()) {
                if (name.compare(QStringLiteral("transparent"), Qt::CaseInsensitive) != 0) {
                    gathered.insert(name.toLower());
                }
            }
            return gathered;
        }();
        return names;
    }

    // ...and the same set as one alternation, for reading a name out of the
    // middle of a declaration. Longest first, so that "darkgreen" is read whole
    // rather than as "green" with a tail left over.
    static const QRegularExpression& cssColourName()
    {
        static const QRegularExpression pattern = [] {
            QStringList names(namedColours().cbegin(), namedColours().cend());
            std::sort(names.begin(), names.end(), [](const QString& one, const QString& other) {
                return one.size() != other.size() ? one.size() > other.size() : one < other;
            });
            return QRegularExpression(QStringLiteral("\\b(?:%1)\\b").arg(names.join(QLatin1Char('|'))), QRegularExpression::CaseInsensitiveOption);
        }();
        return pattern;
    }

    // The colour names Qt's own enum carries, which is the other half of the
    // same list
    static QString qtColourNames() { return QStringLiteral("white|black|red|green|blue|cyan|magenta|yellow|gray|darkGray|lightGray|darkRed|darkGreen|darkBlue|darkCyan|darkMagenta|darkYellow"); }

    // Qt::white, Qt::black and Qt::transparent are the ends of the scale rather
    // than colours of a theme, so they are let through where they are used as
    // such - see exemptEndpoints() - and caught everywhere else.
    static const QRegularExpression& qtColourName()
    {
        static const QRegularExpression pattern(QStringLiteral("Qt::(%1)\\b").arg(qtColourNames()));
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

    // A string literal, and whether it is copy. What tr() is handed is read by
    // a person, and a stylesheet value is never translated - so a translated
    // literal reading "Black" is the name of a thing rather than a colour on
    // its way into a sheet, while a declaration written inside one still is.
    struct Literal
    {
        QString text;
        bool translated = false;
    };

    // What is inside the double quotes on a line, which is where a colour that
    // reaches a stylesheet has to be written. A line of a raw string literal is
    // handed here whole instead, by the caller.
    static QList<Literal> stringLiteralsIn(const QString& line)
    {
        static const QRegularExpression translating(QStringLiteral("\\b(?:tr|translate|QT_TR_NOOP|QT_TRANSLATE_NOOP)\\s*\\(\\s*$"));

        QList<Literal> literals;
        bool open = false;
        bool translated = false;
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
                literals.append({line.mid(from, at - from), translated});
                open = false;
            } else {
                open = true;
                from = at + 1;
                translated = translating.match(line.left(at)).hasMatch();
            }
        }
        return literals;
    }

    // Channels written out into a string - rgb(64, 60, 40), rgba(255, 254,
    // 215, 0.5) - which no hex rule sees. A digit has to follow the paren, so
    // the format template uiDesign::rgba() fills in, "rgba(%1, %2, %3, %4)",
    // is not one of these.
    static const QRegularExpression& channelsInString()
    {
        static const QRegularExpression pattern(QStringLiteral("\\brgba?\\s*\\(\\s*\\d"));
        return pattern;
    }

    // A literal that is a colour name and nothing else, which is how a name
    // reaches a sheet through .arg() - qsl("lightsalmon") - with the
    // declaration it lands in written somewhere else entirely.
    static bool wholeLiteralIsAColourName(const QString& literal) { return namedColours().contains(literal.trimmed().toLower()); }

    // A colour written into a stylesheet: the hex, the channels, a literal that
    // is a colour name outright, or the name in a declaration that is about
    // colour at all - "black" in the middle of a sentence is as likely to be a
    // word as a colour.
    static QString colourInStrings(const QList<Literal>& literals)
    {
        for (const Literal& literal : literals) {
            if (const QString hex = hexColourIn(literal.text); !hex.isEmpty()) {
                return QStringLiteral("the colour %1 written into a string").arg(hex);
            }
            if (channelsInString().match(literal.text).hasMatch()) {
                return QStringLiteral("a colour written channel by channel into a string");
            }
            if (!literal.translated && wholeLiteralIsAColourName(literal.text)) {
                return QStringLiteral("the colour name %1 written as a string of its own").arg(literal.text.trimmed());
            }
            if (!literal.text.contains(QStringLiteral("color:")) && !literal.text.contains(QStringLiteral("background"))) {
                continue;
            }
            if (const QRegularExpressionMatch match = cssColourName().match(literal.text); match.hasMatch()) {
                return QStringLiteral("the colour name %1 written into a stylesheet").arg(match.captured(0));
            }
        }
        return QString();
    }

    // A size written into a stylesheet, in either of the two shapes that are
    // not a step of the scale.
    //
    // A percentage, because Qt's stylesheet parser reads pt and px for
    // font-size and nothing else: it drops a percentage without a word, which
    // is how 28 rules across three windows came to say nothing at all while
    // reading as though they set a size.
    //
    // And a number, because a written point size is a size somebody picked, and
    // the next person picks a different one a pixel away from it. The four the
    // design has are uiDesign::typeSize(), which reaches a sheet through .arg()
    // - so "font-size: %1pt" is what a rule naming a size looks like, and
    // anything with a digit or a per cent sign after the colon is not.
    static const QRegularExpression& writtenFontSize()
    {
        static const QRegularExpression pattern(QStringLiteral("font-size\\s*:\\s*[0-9]"));
        return pattern;
    }

    // "%" not followed by a digit, so that the "%1" of a template is not read
    // as the tail of a percentage
    static const QRegularExpression& percentageFontSize()
    {
        static const QRegularExpression pattern(QStringLiteral("font-size\\s*:\\s*[^;}]*%(?![0-9])"));
        return pattern;
    }

    static QString sizeInStrings(const QList<Literal>& literals)
    {
        for (const Literal& literal : literals) {
            if (literal.translated) {
                continue;
            }
            if (percentageFontSize().match(literal.text).hasMatch()) {
                return QStringLiteral("a font-size in per cent, which Qt's stylesheet parser drops without applying it");
            }
            if (writtenFontSize().match(literal.text).hasMatch()) {
                return QStringLiteral("a font-size written as a number rather than taken from uiDesign::typeSize()");
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
        static const QRegularExpression painter(QStringLiteral("\\b(?:QColor|QBrush|QPen|setPen|setColor|setForeground|setBackground)\\s*\\("));
        if (const QRegularExpressionMatch named = qtColourName().match(probe); named.hasMatch() && painter.match(probe).hasMatch()) {
            return QStringLiteral("Qt::%1 handed to something that paints with it").arg(named.captured(1));
        }
        // ...and one assigned rather than handed to anything, which reaches a
        // painter on some later line the call above never sees
        static const QRegularExpression assigned(QStringLiteral("(?<![=!<>])=\\s*Qt::(%1)\\b").arg(qtColourNames()));
        if (const QRegularExpressionMatch named = assigned.match(probe); named.hasMatch()) {
            return QStringLiteral("Qt::%1 assigned outright").arg(named.captured(1));
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
        // All three channels in one number - QColor(0xff8800) - which the
        // reading of a "#" never sees and the channel rule above does not either
        static const QRegularExpression fromPacked(QStringLiteral("QColor\\s*\\(\\s*(0x[0-9a-fA-F]{6,8})\\s*\\)"));
        if (const QRegularExpressionMatch packed = fromPacked.match(probe); packed.hasMatch()) {
            return QStringLiteral("a QColor built from the written %1").arg(packed.captured(1));
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

    // The two things a designed surface is read for, one traversal apiece: what
    // a colour is mixed from, and what a size is named by
    enum class Looking { Colours, Sizes };

    static void scanSource(const QString& file, const QStringList& lines, QList<Hit>& hits, const Looking looking)
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
            const QList<Literal> literals = wasInsideRawString ? QList<Literal>{{raw, false}} : stringLiteralsIn(raw);
            QString reason = looking == Looking::Sizes ? sizeInStrings(literals) : colourInStrings(literals);
            if (looking == Looking::Colours && reason.isEmpty() && !wasInsideRawString) {
                reason = colourInCode(raw);
            }
            if (reason.isEmpty() || exempted(lines, number)) {
                continue;
            }
            hits.append({file, number, raw.trimmed(), reason});
        }
    }

    static void scanDesignerFile(const QString& file, const QStringList& lines, QList<Hit>& hits, const Looking looking)
    {
        bool insideStyleSheet = false;
        for (int number = 1; number <= lines.size(); ++number) {
            const QString& raw = lines.at(number - 1);
            QString reason;
            // A colour Designer wrote as a palette entry, which is three
            // channel elements under a <color> of its own
            if (looking == Looking::Colours && raw.contains(QStringLiteral("<color"))) {
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
                    reason = looking == Looking::Sizes ? sizeInStrings({{raw, false}}) : colourInStrings({{raw, false}});
                }
            }
            if (reason.isEmpty() || exempted(lines, number)) {
                continue;
            }
            hits.append({file, number, raw.trimmed(), reason});
        }
    }

    // Walking every scanned file once, gathering what the caller is looking
    // for. Returns false and says which file could not be read, since a file
    // renamed off the list is a surface that stopped being checked.
    static bool gather(const Looking looking, QList<Hit>& hits, int& scannedLines, QString& failure)
    {
        for (const QString& file : scannedFiles()) {
            QFile source(QStringLiteral("%1/%2").arg(srcDir(), file));
            if (!source.open(QIODevice::ReadOnly | QIODevice::Text)) {
                failure = QStringLiteral("src/%1 is on the scan list and could not be read - it has been renamed or removed").arg(file);
                return false;
            }
            const QStringList lines = QString::fromUtf8(source.readAll()).split(QChar::LineFeed);
            scannedLines += lines.size();
            if (file.endsWith(QStringLiteral(".ui"))) {
                scanDesignerFile(QStringLiteral("src/%1").arg(file), lines, hits, looking);
            } else {
                scanSource(QStringLiteral("src/%1").arg(file), lines, hits, looking);
            }
        }
        return true;
    }

    // Printed one to a line as well as gathered into the failure: QtTest cuts a
    // failure message off at a few hundred characters, and a run that named
    // three of forty would take thirteen runs to clear
    static void report(const QList<Hit>& hits)
    {
        for (const Hit& hit : hits) {
            qWarning().noquote() << QStringLiteral("%1:%2: %3   [%4]").arg(hit.file, QString::number(hit.line), hit.text, hit.reason);
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
        QString failure;
        QVERIFY2(gather(Looking::Colours, hits, scannedLines, failure), qPrintable(failure));

        qInfo().noquote() << QStringLiteral("  %1 lines over %2 files").arg(QString::number(scannedLines), QString::number(files.size()));

        report(hits);
        QVERIFY2(hits.isEmpty(),
                 qPrintable(QStringLiteral("%1 colour(s) written out rather than taken from uiDesign::themeTokens() - listed above. Mix each from the tokens, or - if it is a value being shown "
                                           "rather than chrome - write \"// theme-fixed: <why>\" on the line.")
                                    .arg(QString::number(hits.size()))));
    }

    // ...and the same surfaces read for the other half of the same rule: a
    // stylesheet names a size only through uiDesign::typeSize()
    void test_everyFontSizeOnADesignedSurfaceComesFromTheScale()
    {
        QList<Hit> hits;
        int scannedLines = 0;
        QString failure;
        QVERIFY2(gather(Looking::Sizes, hits, scannedLines, failure), qPrintable(failure));

        report(hits);
        QVERIFY2(hits.isEmpty(),
                 qPrintable(QStringLiteral("%1 font-size(s) that are not a step of uiDesign::typeSize() - listed above. Qt's stylesheet parser reads pt and px and nothing else, so a percentage "
                                           "sets no size at all, and a written number is a size picked by hand rather than a step of the scale. Interpolate typeSize() into the rule as a pt "
                                           "value, or - if it is a picture of something outside this application - write \"// theme-fixed: <why>\" on the line.")
                                    .arg(QString::number(hits.size()))));
    }
};

QTEST_GUILESS_MAIN(DesignColourLiteralTest)

#include "DesignColourLiteralTest.moc"
