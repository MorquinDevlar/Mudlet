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
 * The shapes the editor's own chrome is cut to, read off a shot of the window
 * rather than off the rules that draw them.
 *
 * Three of them, all of which were a rectangle of the wrong tone before:
 *
 * - The gap between two panes. A splitter handle has to be nine pixels wide to
 *   be aimed at with a mouse, and it used to be nine pixels of the separator
 *   tone - a groove across the window wherever two panes met. It is now a one
 *   pixel seam with each pane's own tone carried up to it, so the width the
 *   mouse needs is not a width the reader sees; hovered, the seam widens to
 *   three and takes the accent, which is what says it can be dragged.
 *
 * - The heading over the code pane, which is the handle above it. The pane
 *   under it is a panel, so the heading takes a panel's corner at the top and
 *   the page shows through outside the arc.
 *
 * - The chip on that heading. A widget a row lays out with no alignment of its
 *   own is given the whole height of the row, so the chip was the strip rather
 *   than a chip on it. It is now given its own height, with room above and
 *   below it.
 *
 * ...and one that is not chrome but is cut the same way: a button showing a
 * colour, which the design draws as a well - the input corner and a hairline
 * round it - rather than as a hard edged rectangle with a grey line round it.
 *
 * Run with: ctest -R EditorChromeShapeTest -V
 */

#include <QImage>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitterHandle>
#include <QToolBar>
#include <QToolButton>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <chrono>

#include "GripSplitter.h"
#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgTriggerEditor.h"
#include "dlgTriggersMainArea.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorChromeShapeTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorChromeShape-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    // The least the strip has to leave above and below the chip for the two to
    // read as different things
    static constexpr int scmChipAir = 4;
    // What the actions toolbar holds its contents off its own leading edge by,
    // which the handle sits inside - see the rules in applyEditorShellStyle()
    static constexpr int scmToolBarPadding = 6;

    void deleteProfileDirectory(const QString& profileName)
    {
        QDir dir(mudlet::getMudletPath(enums::profileHomePath, profileName));
        if (dir.exists()) {
            dir.removeRecursively();
        }
    }

    void startProfile(const QString& profileName, const QString& address, const QString& port)
    {
        mpHost = TestProfile::create(profileName, address, port);
        if (!mpHost) {
            QFAIL("No active host available for the test.");
        }

        QSignalSpy spy(&(mpHost->mTelnet), &cTelnet::signal_connected);
        if (!spy.wait(2000)) {
            QFAIL("Could not connect with the host.");
        }
    }

    QImage windowShot() const { return mpEditor->grab().toImage(); }

    // The seam between the panel of items and the column an item is edited in,
    // which is the one plain handle of the horizontal splitter
    QSplitterHandle* paneSeam() const { return mpEditor->splitter_main->handle(1); }

    // ...and the handle carrying the code pane's heading, which is the second of
    // the three the right hand splitter stacks
    QSplitterHandle* codeHeading() const { return mpEditor->splitter_right->handle(1); }

    // A row of pixels across a handle, in the handle's own coordinates
    QList<QColor> acrossTheHandle(const QImage& shot, const QWidget* pHandle, const int y) const
    {
        QList<QColor> row;
        for (int x = 0; x < pHandle->width(); ++x) {
            row << shot.pixelColor(pHandle->mapTo(mpEditor, QPoint(x, y)));
        }
        return row;
    }

    static QString describe(const QList<QColor>& row)
    {
        QStringList names;
        for (const QColor& colour : row) {
            names << colour.name();
        }
        return names.join(QLatin1Char(' '));
    }

    // How many pixels in a row are the given colour, and where the run of them
    // starts - a seam is one run, so the pair says both how wide it is and
    // whether it is in one piece
    static QPair<int, int> runOf(const QList<QColor>& row, const QColor& wanted)
    {
        int start = -1;
        int length = 0;
        for (int x = 0; x < row.size(); ++x) {
            if (row.at(x).rgb() != wanted.rgb()) {
                continue;
            }
            if (start < 0) {
                start = x;
            }
            ++length;
        }
        return {start, length};
    }

    void hover(QWidget* pWidget, const bool hovered) const
    {
        const QPointF local(pWidget->rect().center());
        if (hovered) {
            QEnterEvent enter(local, local, pWidget->mapToGlobal(pWidget->rect().center()));
            QCoreApplication::sendEvent(pWidget, &enter);
        } else {
            QEvent leave(QEvent::Leave);
            QCoreApplication::sendEvent(pWidget, &leave);
        }
        QTest::qWait(20ms);
    }

private slots:
    void initTestCase()
    {
        if (portableMarkerPresent()) {
            QSKIP("portable.txt present - it takes precedence over XDG_CONFIG_HOME, so the config dir cannot be redirected");
        }

        QVERIFY(mConfigDir.isValid());
        QVERIFY(QDir().mkpath(qsl("%1/mudlet/profiles").arg(mConfigDir.path())));
        mSavedXdg = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", mConfigDir.path().toUtf8());

        mpServer = new TelnetServerStub(qApp);
        mpServer->start(mLocalhost, 0);
        QVERIFY2(mpServer->isListening(), qPrintable(qsl("TelnetServerStub failed to start: %1").arg(mpServer->errorString())));
        mPort = QString::number(mpServer->serverPort());
        mudlet::start();
        mudlet::self()->setupConfig();
        QCOMPARE(mudlet::getMudletPath(enums::mainPath), qsl("%1/mudlet").arg(mConfigDir.path()));
        mudlet::self()->takeOwnershipOfInstanceCoordinator(std::make_unique<MudletInstanceCoordinator>(qsl("MudletInstanceCoordinator")));
        mudlet::self()->init();
        mudlet::self()->setStorePasswordsSecurely(false);
        deleteProfileDirectory(mProfileName);

        startProfile(mProfileName, mLocalhost, mPort);
        if (QTest::currentTestFailed()) {
            return;
        }

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mpEditor->resize(1100, 800);
        mpEditor->slot_showTriggers();
        // A trigger to look at, so that the form above the code pane holds
        // something and the heading over it is laid out
        mpEditor->addTrigger(false);
        QTest::qWait(100ms);

        QVERIFY2(paneSeam() != nullptr, "The main splitter has no handle between its two panes");
        QVERIFY2(codeHeading() != nullptr, "The right hand splitter has no handle over the code pane");
        QVERIFY2(mpEditor->mpWidget_editorCompileChip != nullptr, "The code pane's heading carries no chip");
    }

    void cleanupTestCase()
    {
        mpEditor = nullptr;
        mpHost = nullptr;
        delete mpServer;
        mpServer = nullptr;
        if (mudlet::self()) {
            deleteProfileDirectory(mProfileName);
            delete mudlet::self();
        }
        mSavedXdg.isNull() ? qunsetenv("XDG_CONFIG_HOME") : qputenv("XDG_CONFIG_HOME", mSavedXdg);
    }

    // The gap between the panel of items and the column beside it: one pixel of
    // the seam tone, with the panel's own tone up to it on one side and the
    // page's on the other. Nine pixels of the seam tone is what it used to be.
    void test_aPlainHandleIsOneHairlineBetweenTwoTones()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        QSplitterHandle* pHandle = paneSeam();
        const QList<QColor> row = acrossTheHandle(windowShot(), pHandle, pHandle->height() / 2);
        const QPair<int, int> seam = runOf(row, tokens.separator);

        qInfo().noquote() << qsl("  across the %1px handle: %2; the seam is %3, the panel %4 and the page %5")
                                     .arg(QString::number(pHandle->width()), describe(row), tokens.separator.name(), tokens.pane.name(), tokens.page.name());

        QCOMPARE(pHandle->width(), uiDesign::GripSplitter::scmHandleThickness);
        QVERIFY2(seam.second == 1, qPrintable(qsl("the seam is %1px of %2 rather than one: %3").arg(QString::number(seam.second), tokens.separator.name(), describe(row))));

        QStringList wrong;
        for (int x = 0; x < row.size(); ++x) {
            if (x == seam.first) {
                continue;
            }
            const QColor wanted = x < seam.first ? tokens.pane : tokens.page;
            if (row.at(x).rgb() != wanted.rgb()) {
                wrong << qsl("x=%1 is %2 where the pane beside it is %3").arg(QString::number(x), row.at(x).name(), wanted.name());
            }
        }
        QVERIFY2(wrong.isEmpty(), qPrintable(qsl("the handle is not the two panes' own tones either side of the seam: %1").arg(wrong.join(qsl(", ")))));
    }

    // ...and under the pointer the seam widens to three and takes the accent,
    // which is the whole of what says the panes can be dragged apart
    void test_hoveringAPlainHandleWidensTheSeamToTheAccent()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        QSplitterHandle* pHandle = paneSeam();
        hover(pHandle, true);
        const QImage shot = windowShot();
        // Two readings down the handle rather than one: the grip pill this
        // replaces was three pixels wide as well, and drawn across the middle
        // of the handle - so a single reading taken there says nothing about
        // which of the two is on screen. The whole length of the seam is lit.
        const QList<QPair<QString, QList<QColor>>> rows{{qsl("near its top"), acrossTheHandle(shot, pHandle, 4)},
                                                        {qsl("across its middle"), acrossTheHandle(shot, pHandle, pHandle->height() / 2)},
                                                        {qsl("near its foot"), acrossTheHandle(shot, pHandle, pHandle->height() - 5)}};
        hover(pHandle, false);

        const int middle = (pHandle->width() - 1) / 2;
        QStringList measured;
        QStringList wrong;
        for (const auto& row : rows) {
            const QPair<int, int> lit = runOf(row.second, tokens.accent);
            measured << qsl("%1 %2").arg(row.first, describe(row.second));
            if (lit.second != 3) {
                wrong << qsl("%1 the seam is %2px of the accent rather than three").arg(row.first, QString::number(lit.second));
            } else if (lit.first != middle - 1) {
                // ...and still centred on the line the resting seam is drawn on
                wrong << qsl("%1 the seam starts at x=%2 rather than one either side of the middle at x=%3").arg(row.first, QString::number(lit.first), QString::number(middle));
            }
        }
        qInfo().noquote() << qsl("  hovered: %1; the accent is %2").arg(measured.join(qsl("; ")), tokens.accent.name());
        QVERIFY2(wrong.isEmpty(), qPrintable(qsl("the hovered handle is not one lit seam from end to end: %1").arg(wrong.join(qsl(", ")))));

        // ...and it goes away again, or every later reading is of a lit seam
        const QList<QColor> atRest = acrossTheHandle(windowShot(), pHandle, pHandle->height() / 2);
        QVERIFY2(runOf(atRest, tokens.accent).second == 0, qPrintable(qsl("the seam stayed lit after the pointer left it: %1").arg(describe(atRest))));
    }

    // The heading over the code pane is the top of that pane, so it is cut to a
    // panel's corner: the page outside the arc, the strip inside it. Its bottom
    // stays square - what is under the pane is the next pane down.
    void test_theCodePanesHeadingIsCutToThePanelsCorner()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        QSplitterHandle* pHandle = codeHeading();
        const QImage shot = windowShot();
        const QList<QColor> topRow = acrossTheHandle(shot, pHandle, 0);
        const QList<QColor> bottomRow = acrossTheHandle(shot, pHandle, pHandle->height() - 1);

        const QColor topCorner = topRow.constFirst();
        const QColor topMiddle = topRow.at(topRow.size() / 2);
        const QColor bottomCorner = bottomRow.constFirst();
        qInfo().noquote() << qsl("  the heading is %1px tall; its top corner is %2, the middle of its top edge %3 and its bottom corner %4; the strip is %5 and the page %6")
                                     .arg(QString::number(pHandle->height()), topCorner.name(), topMiddle.name(), bottomCorner.name(), tokens.separator.name(), tokens.page.name());

        QVERIFY2(topMiddle.rgb() == tokens.separator.rgb(), qPrintable(qsl("the heading strip does not reach the top of the handle: the middle of that edge is %1").arg(topMiddle.name())));
        QVERIFY2(topCorner.rgb() == tokens.page.rgb(), qPrintable(qsl("the heading's top corner is not cut away to the page: it is %1").arg(topCorner.name())));
        QVERIFY2(bottomCorner.rgb() == tokens.separator.rgb(), qPrintable(qsl("the heading's bottom corner is cut away as well, at %1 - only its top meets the page").arg(bottomCorner.name())));
    }

    // ...and the chip on it is a chip on a bar rather than the bar itself:
    // measured off the window, there is room above and below it
    void test_theCompileChipHasRoomAboveAndBelowIt()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        QSplitterHandle* pHandle = codeHeading();
        QWidget* pChip = mpEditor->mpWidget_editorCompileChip;
        const QImage shot = windowShot();

        // Down the middle of the chip, out to wherever the strip's own tone
        // starts: what is painted there is the chip, whatever it is filled with
        const int x = pChip->mapTo(pHandle, pChip->rect().center()).x();
        const auto isStrip = [&](const int y) {
            return shot.pixelColor(pHandle->mapTo(mpEditor, QPoint(x, y))).rgb() == tokens.separator.rgb();
        };
        const int middle = pHandle->height() / 2;
        QVERIFY2(!isStrip(middle), "The middle of the heading where the chip should be is the strip's own tone, so there is no chip to measure");
        int top = middle;
        int bottom = middle;
        while (top > 0 && !isStrip(top - 1)) {
            --top;
        }
        while (bottom < pHandle->height() - 1 && !isStrip(bottom + 1)) {
            ++bottom;
        }

        const int painted = bottom - top + 1;
        const int above = top;
        const int below = pHandle->height() - 1 - bottom;
        qInfo().noquote() << qsl("  the strip is %1px tall and the chip %2px, leaving %3px above it and %4px below")
                                     .arg(QString::number(pHandle->height()), QString::number(painted), QString::number(above), QString::number(below));

        QVERIFY2(above >= scmChipAir && below >= scmChipAir,
                 qPrintable(qsl("the chip fills the heading: %1px of strip above it and %2px below, on a strip of %3px")
                                    .arg(QString::number(above), QString::number(below), QString::number(pHandle->height()))));
    }

    // The bar the actions are on can be dragged to another edge of the window,
    // and the grip at its leading end is what says so. Styling the bar takes
    // the platform's own handle with it, which left a pair of dots a shade off
    // the bar they are drawn on; these are the six the pattern rows are
    // dragged by, inked in the tone the rest of the bar's chrome is.
    void test_theToolbarsGripIsTheSixDotsInTheQuietTone()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        QToolBar* pToolBar = mpEditor->toolBar;
        QVERIFY2(pToolBar != nullptr && pToolBar->isMovable(), "The actions toolbar is missing or cannot be moved, so it carries no grip");
        const int extent = pToolBar->style()->pixelMetric(QStyle::PM_ToolBarHandleExtent, nullptr, pToolBar);
        // The handle sits inside whatever the bar holds its contents off its
        // edge by, so the strip read is the extent plus that padding
        const int strip = extent + scmToolBarPadding;

        const QImage shot = windowShot();
        int inked = 0;
        qreal strongest = 0.0;
        QColor strongestColour;
        QRect dots;
        for (int x = 0; x < strip; ++x) {
            for (int y = 0; y < pToolBar->height() - 1; ++y) {
                const QColor measured = shot.pixelColor(pToolBar->mapTo(mpEditor, QPoint(x, y)));
                // How far the pixel is from the bar towards the tone the grip
                // is inked in: a dot two pixels across is drawn with antialiased
                // edges, so even its strongest pixel is short of the full ink
                const qreal towardsTheInk = static_cast<qreal>(tokens.page.green() - measured.green()) / std::max(1, tokens.page.green() - tokens.mutedText.green());
                if (towardsTheInk < 0.1) {
                    continue;
                }
                ++inked;
                dots = dots.isNull() ? QRect(x, y, 1, 1) : dots.united(QRect(x, y, 1, 1));
                if (towardsTheInk > strongest) {
                    strongest = towardsTheInk;
                    strongestColour = measured;
                }
            }
        }
        qInfo().noquote() << qsl("  the handle is %1px wide; the grip is a %2x%3 block of %4 painted pixels at %5,%6, its strongest %7 - %8 of the way from the bar at %9 to the quiet tone at %10")
                                     .arg(QString::number(extent),
                                          QString::number(dots.width()),
                                          QString::number(dots.height()),
                                          QString::number(inked),
                                          QString::number(dots.x()),
                                          QString::number(dots.y()),
                                          strongestColour.isValid() ? strongestColour.name() : qsl("nothing at all"),
                                          QString::number(strongest, 'f', 2),
                                          tokens.page.name(),
                                          tokens.mutedText.name());

        QVERIFY2(extent >= 8, qPrintable(qsl("the handle is %1px wide, which is not room for a grip").arg(QString::number(extent))));
        // Six dots of two pixels across: the count is a floor rather than a
        // measurement of them
        QVERIFY2(inked >= 6 * 4, qPrintable(qsl("the handle holds %1 painted pixels, so the six dots are not being drawn on it").arg(QString::number(inked))));
        QVERIFY2(strongest >= 0.7,
                 qPrintable(qsl("the grip's strongest pixel is %1, only %2 of the way to the quiet tone - it is a wash rather than the ink")
                                    .arg(strongestColour.name(), QString::number(strongest, 'f', 2))));
        // Two columns of three, so it is taller than it is wide
        QVERIFY2(dots.height() > dots.width(), qPrintable(qsl("the grip is %1x%2, which is not two columns of three dots").arg(QString::number(dots.width()), QString::number(dots.height()))));
    }

    // A button showing a colour is a well: the design's input corner, so the
    // extreme corner of it is whatever it lies on rather than more of the fill.
    // The sheet is the editor's own - what it puts on this very button when a
    // trigger colours what it matched.
    void test_aColourWellIsCutToTheInputCorner()
    {
        QPushButton* pWell = mpEditor->mpTriggersMainArea->pushButtonFgColor;
        QVERIFY2(pWell != nullptr, "The trigger form has no foreground colour button");
        // The options the well is one of open closed, so they are asked for -
        // and the column they open in is a scrolling one, so the well is
        // brought into it rather than merely shown
        mpEditor->slot_showAllTriggerControls(true);
        const QColor filled(0, 128, 0);
        pWell->setStyleSheet(dlgTriggerEditor::generateButtonStyleSheet(filled));
        QTest::qWait(50ms);
        auto* pScroll = mpEditor->findChild<QScrollArea*>(qsl("editorTriggerOptionsScroll"));
        QVERIFY2(pScroll != nullptr, "The trigger form has no options column for the well to be scrolled into");
        pScroll->ensureWidgetVisible(pWell);
        QTest::qWait(50ms);
        QVERIFY2(pWell->isVisible() && pWell->width() > 8 && pWell->height() > 8, "The colour well is not on show, so there is nothing to measure");
        const QRect onShow(pWell->mapTo(mpEditor, QPoint(0, 0)), pWell->size());
        const QRect column(pScroll->viewport()->mapTo(mpEditor, QPoint(0, 0)), pScroll->viewport()->size());
        QVERIFY2(column.contains(onShow),
                 qPrintable(qsl("the well at %1 is not inside the column at %2, so what is read below is whatever is painted over it")
                                    .arg(qsl("%1,%2 %3x%4").arg(QString::number(onShow.x()), QString::number(onShow.y()), QString::number(onShow.width()), QString::number(onShow.height())),
                                         qsl("%1,%2 %3x%4").arg(QString::number(column.x()), QString::number(column.y()), QString::number(column.width()), QString::number(column.height())))));

        const QImage shot = windowShot();
        const auto at = [&](const QPoint& point) {
            return shot.pixelColor(pWell->mapTo(mpEditor, point));
        };
        const QColor fill = at(pWell->rect().center());
        const QColor corner = at(pWell->rect().topLeft());
        // What the well lies on, read just outside its own edge
        const QColor behind = at(QPoint(-2, pWell->height() / 2));
        qInfo().noquote() << qsl("  the well is filled %1, its corner is %2 and what it lies on is %3").arg(fill.name(), corner.name(), behind.name());

        QVERIFY2(fill.rgb() == filled.rgb(), qPrintable(qsl("the well is not filled with the colour it stands for: it is %1 where the colour is %2").arg(fill.name(), filled.name())));
        QVERIFY2(corner.rgb() != fill.rgb(), qPrintable(qsl("the well's corner is as solid as the rest of it, at %1 - it is a rectangle rather than a well").arg(corner.name())));
        QVERIFY2(corner.rgb() == behind.rgb(),
                 qPrintable(qsl("the well's corner is not cut away to what it lies on: the corner is %1 and the surface beside it %2").arg(corner.name(), behind.name())));
    }

    // The button that empties the sound file field carried a green bitmap of
    // its own. It is now the same tinted set every other glyph in the window is
    // drawn from, which means it has a switched-off tone as well - and that is
    // the one it is drawn in most of the time, since a trigger with no sound
    // set has nothing for it to take away.
    void test_theSoundFileClearButtonIsATintedGlyph()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        QToolButton* pClear = mpEditor->mpTriggersMainArea->toolButton_clearSoundFile;
        QVERIFY2(pClear != nullptr, "The trigger form has no button to empty the sound file field");
        auto* pScroll = mpEditor->findChild<QScrollArea*>(qsl("editorTriggerOptionsScroll"));
        QVERIFY2(pScroll != nullptr, "The trigger form has no options column");
        pScroll->ensureWidgetVisible(pClear);
        QTest::qWait(50ms);
        QVERIFY2(!pClear->icon().isNull(), "The button carries no picture at all");

        // How far the strongest pixel of the glyph is from the card it is drawn
        // on towards the words on that card: a stroke a pixel and a half wide is
        // antialiased, so its own tone is read as a fraction rather than matched
        const auto strongestInk = [&]() {
            const QImage shot = windowShot();
            qreal strongest = 0.0;
            for (int x = 0; x < pClear->width(); ++x) {
                for (int y = 0; y < pClear->height(); ++y) {
                    const QColor measured = shot.pixelColor(pClear->mapTo(mpEditor, QPoint(x, y)));
                    strongest = std::max(strongest, static_cast<qreal>(tokens.card.green() - measured.green()) / std::max(1, tokens.card.green() - tokens.text.green()));
                }
            }
            return strongest;
        };

        pClear->setEnabled(true);
        QTest::qWait(20ms);
        const qreal lively = strongestInk();
        pClear->setEnabled(false);
        QTest::qWait(20ms);
        const qreal quiet = strongestInk();
        qInfo().noquote() << qsl("  the glyph is drawn at %1px; its strongest pixel is %2 of the way from the card to the words switched on and %3 switched off")
                                     .arg(QString::number(pClear->iconSize().width()), QString::number(lively, 'f', 2), QString::number(quiet, 'f', 2));

        QCOMPARE(pClear->iconSize(), QSize(13, 13));
        QVERIFY2(lively > 0.5, qPrintable(qsl("the button's picture reaches only %1 of the way to the words, so it is not being drawn at all").arg(QString::number(lively, 'f', 2))));
        QVERIFY2(quiet < lively,
                 qPrintable(qsl("the button is drawn the same switched off as switched on - %1 against %2 - so it has no quiet tone")
                                    .arg(QString::number(quiet, 'f', 2), QString::number(lively, 'f', 2))));
    }
};

#include "EditorChromeShapeTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorChromeShapeTest)
