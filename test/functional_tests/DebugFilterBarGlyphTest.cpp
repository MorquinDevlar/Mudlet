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
 * The Central Debug Console's filter bar draws the same monochrome glyphs the
 * main window and the editor do, inked from the palette at runtime. Three things
 * about that can go wrong silently.
 *
 * Pause is a checkable action whose two states are two different pictures, and
 * the picture is now the whole of what carries the state through a QIcon's On
 * slot rather than a setIcon() call inside the toggle slot. An icon built from
 * one file for both states leaves the button showing "pause" while the console
 * is already paused, and nothing about the bar looks wrong.
 *
 * The Show button is a QToolButton on the bar rather than an action, so it is
 * outside restyleActionGlyphs() and has to be inked by hand. It also takes the
 * editor tree's own funnel, so that a filter is the same picture wherever it is
 * offered - checked pixel for pixel rather than by file name, because the tint
 * is half of what makes the pair match.
 *
 * Then the palette is moved and the bar is read again: nothing re-derives a
 * QIcon once it has been set, so a bar left out of the restyle sits in the old
 * theme's ink for the rest of the session.
 *
 * Run with: ctest -R DebugFilterBarGlyphTest -V
 */

#include <QAction>
#include <QImage>
#include <QPalette>
#include <QToolButton>
#include <QtTest/QtTest>

#include "GroupedTest.h"
#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "TDebug.h"
#include "TDebugFilterBar.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgConnectionProfiles.h"
#include "mudlet.h"
#include "uiDesign.h"

using namespace std::chrono_literals;

class DebugFilterBarGlyphTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    const QString mHostname = qsl("Test-DebugFilterBarGlyph");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    // The size the comparison is made at. Both pictures are held at the
    // resolution they were drawn at and scaled by whatever draws them, so any one
    // size settles it.
    static constexpr int scmComparisonSize = 24;

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

    static QImage expectedGlyph(const QString& file, const QColor& ink) { return drawnAt(QIcon(uiDesign::tintedGlyph(uiDesign::glyphPixmap(file), ink))); }

    static QImage expectedPauseGlyph() { return expectedGlyph(qsl(":/icons/debug-pause.svg"), uiDesign::themeTokens().mutedText); }

    // The bar's actions carry no object name, so they are reached the way the
    // user reads them - which is why the interface language is pinned in init()
    static QAction* barAction(const QString& text)
    {
        if (!mudlet::smpDebugFilterBar) {
            return nullptr;
        }
        for (auto* pAction : mudlet::smpDebugFilterBar->findChildren<QAction*>()) {
            if (pAction->text() == text) {
                return pAction;
            }
        }
        return nullptr;
    }

    static QToolButton* categoryButton()
    {
        if (!mudlet::smpDebugFilterBar) {
            return nullptr;
        }
        for (auto* pButton : mudlet::smpDebugFilterBar->findChildren<QToolButton*>()) {
            if (pButton->text() == qsl("Show")) {
                return pButton;
            }
        }
        return nullptr;
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
    }

    void cleanupTestCase() { mSavedXdg.isNull() ? qunsetenv("XDG_CONFIG_HOME") : qputenv("XDG_CONFIG_HOME", mSavedXdg); }

    void init()
    {
        mpServer = new TelnetServerStub(qApp);
        mpServer->start(mLocalhost, 0);
        QVERIFY2(mpServer->isListening(), "TelnetServerStub failed to bind a loopback port");
        mPort = QString::number(mpServer->serverPort());
        mudlet::start();
        mudlet::self()->setupConfig();
        QCOMPARE(mudlet::getMudletPath(enums::mainPath), qsl("%1/mudlet").arg(mConfigDir.path()));
        mudlet::self()->takeOwnershipOfInstanceCoordinator(std::make_unique<MudletInstanceCoordinator>(qsl("MudletInstanceCoordinator")));
        mudlet::self()->init();
        // The buttons below are found by the words on them
        mudlet::self()->setInterfaceLanguage(qsl("en_US"));
        mudlet::self()->setStorePasswordsSecurely(false);
        deleteProfileDirectory(mHostname);
    }

    void cleanup()
    {
        TDebug::setPaused(false);
        TDebug::discardPausedMessages();
        mudlet::smDebugMode = false;

        delete mpServer;
        mpServer = nullptr;
        deleteProfileDirectory(mHostname);
        delete mudlet::self();
    }

    // The word changes too, but the picture is what is read at a glance - and it
    // now comes out of the icon's On slot rather than from the toggle slot
    void test_thePauseButtonCarriesThePauseGlyphAndOffersToResumeOnceChecked()
    {
        startDebuggingProfile();

        QAction* pAction = barAction(qsl("Pause"));
        QVERIFY2(pAction, "the debug filter bar has no action reading 'Pause'");

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QImage paused = drawnAt(pAction->icon(), QIcon::Off);
        const QImage resuming = drawnAt(pAction->icon(), QIcon::On);
        const QImage expectedPaused = expectedGlyph(qsl(":/icons/debug-pause.svg"), tokens.mutedText);
        const QImage expectedResuming = expectedGlyph(qsl(":/icons/debug-resume.svg"), tokens.accentText);
        qInfo().noquote() << qsl("  the Pause button is inked %1 unchecked and %2 checked").arg(inkOf(paused), inkOf(resuming));

        QVERIFY2(!paused.isNull(), "the Pause action has no picture at all");
        QVERIFY2(paused == expectedPaused, qPrintable(qsl("the Pause button is not the pause glyph in the quiet ink: %1").arg(differenceBetween(paused, expectedPaused))));
        QVERIFY2(resuming == expectedResuming, qPrintable(qsl("a checked Pause button is not the play glyph in the accent: %1").arg(differenceBetween(resuming, expectedResuming))));

        pAction->setChecked(true);
        const QImage whileHolding = drawnAt(pAction->icon(), QIcon::On);
        const QString wordingWhileHolding = pAction->text();
        pAction->setChecked(false);

        QVERIFY2(whileHolding != paused,
                 qPrintable(qsl("pausing left the button showing the same picture it offered to pause with, at %1 - nothing says the console is holding messages back").arg(inkOf(whileHolding))));
        QCOMPARE(wordingWhileHolding, qsl("Resume"));
    }

    void test_theClearButtonCarriesTheEraserGlyph()
    {
        startDebuggingProfile();

        QAction* pAction = barAction(qsl("Clear"));
        QVERIFY2(pAction, "the debug filter bar has no action reading 'Clear'");

        const QImage onTheBar = drawnAt(pAction->icon());
        const QImage expected = expectedGlyph(qsl(":/icons/debug-clear.svg"), uiDesign::themeTokens().mutedText);
        qInfo().noquote() << qsl("  the Clear button is inked %1 against an expected %2").arg(inkOf(onTheBar), inkOf(expected));

        QVERIFY2(!onTheBar.isNull(), "the Clear action has no picture at all");
        QVERIFY2(onTheBar == expected, qPrintable(qsl("the Clear button is not the eraser glyph in the toolbar's ink: %1").arg(differenceBetween(onTheBar, expected))));
    }

    // One file, one ink, whichever window the concept is offered in - the editor
    // tree marks its filter with the same funnel
    void test_theShowButtonCarriesTheEditorsFilterGlyph()
    {
        startDebuggingProfile();

        QToolButton* pButton = categoryButton();
        QVERIFY2(pButton, "the debug filter bar has no tool button reading 'Show'");

        const QImage onTheBar = drawnAt(pButton->icon());
        const QImage expected = expectedGlyph(qsl(":/icons/editor-filter.svg"), uiDesign::themeTokens().mutedText);
        qInfo().noquote() << qsl("  the Show button is inked %1 against an expected %2").arg(inkOf(onTheBar), inkOf(expected));

        QVERIFY2(!onTheBar.isNull(), "the Show button has no picture at all");
        QVERIFY2(onTheBar == expected, qPrintable(qsl("the Show button is not the editor's funnel in the toolbar's ink: %1").arg(differenceBetween(onTheBar, expected))));
    }

    // A QIcon set on an action is re-derived by nothing
    void test_aPaletteChangeRetintsTheBar()
    {
        startDebuggingProfile();

        QAction* pAction = barAction(qsl("Pause"));
        QVERIFY2(pAction, "the debug filter bar has no action reading 'Pause'");
        const QPalette savedPalette = QApplication::palette();
        const QImage before = drawnAt(pAction->icon());

        QPalette movedPalette(savedPalette);
        const bool wasDark = savedPalette.color(QPalette::Window).lightness() < 128;
        movedPalette.setColor(QPalette::Window, wasDark ? QColor(0xec, 0xec, 0xec) : QColor(0x2c, 0x2c, 0x2e));
        movedPalette.setColor(QPalette::WindowText, wasDark ? QColor(Qt::black) : QColor(Qt::white));
        QApplication::setPalette(movedPalette);
        QTest::qWait(100ms);

        const QImage after = drawnAt(pAction->icon());
        const QImage expectedAfter = expectedPauseGlyph();
        qInfo().noquote() << qsl("  the Pause button was inked %1 and is now %2").arg(inkOf(before), inkOf(after));

        QApplication::setPalette(savedPalette);
        QTest::qWait(100ms);

        // Read before the pair is compared: a restyle that did nothing at all
        // would leave the two agreeing and say nothing
        QVERIFY2(before != after, qPrintable(qsl("the palette moved and the bar's picture did not: it is still inked %1").arg(inkOf(after))));
        QVERIFY2(after == expectedAfter,
                 qPrintable(qsl("the bar was re-inked to %1 where the new palette's mutedText is %2, and %3").arg(inkOf(after), inkOf(expectedAfter), differenceBetween(after, expectedAfter))));
    }

private:
    // Starts a profile and brings the Central Debug Console - and with it the
    // filter bar - into existence, the way toggling Debug in the editor does.
    Host* startDebuggingProfile()
    {
        startProfile(mHostname, mLocalhost, mPort);
        auto* host = mudlet::self()->getActiveHost();

        mudlet::self()->attachDebugArea(host->getName());
        mudlet::smDebugMode = true;
        TDebug::flushMessageQueue();
        return host;
    }

    void deleteProfileDirectory(const QString& profileName)
    {
        QDir dir(mudlet::getMudletPath(enums::profileHomePath, profileName));
        if (!dir.exists()) {
            return;
        }
        dir.removeRecursively();
    }

    // Starts a profile by driving the connection dialog, as a user would.
    void startProfile(const QString& hostname, const QString& address, const QString& port)
    {
        QTimer::singleShot(0, qApp, [hostname, address, port]() {
            const auto dialog = []() {
                return mudlet::self()->mpConnectionDialog.data();
            };

            mudlet::self()->startAutoLogin({});

            if (!QTest::qWaitFor(
                        [&dialog]() {
                            return dialog() && dialog()->isVisible();
                        },
                        5000)) {
                qWarning() << "the connection dialog never appeared";
                return;
            }

            // Focus is what each step hands to the next, so the field about to
            // be typed into is the real precondition
            const auto waitForFocus = [](QWidget* field, const char* name) {
                if (QTest::qWaitFor(
                            [field]() {
                                return QApplication::focusWidget() == field;
                            },
                            5000)) {
                    return true;
                }
                qWarning() << "focus never reached the" << name << "field";
                return false;
            };

            QTest::mouseClick(dialog()->new_profile_button, Qt::LeftButton);
            if (!waitForFocus(dialog()->profile_name_entry, "profile name")) {
                return;
            }
            QTest::keyClicks(dialog()->profile_name_entry, hostname);
            QTest::keyClick(dialog()->profile_name_entry, Qt::Key_Tab);

            if (!waitForFocus(dialog()->host_name_entry, "server address")) {
                return;
            }
            QTest::keyClicks(dialog()->host_name_entry, address);
            QTest::keyClick(dialog()->host_name_entry, Qt::Key_Tab);

            if (!waitForFocus(dialog()->port_entry, "port")) {
                return;
            }
            QTest::keyClicks(dialog()->port_entry, port);
            QTest::keyClick(dialog()->port_entry, Qt::Key_Return);
        });

        QSignalSpy spy(mudlet::self(), &mudlet::signal_profileLoaded);
        if (!spy.wait(5000)) {
            QFAIL("Profile took too long to load.");
        }
        auto host = mudlet::self()->getActiveHost();
        if (!host) {
            QFAIL("No active host available for the test.");
        }

        QSignalSpy spy2(&(host->mTelnet), &cTelnet::signal_connected);
        if (!spy2.wait(2000)) {
            QFAIL("Could not connect with the host.");
        }
    }
};

#include "DebugFilterBarGlyphTest.moc"
MUDLET_GROUPED_TEST_MAIN(DebugFilterBarGlyphTest)
