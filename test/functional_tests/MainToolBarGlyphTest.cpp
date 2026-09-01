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
 * The main window toolbar draws the same monochrome glyphs the editor and the
 * settings dialog do, inked from the palette at runtime. Three things about that
 * can go wrong silently.
 *
 * The Sound button reads "Sound" whether media is muted or not, and the glyph is
 * the whole of what says which. Its caption comes from QAction::iconText() by way
 * of QToolButton::setDefaultAction, while the action's text() - the wording in
 * the dropdown and the wording a screen reader reads - keeps switching between
 * "Mute all media" and "Unmute all media". Anything that writes the mute wording
 * onto the button puts it back to a caption that changes under the pointer.
 *
 * The seven editor concepts take the editor's own files, so a trigger is the same
 * picture wherever it is offered - checked pixel for pixel rather than by file
 * name, because the tint is half of what makes the pair match. Then the palette
 * is moved and it is read again: nothing re-derives a QIcon once it has been set
 * on an action, so a toolbar left out of the restyle sits in the old theme's ink
 * for the rest of the session.
 *
 * A detached profile window builds its own copy of the same bar from its own list
 * of actions, which is a separate place for the mapping to be wrong.
 *
 * Run with: ctest -R MainToolBarGlyphTest -V
 */

#include <QAction>
#include <QFileInfo>
#include <QImage>
#include <QPalette>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest/QtTest>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "ProfileTestHelper.h"
#include "TDetachedWindow.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class MainToolBarGlyphTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    QString mPort;
    const QString mLocalhost = qsl("localhost");
    const QString mFirstHostname = qsl("MainToolBarGlyph-First");
    const QString mSecondHostname = qsl("MainToolBarGlyph-Second");

    // The size the comparison is made at. Both pictures are held at the
    // resolution they were drawn at and scaled by whatever draws them, so any one
    // size settles it.
    static constexpr int scmComparisonSize = 24;

    // setupConfig() consults portable.txt before the XDG logic
    static bool portableMarkerPresent()
    {
        return QFileInfo::exists(qsl("%1/portable.txt").arg(QCoreApplication::applicationDirPath())) || QFileInfo::exists(qsl("%1/.config/mudlet/portable.txt").arg(QDir::homePath()));
    }

    static QImage drawnAt(const QIcon& icon, const QIcon::State state = QIcon::Off) { return icon.pixmap(QSize(scmComparisonSize, scmComparisonSize), QIcon::Normal, state).toImage(); }

    // Somewhere for a difference to be reported from: the colour the glyph is
    // actually inked in, read off the most opaque pixel of it
    static QString inkOf(const QImage& glyph)
    {
        QColor darkest;
        int strongest = -1;
        for (int y = 0; y < glyph.height(); ++y) {
            for (int x = 0; x < glyph.width(); ++x) {
                const QColor pixel = glyph.pixelColor(x, y);
                if (pixel.alpha() > strongest) {
                    strongest = pixel.alpha();
                    darkest = pixel;
                }
            }
        }
        return strongest <= 0 ? qsl("nothing at all") : qsl("%1 at alpha %2").arg(darkest.name(), QString::number(strongest));
    }

    // Two glyphs of the same ink differ by shape alone, which a colour cannot
    // report: say how much of the picture moved as well as what it is inked in
    static QString differenceBetween(const QImage& left, const QImage& right)
    {
        if (left.size() != right.size()) {
            return qsl("they are not even the same size");
        }
        int differing = 0;
        for (int y = 0; y < left.height(); ++y) {
            for (int x = 0; x < left.width(); ++x) {
                if (left.pixel(x, y) != right.pixel(x, y)) {
                    ++differing;
                }
            }
        }
        return qsl("%1 of %2 pixels differ").arg(QString::number(differing), QString::number(left.width() * left.height()));
    }

    // Reached by name rather than by member, which the toolbar's own tests do as
    // well: the action carries the name the toolbar button it becomes is given
    static QAction* mainWindowTriggersAction() { return mudlet::self()->findChild<QAction*>(qsl("triggers_action")); }

    // What the Triggers action ought to be carrying: the editor's own file, in
    // the quiet ink everything else on the bar is set in
    static QImage expectedTriggersGlyph()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        return drawnAt(QIcon(uiDesign::tintedGlyph(QPixmap(qsl(":/icons/editor-triggers.png")), tokens.mutedText)));
    }

    void startProfile(const QString& hostname)
    {
        auto host = TestProfile::create(hostname, mLocalhost, mPort);
        if (!host) {
            QFAIL("No active host available for the test.");
        }

        QSignalSpy connectionSpy(&(host->mTelnet), &cTelnet::signal_connected);
        if (!connectionSpy.wait(2000)) {
            QFAIL("Could not connect with the host.");
        }
    }

    void deleteProfileDirectory(const QString& profileName)
    {
        QDir dir(mudlet::getMudletPath(enums::profileHomePath, profileName));
        if (dir.exists()) {
            dir.removeRecursively();
        }
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
        // The captions below are read as words rather than as pictures
        mudlet::self()->setInterfaceLanguage(qsl("en_US"));
        mudlet::self()->setStorePasswordsSecurely(false);

        deleteProfileDirectory(mFirstHostname);
        deleteProfileDirectory(mSecondHostname);

        // Two of them, because slot_tabDetachRequested() refuses index 0
        startProfile(mFirstHostname);
        if (QTest::currentTestFailed()) {
            return;
        }
        startProfile(mSecondHostname);
    }

    void cleanupTestCase()
    {
        delete mpServer;
        mpServer = nullptr;
        if (mudlet::self()) {
            deleteProfileDirectory(mFirstHostname);
            deleteProfileDirectory(mSecondHostname);
            delete mudlet::self();
        }
        mSavedXdg.isNull() ? qunsetenv("XDG_CONFIG_HOME") : qputenv("XDG_CONFIG_HOME", mSavedXdg);
    }

    // The caption stands still and the picture moves - the whole of the relabel
    void test_theSoundButtonKeepsItsCaptionWhileItsGlyphSaysTheState()
    {
        QToolButton* pButton = mudlet::self()->findChild<QToolButton*>(qsl("mute"));
        QVERIFY2(pButton, "the main window has no 'mute' toolbar button");
        QAction* pAction = mudlet::self()->findChild<QAction*>(qsl("muteMedia"));
        QVERIFY2(pAction, "the main window has no 'muteMedia' action");

        const bool startedMuted = mudlet::self()->mediaMuted();
        const QString captionUnmuted = pButton->text();
        const QString wordingUnmuted = pAction->text();
        const QImage glyphUnmuted = drawnAt(pButton->icon(), pButton->isChecked() ? QIcon::On : QIcon::Off);

        mudlet::self()->slot_muteMedia();

        const QString captionMuted = pButton->text();
        const QString wordingMuted = pAction->text();
        const QImage glyphMuted = drawnAt(pButton->icon(), pButton->isChecked() ? QIcon::On : QIcon::Off);
        const bool endedMuted = mudlet::self()->mediaMuted();

        // Back the way it was found, whichever way round that was
        mudlet::self()->slot_muteMedia();

        qInfo().noquote() << qsl("  caption \"%1\" -> \"%2\"; the action says \"%3\" -> \"%4\"").arg(captionUnmuted, captionMuted, wordingUnmuted, wordingMuted);
        qInfo().noquote() << qsl("  the glyph was %1 and is now %2").arg(inkOf(glyphUnmuted), inkOf(glyphMuted));

        QVERIFY2(!startedMuted && endedMuted, "slot_muteMedia() did not take the profiles from unmuted to muted, so nothing below was measured across the change");
        QVERIFY2(captionUnmuted == qsl("Sound"), qPrintable(qsl("the Sound button reads \"%1\" while media is unmuted").arg(captionUnmuted)));
        QVERIFY2(captionMuted == qsl("Sound"), qPrintable(qsl("the Sound button reads \"%1\" once media is muted - something is writing the action's wording onto the caption").arg(captionMuted)));
        QCOMPARE(wordingUnmuted, qsl("Mute all media"));
        QCOMPARE(wordingMuted, qsl("Unmute all media"));
        QVERIFY2(!glyphUnmuted.isNull() && !glyphMuted.isNull(), "the Sound button has no picture in one of the two states");
        QVERIFY2(glyphUnmuted != glyphMuted,
                 qPrintable(qsl("muting left the Sound button's glyph exactly as it was, at %1 - the caption no longer says which state it is in, so the picture has to").arg(inkOf(glyphMuted))));

        // ...and it is the two pictures the design asks for rather than merely two
        // different ones: a change of ink alone would pass the line above while
        // the speaker kept its waves through both states
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QImage expectedUnmuted = drawnAt(QIcon(uiDesign::tintedGlyph(QPixmap(qsl(":/icons/toolbar-sound-on.png")), tokens.mutedText)));
        const QImage expectedMuted = drawnAt(QIcon(uiDesign::tintedGlyph(QPixmap(qsl(":/icons/toolbar-sound-off.png")), tokens.accentText)));
        QVERIFY2(glyphUnmuted == expectedUnmuted,
                 qPrintable(qsl("the unmuted Sound button is not the speaker-with-waves glyph in the quiet ink: %1").arg(differenceBetween(glyphUnmuted, expectedUnmuted))));
        QVERIFY2(glyphMuted == expectedMuted, qPrintable(qsl("the muted Sound button is not the speaker-with-x glyph in the accent: %1").arg(differenceBetween(glyphMuted, expectedMuted))));
    }

    // One file, one ink, whichever of the two windows the concept is offered in
    void test_theTriggersButtonCarriesTheEditorsGlyph()
    {
        QAction* pAction = mainWindowTriggersAction();
        QVERIFY2(pAction, "the main window has no 'triggers_action' on its toolbar");
        const QImage onTheToolbar = drawnAt(pAction->icon());
        const QImage expected = expectedTriggersGlyph();
        qInfo().noquote() << qsl("  the Triggers button is inked %1 against an expected %2").arg(inkOf(onTheToolbar), inkOf(expected));

        QVERIFY2(!onTheToolbar.isNull(), "the Triggers toolbar action has no picture at all");
        QVERIFY2(onTheToolbar == expected,
                 qPrintable(qsl("the Triggers button is not the editor's glyph in the toolbar's ink: it is %1 where the editor's file tinted to mutedText is %2, and %3")
                                    .arg(inkOf(onTheToolbar), inkOf(expected), differenceBetween(onTheToolbar, expected))));
    }

    // ...and it stays that way when the theme moves. A QIcon set on an action is
    // re-derived by nothing.
    void test_aPaletteChangeRetintsTheToolbar()
    {
        QAction* pAction = mainWindowTriggersAction();
        QVERIFY2(pAction, "the main window has no 'triggers_action' on its toolbar");
        const QPalette savedPalette = QApplication::palette();
        const QImage before = drawnAt(pAction->icon());

        QPalette movedPalette(savedPalette);
        const bool wasDark = savedPalette.color(QPalette::Window).lightness() < 128;
        movedPalette.setColor(QPalette::Window, wasDark ? QColor(0xec, 0xec, 0xec) : QColor(0x2c, 0x2c, 0x2e));
        movedPalette.setColor(QPalette::WindowText, wasDark ? QColor(Qt::black) : QColor(Qt::white));
        QApplication::setPalette(movedPalette);
        QTest::qWait(100ms);

        const QImage after = drawnAt(pAction->icon());
        const QImage expectedAfter = expectedTriggersGlyph();
        qInfo().noquote() << qsl("  the Triggers button was inked %1 and is now %2").arg(inkOf(before), inkOf(after));

        QApplication::setPalette(savedPalette);
        QTest::qWait(100ms);

        // Read before the pair is compared: a restyle that did nothing at all
        // would leave the two agreeing and say nothing
        QVERIFY2(before != after, qPrintable(qsl("the palette moved and the toolbar's picture did not: it is still inked %1").arg(inkOf(after))));
        QVERIFY2(after == expectedAfter,
                 qPrintable(qsl("the toolbar was re-inked to %1 where the new palette's mutedText is %2, and %3").arg(inkOf(after), inkOf(expectedAfter), differenceBetween(after, expectedAfter))));
    }

    // The detached window builds its own bar from its own actions
    void test_aDetachedWindowCarriesTheSameTriggersGlyph()
    {
        QVERIFY2(mudlet::self()->getDetachedWindows().isEmpty(), "a detached window was left over from an earlier test");
        mudlet::self()->slot_tabDetachRequested(1, QPoint(200, 200));
        TDetachedWindow* pDetachedWindow = mudlet::self()->getDetachedWindows().value(mSecondHostname);
        QVERIFY2(pDetachedWindow, qPrintable(qsl("detaching tab 1 produced no window for '%1' - the tab order is not what this test assumes").arg(mSecondHostname)));

        QAction* pDetachedAction = pDetachedWindow->findChild<QAction*>(qsl("triggers_action"));
        QVERIFY2(pDetachedAction, "the detached window has no 'triggers_action' on its toolbar");
        const QImage inTheDetachedWindow = drawnAt(pDetachedAction->icon());
        const QImage inTheMainWindow = drawnAt(mainWindowTriggersAction()->icon());
        qInfo().noquote() << qsl("  the detached window's Triggers button is inked %1 against the main window's %2").arg(inkOf(inTheDetachedWindow), inkOf(inTheMainWindow));

        mudlet::self()->slot_tabReattachRequested(mSecondHostname);

        QVERIFY2(!inTheDetachedWindow.isNull(), "the detached window's Triggers action has no picture at all");
        QVERIFY2(inTheDetachedWindow == inTheMainWindow,
                 qPrintable(qsl("a detached window's Triggers button is not the main window's picture: it is %1 where the main window's is %2, and %3")
                                    .arg(inkOf(inTheDetachedWindow), inkOf(inTheMainWindow), differenceBetween(inTheDetachedWindow, inTheMainWindow))));
    }
};

#include "MainToolBarGlyphTest.moc"
MUDLET_GROUPED_TEST_MAIN(MainToolBarGlyphTest)
