/***************************************************************************
 *   Copyright (C) 2024 by Zooka                                           *
 *   Copyright (C) 2026 by Stephen Lyons - slysven@virginmedia.com         *
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

#include "Host.h"
#include "TrailingWhitespaceMarker.h"
#include "TriggerHighlighter.h"
#include "uiDesign.h"
#include "edbee/views/texttheme.h"
#include "edbee/models/textdocumentscopes.h"

namespace {
// What a token has to clear against the field it lies on before it counts as
// readable. Below the 4.5 body text is held to, because a pattern is read in a
// monospaced face at the form's own size and the colour is a hint rather than
// the message - but far enough above 1 that a theme's mid-grey comment colour
// cannot survive on a mid-grey field.
constexpr qreal scmMinimumTokenContrast = 3.5;
} // namespace

TriggerHighlighter::TriggerHighlighter(QTextDocument* parent)
: QSyntaxHighlighter(parent)
{
    setTheme("Mudlet"); // start with the default theme
}

void TriggerHighlighter::setHighlightingEnabled(bool enabled)
{
    highlightingEnabled = enabled;
    rehighlight();
}

void TriggerHighlighter::highlightBlock(const QString& text)
{
    if (!highlightingEnabled) {
        return;
    }

    for (const HighlightingRule& rule : std::as_const(highlightingRules)) {
        QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            QRegularExpressionMatch match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}

void TriggerHighlighter::setTheme(const QString& themeName)
{
    mThemeName = themeName;
    rebuildRules();
}

void TriggerHighlighter::setFieldColors(const QColor& background, const QColor& text)
{
    if (mFieldBackground == background && mFieldText == text) {
        return;
    }
    mFieldBackground = background;
    mFieldText = text;
    rebuildRules();
}

void TriggerHighlighter::rebuildRules()
{
    highlightingRules.clear();

    auto edbee = edbee::Edbee::instance();
    auto themeManager = edbee->themeManager();
    edbee::TextTheme* theme = themeManager->theme(mThemeName);

    // set defaults from chosen theme. The theme's own background is not among
    // them: a pattern row is a field on the form rather than a slice of the code
    // pane, so what is behind the words is the field's colour and nothing a
    // token brings with it.
    edbee::TextThemeRule defaultRule("default", "selector", theme->foregroundColor(), QColor(), false, false, false);
    applyFormatting(anchorFormat, &defaultRule);
    applyFormatting(charClassFormat, &defaultRule);
    applyFormatting(escapeCharFormat, &defaultRule);
    applyFormatting(groupFormat, &defaultRule);
    applyFormatting(quantifierFormat, &defaultRule);

    QList<edbee::TextThemeRule*> rules = theme->rules();

    // override defaults in theme using scopes which map to regex formats
    // check a few as some themes don't provide all of them
    std::map<QString, QTextCharFormat&> scopeMap = {{"comment", anchorFormat},
                                                    {"keyword", charClassFormat},
                                                    {"keyword.control", charClassFormat},
                                                    {"keyword.operator", charClassFormat},
                                                    {"constant", escapeCharFormat},
                                                    {"constant.numeric", escapeCharFormat},
                                                    {"constant.language", escapeCharFormat},
                                                    {"constant.character.escape", escapeCharFormat},
                                                    {"string", groupFormat},
                                                    {"variable", quantifierFormat},
                                                    {"variable.language", quantifierFormat},
                                                    {"variable.parameter", quantifierFormat},
                                                    {"variable.function", quantifierFormat}};

    for (auto& rule : rules) {
        QString scope = rule->scopeSelector()->toString();
        auto it = scopeMap.find(scope);
        if (it != scopeMap.end()) {
            applyFormatting(it->second, rule);
        }
    }

    const QRegularExpression anchorPattern(R"([$^])", QRegularExpression::UseUnicodePropertiesOption);
    const QRegularExpression charClassPattern(R"(\[.*?\])", QRegularExpression::UseUnicodePropertiesOption);
    const QRegularExpression escapePattern(R"(\\[.*?])", QRegularExpression::UseUnicodePropertiesOption);
    const QRegularExpression groupPattern(R"(\((?:\?[:=!])?.*?\))", QRegularExpression::UseUnicodePropertiesOption);
    const QRegularExpression quantifierPattern(R"([?+*]|\{\d+,?\d*\})", QRegularExpression::UseUnicodePropertiesOption);

    highlightingRules.append({anchorPattern, anchorFormat});
    highlightingRules.append({charClassPattern, charClassFormat});
    highlightingRules.append({escapePattern, escapeCharFormat});
    highlightingRules.append({quantifierPattern, quantifierFormat});
    highlightingRules.append({groupPattern, groupFormat});

    rehighlight();
}

void TriggerHighlighter::applyFormatting(QTextCharFormat& format, edbee::TextThemeRule* rule)
{
    // What the theme calls this token, moved onto the field it will be read on.
    // The hue is what says which part of a pattern this is, so that is the half
    // kept; where it cannot be made to read at all, the field's own text colour
    // stands in - an unreadable pattern is worse than an unhighlighted one.
    const QColor foreground = mFieldBackground.isValid() ? uiDesign::readableOn(mFieldBackground, rule->foregroundColor(), mFieldText, scmMinimumTokenContrast) : rule->foregroundColor();

    if (foreground.isValid()) {
        format.setForeground(foreground);
    }

    // A token never paints its own background, whatever the theme says: the
    // dark block a code theme puts behind its words is what drew a pattern row
    // as a black box on a light form
    format.clearBackground();
    if (rule->bold()) {
        format.setFontWeight(QFont::Bold);
    } else {
        format.setFontWeight(QFont::Normal);
    }
    format.setFontItalic(rule->italic());
    format.setFontUnderline(rule->underline());
}
