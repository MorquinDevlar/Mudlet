/***************************************************************************
 *   Copyright (C) 2026 by Mudlet Developers - mudlet@mudlet.org           *
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
 * A control that sits inside a sentence is laid out from the translation, not
 * from the English word order.
 *
 * The editor's "Keep firing for [3] more lines" and "within [5] lines" rows used
 * to be built as label, control, label in that fixed order, from three separate
 * strings. A translator handed "Keep firing for" and "more lines" cannot move
 * the number: languages that read the count first, or last, had no way to say
 * so. uiDesign::buildControlSentenceRow() takes the whole sentence with a %1
 * where the control goes, so the placeholder's position in the translation is
 * what decides the row's order.
 *
 * The cases below are what a translator can actually produce: the placeholder in
 * the middle, at the front, at the end, and - because a dropped %1 is a
 * translation mistake and not a crash - missing entirely.
 *
 * Run with: ctest -R ControlSentenceRowTest -V
 */

#include <QBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QtTest/QtTest>

#include "uiDesign.h"

#include "GroupedTest.h"

class ControlSentenceRowTest : public QObject
{
    Q_OBJECT

private:
    // What the row reads as, left to right: each label's text in place, and the
    // control itself as a marker, so a case says the order it expects in one line
    static QStringList readingOf(const QLayout* pLayout, const QWidget* pControl)
    {
        QStringList reading;
        for (int i = 0, total = pLayout->count(); i < total; ++i) {
            QWidget* pWidget = pLayout->itemAt(i)->widget();
            if (!pWidget) {
                continue;
            }
            if (pWidget == pControl) {
                reading << qsl("[control]");
                continue;
            }
            auto* pLabel = qobject_cast<QLabel*>(pWidget);
            reading << (pLabel ? pLabel->text() : qsl("[%1]").arg(QString::fromLatin1(pWidget->metaObject()->className())));
        }
        return reading;
    }

    // ...and the same for a sentence holding several of them, where which
    // control landed where is the whole of what a case here is about
    static QStringList readingOf(const QLayout* pLayout, const QList<QWidget*>& controls)
    {
        QStringList reading;
        for (int i = 0, total = pLayout->count(); i < total; ++i) {
            QWidget* pWidget = pLayout->itemAt(i)->widget();
            if (!pWidget) {
                continue;
            }
            const qsizetype control = controls.indexOf(pWidget);
            if (control >= 0) {
                reading << qsl("[control %1]").arg(QString::number(control + 1));
                continue;
            }
            auto* pLabel = qobject_cast<QLabel*>(pWidget);
            reading << (pLabel ? pLabel->text() : qsl("[%1]").arg(QString::fromLatin1(pWidget->metaObject()->className())));
        }
        return reading;
    }

    static QString drift(const QString& sentence, const QStringList& reading) { return qsl("\"%1\" laid out as: %2").arg(sentence, reading.join(qsl(" | "))); }

private slots:
    // The English order, which is also the one the two editor rows ship with
    void test_aSentenceWithThePlaceholderInTheMiddleReadsLabelControlLabel()
    {
        QWidget host;
        auto* pRow = new QHBoxLayout(&host);
        auto* pSpinBox = new QSpinBox(&host);

        const QString sentence = qsl("Keep firing for %1 more lines");
        uiDesign::buildControlSentenceRow(pRow, sentence, pSpinBox);

        const QStringList reading = readingOf(pRow, pSpinBox);
        QVERIFY2(reading == QStringList({qsl("Keep firing for"), qsl("[control]"), qsl("more lines")}), qPrintable(drift(sentence, reading)));
    }

    // A language that counts first: nothing precedes the control, so no empty
    // label is put in front of it either
    void test_aTranslationCanPutTheControlFirst()
    {
        QWidget host;
        auto* pRow = new QHBoxLayout(&host);
        auto* pSpinBox = new QSpinBox(&host);

        const QString sentence = qsl("%1 extra lines are kept firing for");
        uiDesign::buildControlSentenceRow(pRow, sentence, pSpinBox);

        const QStringList reading = readingOf(pRow, pSpinBox);
        QVERIFY2(reading == QStringList({qsl("[control]"), qsl("extra lines are kept firing for")}), qPrintable(drift(sentence, reading)));
    }

    // ...and one that counts last
    void test_aTranslationCanPutTheControlLast()
    {
        QWidget host;
        auto* pRow = new QHBoxLayout(&host);
        auto* pSpinBox = new QSpinBox(&host);

        const QString sentence = qsl("Extra lines to keep firing for: %1");
        uiDesign::buildControlSentenceRow(pRow, sentence, pSpinBox);

        const QStringList reading = readingOf(pRow, pSpinBox);
        QVERIFY2(reading == QStringList({qsl("Extra lines to keep firing for:"), qsl("[control]")}), qPrintable(drift(sentence, reading)));
    }

    // A translation that lost its placeholder is a mistake in the .ts file, and
    // the row it produces still has to read as a sentence with a control after it
    void test_aTranslationWithNoPlaceholderKeepsAllOfItsWords()
    {
        QWidget host;
        auto* pRow = new QHBoxLayout(&host);
        auto* pSpinBox = new QSpinBox(&host);

        const QString sentence = qsl("Keep firing for more lines");
        uiDesign::buildControlSentenceRow(pRow, sentence, pSpinBox);

        const QStringList reading = readingOf(pRow, pSpinBox);
        QVERIFY2(reading == QStringList({qsl("Keep firing for more lines"), qsl("[control]")}), qPrintable(drift(sentence, reading)));
    }

    // The timer form's interval is four fields in one sentence, and %1 to %4
    // are what says which of them goes where
    void test_aSentenceCanHoldSeveralControls()
    {
        QWidget host;
        auto* pRow = new QHBoxLayout(&host);
        QList<QWidget*> controls{new QSpinBox(&host), new QSpinBox(&host), new QSpinBox(&host)};

        const QString sentence = qsl("Fires every %1 h %2 min %3 s");
        uiDesign::buildControlSentenceRow(pRow, sentence, controls);

        const QStringList reading = readingOf(pRow, controls);
        QVERIFY2(reading == QStringList({qsl("Fires every"), qsl("[control 1]"), qsl("h"), qsl("[control 2]"), qsl("min"), qsl("[control 3]"), qsl("s")}), qPrintable(drift(sentence, reading)));
    }

    // A language that counts the other way round moves the placeholders rather
    // than the fields, and the row follows the translation
    void test_aTranslationCanReorderTheControls()
    {
        QWidget host;
        auto* pRow = new QHBoxLayout(&host);
        QList<QWidget*> controls{new QSpinBox(&host), new QSpinBox(&host), new QSpinBox(&host)};

        const QString sentence = qsl("%3 s, %2 min and %1 h between firings");
        uiDesign::buildControlSentenceRow(pRow, sentence, controls);

        const QStringList reading = readingOf(pRow, controls);
        QVERIFY2(reading == QStringList({qsl("[control 3]"), qsl("s,"), qsl("[control 2]"), qsl("min and"), qsl("[control 1]"), qsl("h between firings")}), qPrintable(drift(sentence, reading)));
    }

    // A translation that dropped one of its placeholders has still to hold
    // every field it was given, or the interval loses a part of itself
    void test_aTranslationMissingOnePlaceholderKeepsEveryControl()
    {
        QWidget host;
        auto* pRow = new QHBoxLayout(&host);
        QList<QWidget*> controls{new QSpinBox(&host), new QSpinBox(&host), new QSpinBox(&host)};

        const QString sentence = qsl("Fires every %1 h %3 s");
        uiDesign::buildControlSentenceRow(pRow, sentence, controls);

        const QStringList reading = readingOf(pRow, controls);
        QVERIFY2(reading == QStringList({qsl("Fires every"), qsl("[control 1]"), qsl("h"), qsl("[control 3]"), qsl("s"), qsl("[control 2]")}), qPrintable(drift(sentence, reading)));
    }

    // A screen reader names the field by the sentence it sits in, since the
    // labels either side of it are not attached to it as buddies
    void test_theControlIsNamedByTheWholeSentence()
    {
        QWidget host;
        auto* pRow = new QHBoxLayout(&host);
        auto* pSpinBox = new QSpinBox(&host);

        uiDesign::buildControlSentenceRow(pRow, qsl("Keep firing for %1 more lines"), pSpinBox);

        QCOMPARE(pSpinBox->accessibleName(), qsl("Keep firing for more lines"));
    }

    // The Firing card builds its row before there is a widget to build it on, so
    // the labels have to arrive in the card once the row is added to it
    void test_aRowBuiltBeforeItHasAWidgetStillEndsUpInOne()
    {
        QWidget host;
        auto* pColumn = new QVBoxLayout(&host);
        auto* pSpinBox = new QSpinBox(&host);

        auto* pRow = new QHBoxLayout();
        uiDesign::buildControlSentenceRow(pRow, qsl("Keep firing for %1 more lines"), pSpinBox);
        pColumn->addLayout(pRow);

        const QStringList reading = readingOf(pRow, pSpinBox);
        QVERIFY2(reading == QStringList({qsl("Keep firing for"), qsl("[control]"), qsl("more lines")}), qPrintable(drift(qsl("Keep firing for %1 more lines"), reading)));

        for (int i = 0, total = pRow->count(); i < total; ++i) {
            QWidget* pWidget = pRow->itemAt(i)->widget();
            QVERIFY2(pWidget && pWidget->parentWidget() == &host, "a widget in the row was left outside the widget the row ended up on");
        }
    }
};

#include "ControlSentenceRowTest.moc"
MUDLET_GROUPED_TEST_MAIN(ControlSentenceRowTest)
