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
 * A QToolBar too narrow for what it holds posts the tail of itself into a
 * drop-down behind a chevron a few pixels wide at the far edge of the window.
 * Nothing says it is there, and what went into it - Save Profile among them -
 * is out of reach until it is found.
 *
 * The bar gives its names up instead. As the room runs out its groups become
 * rows of pictures in turn, the profile's four first and the item's four after,
 * and the tooltips speak for them; widening gives the names back from the other
 * end, and only with room to spare so a drag across the breakpoint settles
 * rather than flickering. What the cases below hold to is that every action
 * stays on the bar at every width the window can be dragged to.
 */

#include <QLayout>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QtTest/QtTest>
#include <chrono>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TelnetServerStub.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorToolBarOverflowTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorToolBarOverflow-Test-Profile");
    QString mPort; // assigned the stub's actual ephemeral port in initTestCase()
    const QString mLocalhost = qsl("localhost");

    // Where each group sits in dlgTriggerEditor::mEditorToolBarGroups, which is
    // also the order they give their names up in
    static constexpr int scmProfileGroup = 0;
    static constexpr int scmItemGroup = 1;

    // The sweep: wide enough for every name at any icon size, down to the
    // narrowest the editor can be dragged to, in steps fine enough to place a
    // breakpoint within a button's width
    static constexpr int scmSweepWidest = 1600;
    static constexpr int scmSweepStep = 8;
    static constexpr int scmSweepHeight = 700;
    // What the preference holds until somebody changes it, and the largest it
    // offers - the two the breakpoints are reported at
    static constexpr int scmDefaultPreference = 3;
    static constexpr int scmLargestPreference = 4;

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
        if (!spy.wait(1000)) {
            QFAIL("Could not connect with the host.");
        }
    }

    // The button a QToolBar posts what it cannot fit into. It exists from the
    // start and is shown only while something has been posted to it, so its
    // visibility is the fold itself rather than a proxy for it.
    QWidget* extensionButton() const { return mpEditor->toolBar->findChild<QWidget*>(qsl("qt_toolbar_ext_button")); }

    void resizeTo(const int width)
    {
        mpEditor->resize(width, scmSweepHeight);
        QCoreApplication::sendPostedEvents();
        QTest::qWait(10ms);
    }

    // The narrowest the window can actually be dragged to, which is where every
    // sweep stops - a width below it is one no user can reach
    int narrowestWindowWidth() const { return std::max(mpEditor->minimumWidth(), mpEditor->minimumSizeHint().width()); }

    // Whether every button of a group is still carrying its name. Read off the
    // buttons rather than off the bookkeeping, so a group the bookkeeping
    // thinks is named but that was never told is a failure
    bool groupIsNamed(const int index) const
    {
        const auto& group = mpEditor->mEditorToolBarGroups.at(index);
        for (QAction* pAction : group.actions) {
            auto* pButton = qobject_cast<QToolButton*>(mpEditor->toolBar->widgetForAction(pAction));
            if (!pButton || pButton->toolButtonStyle() != Qt::ToolButtonTextBesideIcon) {
                return false;
            }
        }
        return true;
    }

    QString stateAt(const int width) const
    {
        return qsl("%1px of window (%2px of bar): profile %3, item %4")
                .arg(QString::number(width),
                     QString::number(mpEditor->toolBar->width()),
                     groupIsNamed(scmProfileGroup) ? qsl("named") : qsl("pictures"),
                     groupIsNamed(scmItemGroup) ? qsl("named") : qsl("pictures"));
    }

    // The length the bar wants laid out in one line with the styles it is
    // carrying - the same figure the fit measures against its own width
    int barLengthWanted() const { return mpEditor->toolBar->layout()->sizeHint().width(); }

    // What the window costs the bar: everything outside the toolbar's own width
    int windowChromeWidth() const { return mpEditor->width() - mpEditor->toolBar->width(); }

    // Every name back on the bar and the fit asked once for what this width
    // actually calls for. Put back by hand because it was taken by hand: a
    // resize to the width the window already has sends no event, so nothing
    // would run on its own - and by hand rather than through the fit alone, so
    // that a case measuring with the fit disabled still starts from a named bar.
    void undoTheForcing()
    {
        for (auto& group : mpEditor->mEditorToolBarGroups) {
            group.labelsShown = true;
        }
        mpEditor->applyEditorToolbarButtonStyles();
        mpEditor->fitEditorToolBarToItsLength();
        QTest::qWait(20ms);
    }

    // The shortest the bar can be made - both groups pictures - said as a width
    // of window, which is what it has to be compared against
    int pictureBarLength()
    {
        resizeTo(scmSweepWidest);
        const int chrome = windowChromeWidth();
        for (auto& group : mpEditor->mEditorToolBarGroups) {
            group.labelsShown = false;
        }
        mpEditor->applyEditorToolbarButtonStyles();
        QTest::qWait(20ms);
        const int wanted = barLengthWanted() + chrome;
        undoTheForcing();
        return wanted;
    }

    // ...and the same with only the item group named, which is the length the
    // second breakpoint is measured against. The wording is the view's, so this
    // is a different number in every view.
    int itemGroupNamedLength()
    {
        resizeTo(scmSweepWidest);
        const int chrome = windowChromeWidth();
        mpEditor->mEditorToolBarGroups[scmProfileGroup].labelsShown = false;
        mpEditor->mEditorToolBarGroups[scmItemGroup].labelsShown = true;
        mpEditor->applyEditorToolbarButtonStyles();
        QTest::qWait(20ms);
        const int wanted = barLengthWanted() + chrome;
        undoTheForcing();
        return wanted;
    }

    struct Sweep
    {
        // The widest window at which each of these is true, found narrowing
        // from a window with room for everything
        int profileGaveUp = -1;
        int bothGaveUp = -1;
        // Qt posted something into its drop-down - and did so while a name was
        // still being written out, which is the fold that would mean the fit
        // had given up early
        int folded = -1;
        int foldedWithANameStillWritten = -1;
        int narrowest = 0;
    };

    Sweep sweepNarrowing()
    {
        Sweep result;
        resizeTo(scmSweepWidest);
        result.narrowest = narrowestWindowWidth();
        const QWidget* pExtension = extensionButton();
        int reachedBefore = 0;
        for (int width = scmSweepWidest; width >= result.narrowest; width -= scmSweepStep) {
            resizeTo(width);
            // A window that will not go any narrower has nothing left to say
            if (reachedBefore && mpEditor->width() >= reachedBefore) {
                break;
            }
            reachedBefore = mpEditor->width();
            const bool profileNamed = groupIsNamed(scmProfileGroup);
            const bool itemNamed = groupIsNamed(scmItemGroup);
            if (result.profileGaveUp < 0 && !profileNamed) {
                result.profileGaveUp = width;
            }
            if (result.bothGaveUp < 0 && !profileNamed && !itemNamed) {
                result.bothGaveUp = width;
            }
            if (pExtension && pExtension->isVisible()) {
                if (result.folded < 0) {
                    result.folded = width;
                }
                if (result.foldedWithANameStillWritten < 0 && (profileNamed || itemNamed)) {
                    result.foldedWithANameStillWritten = width;
                }
            }
        }
        return result;
    }

    // mudlet::setToolBarIconSize() drops a value equal to the one it holds, so
    // the step through a different value is what makes it run
    void setPreference(const int value)
    {
        mudlet::self()->setToolBarIconSize(value == 1 ? 2 : 1);
        mudlet::self()->setToolBarIconSize(value);
        QCoreApplication::sendPostedEvents();
        QTest::qWait(50ms);
    }

private slots:
    void initTestCase()
    {
        if (portableMarkerPresent()) {
            QSKIP("portable.txt present - it takes precedence over XDG_CONFIG_HOME, "
                  "so the config dir cannot be redirected");
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

        mudlet::self()->slot_showEditorDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        QVERIFY2(mpEditor->mEditorToolBarGroups.count() == 2, "The bar is expected to have a profile group and an item group");
        setPreference(scmDefaultPreference);
        mpEditor->slot_showTriggers();
        resizeTo(scmSweepWidest);
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

    // Given the room, the bar reads as what it is: two runs of named buttons
    // with the arrows between them
    void test_everyGroupIsNamedOnAWideWindow()
    {
        resizeTo(scmSweepWidest);
        QVERIFY2(groupIsNamed(scmProfileGroup) && groupIsNamed(scmItemGroup), qPrintable(qsl("On a window with room to spare, %1").arg(stateAt(scmSweepWidest))));
    }

    // The order the names are given up in: the four that act on the profile are
    // the first to stand as pictures, and there is a width where they have and
    // the four that act on the item have not
    void test_theProfileGroupIsTheFirstToStandAsPictures()
    {
        const Sweep sweep = sweepNarrowing();
        qInfo().noquote() << qsl("  narrowing from %1px to %2px: the profile group gives its names up at %3px, the item group at %4px")
                                     .arg(QString::number(scmSweepWidest), QString::number(sweep.narrowest), QString::number(sweep.profileGaveUp), QString::number(sweep.bothGaveUp));

        QVERIFY2(sweep.profileGaveUp > 0, "The profile group never gave its names up, so the bar was never asked to fit into anything");
        QVERIFY2(sweep.bothGaveUp < 0 || sweep.profileGaveUp > sweep.bothGaveUp,
                 qPrintable(qsl("The item group gave its names up at %1px and the profile group only at %2px - the wrong way round")
                                    .arg(QString::number(sweep.bothGaveUp), QString::number(sweep.profileGaveUp))));
    }

    // ...and narrower still, both groups are pictures rather than anything
    // being taken off the bar
    void test_bothGroupsStandAsPicturesOnANarrowWindow()
    {
        const Sweep sweep = sweepNarrowing();
        QVERIFY2(sweep.bothGaveUp > 0,
                 qPrintable(qsl("Down to %1px, the narrowest the window goes, the item group never gave its names up: %2").arg(QString::number(sweep.narrowest), stateAt(mpEditor->width()))));
        resizeTo(sweep.narrowest);
        QVERIFY2(!groupIsNamed(scmProfileGroup) && !groupIsNamed(scmItemGroup), qPrintable(qsl("At the narrowest the window goes, %1").arg(stateAt(sweep.narrowest))));
    }

    // Nothing is ever posted away while there is still a name to give up. Qt's
    // fold is left in place below that - a window narrower than a bar of
    // pictures has run out of anything the fit could do - but it is never
    // reached with a word still written out on the bar.
    void test_nothingIsFoldedAwayWhileANameIsStillWritten()
    {
        const Sweep sweep = sweepNarrowing();
        QVERIFY2(extensionButton() != nullptr, "The bar has no overflow button, so this case is watching nothing");
        qInfo().noquote() << qsl("  narrowing to %1px: the fold is first reached at %2px, by which width both groups are pictures")
                                     .arg(QString::number(sweep.narrowest), sweep.folded < 0 ? qsl("no width swept") : QString::number(sweep.folded));

        QVERIFY2(sweep.foldedWithANameStillWritten < 0,
                 qPrintable(qsl("At %1px of window the bar posted actions into its overflow menu with names still written out on it - the fit had room left to make and did not take it. "
                                "Both groups only became pictures at %2px.")
                                    .arg(QString::number(sweep.foldedWithANameStillWritten), QString::number(sweep.bothGaveUp))));
    }

    // Widening gives the names back from the other end: the item group first,
    // the profile group last
    void test_wideningGivesTheNamesBackInTheOtherOrder()
    {
        const Sweep sweep = sweepNarrowing();
        resizeTo(sweep.narrowest);
        QVERIFY2(!groupIsNamed(scmProfileGroup) && !groupIsNamed(scmItemGroup), qPrintable(qsl("This case starts from a bar of pictures, and instead %1").arg(stateAt(sweep.narrowest))));

        int itemNamedAt = -1;
        int profileNamedAt = -1;
        for (int width = sweep.narrowest; width <= scmSweepWidest; width += scmSweepStep) {
            resizeTo(width);
            if (itemNamedAt < 0 && groupIsNamed(scmItemGroup)) {
                itemNamedAt = width;
            }
            if (profileNamedAt < 0 && groupIsNamed(scmProfileGroup)) {
                profileNamedAt = width;
            }
        }
        qInfo().noquote() << qsl("  widening from %1px: the item group has its names back at %2px, the profile group at %3px")
                                     .arg(QString::number(sweep.narrowest), QString::number(itemNamedAt), QString::number(profileNamedAt));

        QVERIFY2(itemNamedAt > 0 && profileNamedAt > 0,
                 qPrintable(qsl("Widening back to %1px did not give both groups their names: item at %2, profile at %3")
                                    .arg(QString::number(scmSweepWidest), QString::number(itemNamedAt), QString::number(profileNamedAt))));
        QVERIFY2(itemNamedAt < profileNamedAt,
                 qPrintable(qsl("The profile group took its names back at %1px and the item group only at %2px - the last to give them up has to be the last to take them back")
                                    .arg(QString::number(profileNamedAt), QString::number(itemNamedAt))));
    }

    // The margin the names are taken back with, which is what a drag across the
    // breakpoint is settled by: the same two pixels back and forth cannot keep
    // turning the names on and off under the pointer
    void test_aDragAcrossTheBreakpointDoesNotFlicker()
    {
        const Sweep sweep = sweepNarrowing();
        QVERIFY2(sweep.profileGaveUp > 0, "No breakpoint was found to drag across");

        resizeTo(sweep.profileGaveUp);
        const bool settled = groupIsNamed(scmProfileGroup);
        QStringList seen;
        for (int pass = 0; pass < 10; ++pass) {
            resizeTo(sweep.profileGaveUp + 2);
            const bool wider = groupIsNamed(scmProfileGroup);
            resizeTo(sweep.profileGaveUp);
            const bool narrower = groupIsNamed(scmProfileGroup);
            if (wider != settled || narrower != settled) {
                seen.append(qsl("pass %1: %2px %3, %4px %5")
                                    .arg(QString::number(pass),
                                         QString::number(sweep.profileGaveUp + 2),
                                         wider ? qsl("named") : qsl("pictures"),
                                         QString::number(sweep.profileGaveUp),
                                         narrower ? qsl("named") : qsl("pictures")));
            }
        }
        QVERIFY2(seen.isEmpty(),
                 qPrintable(qsl("Two pixels either side of the %1px breakpoint changed the profile group, so a drag over it flickers:\n  %2")
                                    .arg(QString::number(sweep.profileGaveUp), seen.join(qsl("\n  ")))));

        // What the fit costs, which is what says whether a burst of resizes
        // needs coalescing behind a zero-timer or can simply be answered
        QElapsedTimer timer;
        timer.start();
        constexpr int runs = 1000;
        for (int run = 0; run < runs; ++run) {
            mpEditor->fitEditorToolBarToItsLength();
        }
        qInfo().noquote() << qsl("  the fit costs %1us a call at a width it settles at").arg(QString::number(static_cast<double>(timer.nsecsElapsed()) / (runs * 1000.0), 'f', 1));
    }

    // The item group's names are the view's words - "Add Trigger" in one view
    // and "Add Alias" in the next - so the width they are given up at moves with
    // the view, and switching views is a re-measurement rather than a repaint
    void test_switchingViewRemeasuresForTheNewWording()
    {
        // The item group's own breakpoint: the width the bar wants with the
        // profile group already pictures and this group still named, which is
        // where the second lot of names goes
        mpEditor->slot_showTriggers();
        const int triggerViewBoundary = itemGroupNamedLength();
        mpEditor->slot_showAliases();
        const int aliasViewBoundary = itemGroupNamedLength();
        qInfo().noquote() << qsl("  with the profile group already pictures, the bar wants %1px of window for the trigger view's names and %2px for the alias view's")
                                     .arg(QString::number(triggerViewBoundary), QString::number(aliasViewBoundary));

        QVERIFY2(aliasViewBoundary < triggerViewBoundary,
                 qPrintable(qsl("The two views' wordings measure the same (%1px against %2px), so nothing here would show a re-measurement")
                                    .arg(QString::number(aliasViewBoundary), QString::number(triggerViewBoundary))));

        // Roomy for the alias wording by more than the margin the names are
        // taken back with, and too narrow for the trigger wording: the one
        // window at which the two views disagree about the item group
        const int between = aliasViewBoundary + 20;
        QVERIFY2(between < triggerViewBoundary,
                 qPrintable(qsl("The wordings are %1px apart, which leaves no width that is roomy for one and not for the other").arg(QString::number(triggerViewBoundary - aliasViewBoundary))));

        // Arrived at by switching the view...
        resizeTo(scmSweepWidest);
        mpEditor->slot_showTriggers();
        QTest::qWait(20ms);
        resizeTo(between);
        const bool namedInTriggerView = groupIsNamed(scmItemGroup);
        mpEditor->slot_showAliases();
        QTest::qWait(20ms);
        const bool namedAfterSwitching = groupIsNamed(scmItemGroup);

        // ...against arriving at the same window in the same view by a resize
        resizeTo(scmSweepWidest);
        QTest::qWait(20ms);
        resizeTo(between);
        const bool namedAfterResizing = groupIsNamed(scmItemGroup);

        qInfo().noquote() << qsl("  at %1px of window the item group is %2 in the trigger view, %3 after switching to the alias view, and %4 on a fresh resize there")
                                     .arg(QString::number(between),
                                          namedInTriggerView ? qsl("named") : qsl("pictures"),
                                          namedAfterSwitching ? qsl("named") : qsl("pictures"),
                                          namedAfterResizing ? qsl("named") : qsl("pictures"));

        QVERIFY2(!namedInTriggerView, "The trigger view's wording was supposed to be too wide for this window, and the item group kept its names");
        QVERIFY2(namedAfterSwitching == namedAfterResizing,
                 qPrintable(qsl("Switching to the alias view left the item group %1, where a resize to the same width in that view leaves it %2 - the view change did not re-measure")
                                    .arg(namedAfterSwitching ? qsl("named") : qsl("pictures"), namedAfterResizing ? qsl("named") : qsl("pictures"))));
        QVERIFY2(namedAfterSwitching, "The alias view's wording fits this window with room to spare, so the item group should have taken its names back");
        mpEditor->slot_showTriggers();
    }

    // A button standing as a picture is read by its tooltip alone, so each of
    // the eight says the words it would otherwise be carrying
    void test_everyCollapsingButtonIsNamedByItsTooltip()
    {
        resizeTo(scmSweepWidest);
        QStringList silent;
        for (const auto& group : mpEditor->mEditorToolBarGroups) {
            for (const QAction* pAction : group.actions) {
                const QString wording = pAction->text();
                const QString tip = pAction->toolTip();
                if (wording.isEmpty() || !tip.contains(wording)) {
                    silent.append(qsl("%1 -> \"%2\"").arg(wording.isEmpty() ? qsl("<unnamed action>") : wording, tip));
                }
            }
        }
        QVERIFY2(silent.isEmpty(), qPrintable(qsl("These buttons do not say their own name in their tooltip, so they say nothing once they are pictures:\n  %1").arg(silent.join(qsl("\n  ")))));
    }

    // The fallback, measured rather than assumed. A bar of pictures still wants
    // more width than the narrowest the editor can be dragged to, so Qt's fold
    // is reachable - on a window too narrow to work in, but reachable. It is
    // left in place rather than replaced with a scrolling host of our own,
    // which would be a second way of hiding the same buttons; what is done
    // instead is to make its button unmistakable, which the case below checks.
    // The numbers this reports are the ones docs/design-language.md quotes.
    void test_theBarOfPicturesIsMeasuredAgainstTheNarrowestWindow()
    {
        QStringList measured;
        for (const int preference : {scmDefaultPreference, scmLargestPreference}) {
            setPreference(preference);
            const int pictureBar = pictureBarLength();
            const Sweep sweep = sweepNarrowing();
            measured.append(qsl("at preference %1 (%2px glyphs) the profile group gives its names up at %3px and the item group at %4px; a bar of pictures wants %5px, the window goes down to %6px, "
                                "and the fold is reached at %7px")
                                    .arg(QString::number(preference),
                                         QString::number(mpEditor->toolBar->iconSize().width()),
                                         QString::number(sweep.profileGaveUp),
                                         QString::number(sweep.bothGaveUp),
                                         QString::number(pictureBar),
                                         QString::number(sweep.narrowest),
                                         sweep.folded < 0 ? qsl("no swept width") : QString::number(sweep.folded)));

            // Whatever the fold costs, it is only ever paid below the length a
            // bar of pictures wants: everything above it is the fit's to hold,
            // and both groups have given their names up before it is reached
            QVERIFY2(sweep.folded < 0 || sweep.folded <= pictureBar, qPrintable(measured.constLast()));
            QVERIFY2(sweep.folded < 0 || (sweep.bothGaveUp > 0 && sweep.bothGaveUp > sweep.folded), qPrintable(measured.constLast()));
        }
        qInfo().noquote() << qsl("  %1").arg(measured.join(qsl("\n  ")));
        setPreference(scmDefaultPreference);
    }

    // ...and where it is reached, the button is a control rather than the two
    // faint arrowheads a style leaves on the page: the design's chevron, inked
    // in the tone a lit control is drawn in
    void test_theFoldsOwnButtonIsTheDesignsChevron()
    {
        auto* pExtension = qobject_cast<QToolButton*>(extensionButton());
        QVERIFY2(pExtension != nullptr, "The bar has no overflow button to draw");
        const QIcon chevron = pExtension->icon();
        QVERIFY2(!chevron.isNull(), "The overflow button carries no picture of ours, so the style's own is what shows");

        const QImage drawn = chevron.pixmap(pExtension->iconSize()).toImage();
        QVERIFY2(!drawn.isNull(), "The overflow button's picture is empty");
        QColor strongest;
        int strongestAlpha = 0;
        for (int x = 0; x < drawn.width(); ++x) {
            for (int y = 0; y < drawn.height(); ++y) {
                const QColor pixel = drawn.pixelColor(x, y);
                if (pixel.alpha() > strongestAlpha) {
                    strongestAlpha = pixel.alpha();
                    strongest = pixel;
                }
            }
        }
        const QColor lit = uiDesign::themeTokens().accentText;
        qInfo().noquote() << qsl("  the chevron's strongest pixel is %1 at alpha %2, against the lit tone %3").arg(strongest.name(), QString::number(strongestAlpha), lit.name());
        QVERIFY2(strongestAlpha > 200, qPrintable(qsl("The chevron is barely drawn - its strongest pixel is alpha %1").arg(QString::number(strongestAlpha))));
        QVERIFY2(strongest.rgb() == lit.rgb(), qPrintable(qsl("The chevron is inked %1 rather than the lit tone %2").arg(strongest.name(), lit.name())));
    }
};

#include "EditorToolBarOverflowTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorToolBarOverflowTest)
