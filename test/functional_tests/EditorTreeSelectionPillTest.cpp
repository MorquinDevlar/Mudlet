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
 * What a chosen row in the panel of items is painted, measured across the whole
 * width of the row off a shot of the window.
 *
 * A selected row used to be two things stuck together. The column its chevron
 * stands in was painted by the style, in the platform's own saturated selection
 * colour, because a stylesheet rule naming QTreeWidget::item does not reach the
 * branch sub-control - and the row itself was filled with a wash that faded
 * along it, which reads as a second tone again at the width this panel is
 * dragged to. So the row was a bright block against a muted band, with the
 * block's square edge running down the middle of it.
 *
 * It is now one pill in one tone, cornered like the sidebar's items and the
 * search field over it. Both halves of that are checked here: that the arrow
 * column and the far end of the row are painted the same colour, on a row deep
 * enough that the view would have drawn several branch cells for it, and that
 * the pill's four extreme corners are cut away to the panel behind rather than
 * being more of the selection.
 *
 * The corners are read on a tree that holds the keyboard, because that is the
 * one state the mark that used to square them off was drawn in.
 * QTreeView::drawRow marks the row the keyboard is on with a focus rectangle
 * across the whole row while the tree shows focus on all its columns - and it
 * draws that one through the style with no widget to look a rule up on, so the
 * "outline: none" the trees carry cannot reach it and the platform draws its
 * own square hairline round the row. Over the pill that reads as a chosen row
 * with square corners and an outline the hovered row beside it does not have,
 * which is the pair the last case here compares.
 *
 * Run with: ctest -R EditorTreeSelectionPillTest -V
 */

#include <QImage>
#include <QListWidget>
#include <QMouseEvent>
#include <QPixmap>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest/QtTest>
#include <chrono>
#include <cstdlib>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TTreeWidget.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorTreeSelectionPillTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorTreeSelectionPill-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    // A package folder holding a group holding a trigger, which is the shape a
    // profile with anything installed in it has. The group in the middle is the
    // row the cases below choose: it is two levels down, so the view would have
    // laid two branch cells out ahead of it, and it holds something, so it has a
    // chevron to draw.
    QTreeWidgetItem* mpOuterGroup = nullptr;
    QTreeWidgetItem* mpInnerGroup = nullptr;

    // How far in from either end of the row the readings are taken. The pill's
    // corner is 8px, so a line one pixel under its top edge is cut into for the
    // first four of them - and a reading is wanted on the fill rather than on
    // the arc.
    static constexpr int scmClearOfTheCorners = 12;

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

    TTreeWidget* tree() const { return mpEditor->treeWidget_triggers; }

    // Off a shot of the whole window rather than of the tree alone: what the row
    // is painted over is the pane the panel is filled with, and a widget grabbed
    // on its own comes back without whatever shows through it.
    QImage windowShot() const { return mpEditor->grab().toImage(); }

    QPoint inViewport(const QPoint& point) const { return tree()->viewport()->mapTo(mpEditor, point); }

    // The band a row occupies across the whole width of the viewport, which is
    // what the selection is drawn into - rather than the row's own rectangle,
    // which where the view indents starts past the arrow column
    QRect rowBand(QTreeWidgetItem* pItem) const
    {
        const QRect rowRect = tree()->visualItemRect(pItem);
        return QRect(0, rowRect.top(), tree()->viewport()->width(), rowRect.height());
    }

    // Both surfaces are flat fills, so the nearer of the two is the one that was
    // painted where a pixel sits between them
    static int distanceBetween(const QColor& one, const QColor& other) { return std::abs(one.red() - other.red()) + std::abs(one.green() - other.green()) + std::abs(one.blue() - other.blue()); }

    static QString describe(const QColor& measured, const uiDesign::ThemeTokens& tokens)
    {
        return qsl("%1 - the pane is %2, the accent %3, the page %4").arg(measured.name(), tokens.pane.name(), tokens.accent.name(), tokens.page.name());
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
        mpEditor->resize(1000, 800);
        mpEditor->slot_showTriggers();
        QTest::qWait(50ms);

        // Each of these is added under the one before it, because the editor
        // adds a new item inside whichever folder is chosen and chooses what it
        // has just added
        mpEditor->addTrigger(true);
        mpOuterGroup = tree()->currentItem();
        mpEditor->addTrigger(true);
        mpInnerGroup = tree()->currentItem();
        mpEditor->addTrigger(false);
        QTest::qWait(50ms);

        QVERIFY2(mpOuterGroup && mpInnerGroup, "The editor did not make the two groups this test reads a row out of");
        QVERIFY2(mpInnerGroup->parent() == mpOuterGroup, "The second group was not made inside the first, so the row read below is not a nested one");
        QVERIFY2(mpInnerGroup->childCount() == 1, "The trigger was not made inside the second group, so that group has nothing to fold and no chevron");

        mpEditor->mpTriggerBaseItem->setExpanded(true);
        mpOuterGroup->setExpanded(true);
        mpInnerGroup->setExpanded(true);
        tree()->setCurrentItem(mpInnerGroup);
        // The mark that used to square the pill off is only drawn on the row
        // the keyboard is on, so a tree without the keyboard measures nothing
        mpEditor->activateWindow();
        tree()->setFocus();
        QTest::qWait(50ms);
        QVERIFY2(tree()->hasFocus(), "The tree does not hold the keyboard, so the state a chosen row is drawn in here is not the one the reader sees");

        QVERIFY2(!rowBand(mpInnerGroup).isEmpty(), "The chosen row is not laid out");
        QVERIFY2(rowBand(mpInnerGroup).width() > 4 * scmClearOfTheCorners, "The panel of items is too narrow to read a row across");
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

    // The whole point: the column the chevron stands in and the column the name
    // is written in are one colour. Read a pixel under the top of the row, which
    // is above everything drawn into it - the chevron, the dot and the name are
    // all centred on the row's middle - so what is measured is the fill.
    void test_theArrowColumnAndTheNameColumnAreOneTone()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QRect band = rowBand(mpInnerGroup);
        const QImage shot = windowShot();
        const int y = band.top() + 1;

        // Inside the second indentation step, which is where the row's chevron
        // is - and where the view used to draw the innermost of the two branch
        // cells it laid out for a row this deep
        const QColor arrowColumn = shot.pixelColor(inViewport(QPoint(band.left() + 30, y)));
        const QColor nameColumn = shot.pixelColor(inViewport(QPoint(band.right() - 20, y)));

        qInfo().noquote() << qsl("measured across the chosen row: arrow column %1, name column %2; the pane is %3 and the accent %4")
                                     .arg(arrowColumn.name(), nameColumn.name(), tokens.pane.name(), tokens.accent.name());

        QVERIFY2(arrowColumn.rgb() != tokens.pane.rgb(), qPrintable(qsl("the chosen row is not painted at all: the arrow column is %1").arg(describe(arrowColumn, tokens))));
        QVERIFY2(arrowColumn.rgb() == nameColumn.rgb(),
                 qPrintable(qsl("the chosen row is two tones: its arrow column is painted %1 and the far end of it %2").arg(arrowColumn.name(), nameColumn.name())));

        // ...and the same the whole way between them, which is what says the
        // fill is flat rather than a wash that fades along the row
        QStringList uneven;
        for (int x = band.left() + scmClearOfTheCorners; x <= band.right() - scmClearOfTheCorners; ++x) {
            const QColor measured = shot.pixelColor(inViewport(QPoint(x, y)));
            if (measured.rgb() != arrowColumn.rgb()) {
                uneven << qsl("x=%1 %2").arg(QString::number(x), measured.name());
            }
        }
        QVERIFY2(uneven.isEmpty(),
                 qPrintable(qsl("the chosen row is not one tone from end to end: it is %1 at the arrow column and %2 elsewhere")
                                    .arg(arrowColumn.name(), uneven.mid(0, 12).join(qsl(", ")))));
    }

    // ...and it is a pill: the four extreme corners of the band are cut away to
    // the panel behind, while the middle of its top edge is the fill. A branch
    // column painted by the style is a plain rectangle, so the leading two
    // corners used to be as solid as the rest of it.
    //
    // "Cut away to the panel" rather than "is the panel to the last digit":
    // where a platform style marks the row the keyboard is on, it draws that
    // mark as a square hairline round the whole row, and its own corner passes
    // through the pixel being read. So the reading has to land nearer the panel
    // than the fill, by a margin - and did not, by any margin, while the arrow
    // column was a solid block of the platform's selection colour.
    void test_theSelectionIsAPillWithRoundedCorners()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QRect band = rowBand(mpInnerGroup);
        const QImage shot = windowShot();

        const QColor fill = shot.pixelColor(inViewport(QPoint(band.center().x(), band.top() + 1)));
        const QColor topMiddle = shot.pixelColor(inViewport(QPoint(band.center().x(), band.top())));
        const QList<QPair<QString, QColor>> corners{{qsl("leading top"), shot.pixelColor(inViewport(band.topLeft()))},
                                                    {qsl("trailing top"), shot.pixelColor(inViewport(band.topRight()))},
                                                    {qsl("leading bottom"), shot.pixelColor(inViewport(band.bottomLeft()))},
                                                    {qsl("trailing bottom"), shot.pixelColor(inViewport(band.bottomRight()))}};

        QStringList measured;
        for (const auto& corner : corners) {
            measured << qsl("%1 %2").arg(corner.first, corner.second.name());
        }
        qInfo().noquote() << qsl("corners of the chosen row: %1; its fill is %2, the middle of its top edge %3, the pane %4")
                                     .arg(measured.join(qsl(", ")), fill.name(), topMiddle.name(), tokens.pane.name());

        QVERIFY2(topMiddle.rgb() != tokens.pane.rgb(),
                 qPrintable(qsl("the chosen row does not reach the top of its band, so the corners below say nothing: the middle of that edge is %1").arg(describe(topMiddle, tokens))));
        for (const auto& corner : corners) {
            // The panel itself, to the last digit: the pill's arc does not
            // reach the extreme corner of the band, so nothing is painted
            // there at all. It used to be a wash of the platform's focus ring,
            // whose square corner passed through this very pixel.
            QVERIFY2(corner.second.rgb() == tokens.pane.rgb(),
                     qPrintable(qsl("the chosen row's %1 corner is not cut away to the panel: it is painted %2, %3 off the panel and %4 off the row's own fill of %5")
                                        .arg(corner.first,
                                             describe(corner.second, tokens),
                                             QString::number(distanceBetween(corner.second, tokens.pane)),
                                             QString::number(distanceBetween(corner.second, fill)),
                                             fill.name())));
        }
    }

    // The user's own reading of the pair, and the one that says the two states
    // are drawn as one shape: a hovered row and a chosen row, on the same tree,
    // measured at the same four corners and along the same two edges. The hover
    // was always right - what a row is filled with is a stylesheet rule either
    // way - and it is the chosen row that used to gain a square hairline around
    // the whole of it, drawn by the platform over the rounded fill.
    void test_theChosenRowAndTheHoveredRowAreTheSameShape()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();

        // A sibling of the chosen row rather than the row itself: the two
        // states have to be read at once, off one shot, or a difference in the
        // window between the two grabs would read as a difference in the shape
        QTreeWidgetItem* pHovered = mpOuterGroup;
        QVERIFY2(pHovered != mpInnerGroup, "The hovered and the chosen row must be two different rows");
        const QRect hoveredBand = rowBand(pHovered);
        const QPoint hoveredCentre = hoveredBand.center();
        QMouseEvent move(QEvent::MouseMove, QPointF(hoveredCentre), QPointF(hoveredCentre), tree()->viewport()->mapToGlobal(hoveredCentre), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(tree()->viewport(), &move);
        QTest::qWait(50ms);

        const QImage shot = windowShot();
        const QRect chosenBand = rowBand(mpInnerGroup);
        auto at = [&shot, this](const QPoint& point) {
            return shot.pixelColor(inViewport(point));
        };

        const QColor hoveredFill = at(QPoint(hoveredBand.center().x(), hoveredBand.top() + 3));
        const QColor chosenFill = at(QPoint(chosenBand.center().x(), chosenBand.top() + 3));
        QVERIFY2(hoveredFill.rgb() != tokens.pane.rgb(), qPrintable(qsl("the row under the pointer is not tinted at all: it is painted %1").arg(describe(hoveredFill, tokens))));
        QVERIFY2(chosenFill.rgb() != tokens.pane.rgb(), qPrintable(qsl("the chosen row is not painted at all: it is %1").arg(describe(chosenFill, tokens))));
        QVERIFY2(hoveredFill.rgb() != chosenFill.rgb(), "the hovered row and the chosen row are filled with the same colour, so this case is reading one state twice");

        // Four corners and the middle of each edge. A shape drawn to the same
        // radius on the same rectangle leaves the corners empty and the edges
        // filled, whichever of the two states it is in.
        const QList<QPair<QString, QPoint>> readings{
                {qsl("leading top corner"), QPoint(0, 0)}, {qsl("trailing top corner"), QPoint(1, 0)}, {qsl("leading bottom corner"), QPoint(0, 1)}, {qsl("trailing bottom corner"), QPoint(1, 1)}};
        QStringList measured;
        QStringList disagreed;
        for (const auto& reading : readings) {
            const auto cornerOf = [&reading](const QRect& band) {
                return QPoint(reading.second.x() == 0 ? band.left() : band.right(), reading.second.y() == 0 ? band.top() : band.bottom());
            };
            const QColor hovered = at(cornerOf(hoveredBand));
            const QColor chosen = at(cornerOf(chosenBand));
            measured << qsl("%1 hovered %2 chosen %3").arg(reading.first, hovered.name(), chosen.name());
            if (hovered.rgb() != tokens.pane.rgb() || chosen.rgb() != tokens.pane.rgb()) {
                disagreed << qsl("%1: hovered %2, chosen %3").arg(reading.first, hovered.name(), chosen.name());
            }
        }
        qInfo().noquote()
                << qsl("corners of the two rows: %1; the panel is %2, the hover fill %3, the chosen fill %4").arg(measured.join(qsl("; ")), tokens.pane.name(), hoveredFill.name(), chosenFill.name());

        QVERIFY2(disagreed.isEmpty(),
                 qPrintable(
                         qsl("the hovered row and the chosen row are not cut to the same corner - the panel is %1 and these corners are not: %2").arg(tokens.pane.name(), disagreed.join(qsl(", ")))));

        // ...and no hairline anywhere along the chosen row's edges: the pixel
        // one in from either end of its top edge is the fill, the same as the
        // hovered row's is
        QStringList outlined;
        for (const int x : {chosenBand.left() + scmClearOfTheCorners, chosenBand.center().x(), chosenBand.right() - scmClearOfTheCorners}) {
            const QColor chosenEdge = at(QPoint(x, chosenBand.top()));
            const QColor hoveredEdge = at(QPoint(x, hoveredBand.top()));
            if (chosenEdge.rgb() != chosenFill.rgb() || hoveredEdge.rgb() != hoveredFill.rgb()) {
                outlined << qsl("x=%1 chosen %2 against a fill of %3, hovered %4 against %5").arg(QString::number(x), chosenEdge.name(), chosenFill.name(), hoveredEdge.name(), hoveredFill.name());
            }
        }
        QVERIFY2(outlined.isEmpty(), qPrintable(qsl("one of the two rows carries an outline the other does not: %1").arg(outlined.join(qsl(", ")))));
    }

    // The control case: the sidebar's chosen row, drawn from the same corner
    // and the same accent by rules of its own. It never had the fault - a list
    // draws no row-wide focus mark - so a reading that says otherwise would
    // mean the corners are being measured wrong rather than painted wrong.
    void test_theSidebarsChosenRowIsCutToTheSameCorner()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        QListWidget* pSidebar = mpEditor->mpListWidget_editorSidebar;
        QVERIFY2(pSidebar != nullptr, "The editor has no sidebar");
        QListWidgetItem* pChosen = pSidebar->currentItem();
        QVERIFY2(pChosen != nullptr, "No sidebar row is chosen");

        const QRect rowRect = pSidebar->visualItemRect(pChosen);
        QVERIFY2(!rowRect.isEmpty(), "The chosen sidebar row is not laid out");
        const QImage shot = windowShot();
        auto at = [&](const QPoint& point) {
            return shot.pixelColor(pSidebar->viewport()->mapTo(mpEditor, point));
        };

        const QColor fill = at(QPoint(rowRect.center().x(), rowRect.center().y()));
        const QList<QPair<QString, QPoint>> corners{
                {qsl("leading top"), rowRect.topLeft()}, {qsl("trailing top"), rowRect.topRight()}, {qsl("leading bottom"), rowRect.bottomLeft()}, {qsl("trailing bottom"), rowRect.bottomRight()}};
        QStringList measured;
        for (const auto& corner : corners) {
            measured << qsl("%1 %2").arg(corner.first, at(corner.second).name());
        }
        qInfo().noquote() << qsl("corners of the sidebar's chosen row: %1; its fill is %2 and the page %3").arg(measured.join(qsl(", ")), fill.name(), tokens.page.name());

        QVERIFY2(fill.rgb() != tokens.page.rgb(), qPrintable(qsl("the sidebar's chosen row is not painted at all: its middle is %1").arg(fill.name())));
        for (const auto& corner : corners) {
            const QColor cornerColor = at(corner.second);
            QVERIFY2(distanceBetween(cornerColor, tokens.page) < distanceBetween(cornerColor, fill),
                     qPrintable(qsl("the sidebar's %1 corner is not cut away: it is painted %2 against a page of %3 and a fill of %4")
                                        .arg(corner.first, cornerColor.name(), tokens.page.name(), fill.name())));
        }
    }

    // The tone itself is mixed from the two colours the window is drawn out of,
    // rather than being whatever the platform fills a selected row with. Checked
    // without naming how much of the accent goes in: every channel has to be the
    // same fraction of the way from the pane to the accent.
    void test_theToneIsTheAccentMixedIntoThePane()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QRect band = rowBand(mpInnerGroup);
        const QColor measured = windowShot().pixelColor(inViewport(QPoint(band.center().x(), band.top() + 1)));

        const int channels[3][3] = {{tokens.pane.red(), tokens.accent.red(), measured.red()},
                                    {tokens.pane.green(), tokens.accent.green(), measured.green()},
                                    {tokens.pane.blue(), tokens.accent.blue(), measured.blue()}};
        // The channel the two colours are furthest apart in is the one the
        // fraction can be read off with any accuracy
        int widest = 0;
        for (int channel = 1; channel < 3; ++channel) {
            if (std::abs(channels[channel][1] - channels[channel][0]) > std::abs(channels[widest][1] - channels[widest][0])) {
                widest = channel;
            }
        }
        const int spread = channels[widest][1] - channels[widest][0];
        QVERIFY2(std::abs(spread) > 16, "the pane and the accent are too close together on this theme to read a mixture of them");
        const qreal fraction = static_cast<qreal>(channels[widest][2] - channels[widest][0]) / spread;
        QVERIFY2(fraction > 0.05 && fraction < 0.95, qPrintable(qsl("the chosen row is painted %1, which is not a mixture of the pane and the accent at all").arg(describe(measured, tokens))));

        for (const auto& channel : channels) {
            const int wanted = qRound(channel[0] + fraction * (channel[1] - channel[0]));
            QVERIFY2(std::abs(channel[2] - wanted) <= 2,
                     qPrintable(qsl("the chosen row is painted %1, which is not the accent mixed into the pane - at %2 of the way it would be %3")
                                        .arg(describe(measured, tokens), QString::number(fraction, 'f', 2), QString::number(wanted))));
        }
    }
};

#include "EditorTreeSelectionPillTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorTreeSelectionPillTest)
