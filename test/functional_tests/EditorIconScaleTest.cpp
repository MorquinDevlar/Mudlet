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
 * The editor draws glyphs in two places that sit against each other - the
 * actions toolbar along the top and the sidebar down the left - and they were
 * sized by different rules. The sidebar took the design language's 18px; the
 * toolbar multiplied the "Icon size toolbars" preference by 8, which at that
 * preference's own default of 3 is 24px.
 *
 * Two costs. The pictures did not match, a third again as large on one bar as
 * on the other. And the toolbar's buttons are as wide as their glyph plus their
 * name, so the oversized default pushed the row past the window sooner and
 * posted actions into the overflow menu on windows with plenty of room.
 *
 * The preference is not the problem and is not dropped - it is there for anyone
 * who needs a bigger target. What changed is what its default maps to: six
 * pixels a step rather than eight, so the default lands on the sidebar's 18px
 * and the steps above it still scale up, with 4 giving the 24px the default
 * used to draw. Both bars follow it, so the editor has one icon scale.
 */

#include <QListWidget>
#include <QTemporaryDir>
#include <QToolBar>
#include <QtTest/QtTest>
#include <chrono>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TelnetServerStub.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorIconScaleTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorIconScale-Test-Profile");
    QString mPort; // assigned the stub's actual ephemeral port in initTestCase()
    const QString mLocalhost = qsl("localhost");

    // What the preference's default has to come out at: the size every other
    // glyph in the editor and in the settings dialog is drawn at
    static constexpr int scmDesignLanguageGlyphSize = 18;
    // ...and the value the preference holds until somebody changes it, set in
    // mudlet::readSettings()
    static constexpr int scmDefaultPreference = 3;
    // What that default used to draw, when the step was eight pixels
    static constexpr int scmOldDefaultGlyphSize = 24;

    // The sweep the overflow width is found by. The step is fine enough to tell
    // two icon sizes apart on a bar of a dozen buttons.
    static constexpr int scmSweepStep = 10;
    static constexpr int scmSweepCeiling = 3000;

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
    // visibility is the overflow itself rather than a proxy for it.
    QWidget* extensionButton() const { return mpEditor->toolBar->findChild<QWidget*>(qsl("qt_toolbar_ext_button")); }

    // The narrowest window that still shows every action on the bar. Swept
    // upwards from the narrowest the editor can be dragged to, so the figure is
    // one a user could actually arrive at.
    int narrowestWidthWithNoOverflow()
    {
        QWidget* pExtension = extensionButton();
        if (!pExtension) {
            return -1;
        }
        for (int width = mpEditor->minimumWidth(); width <= scmSweepCeiling; width += scmSweepStep) {
            mpEditor->resize(width, 700);
            QCoreApplication::sendPostedEvents();
            QTest::qWait(20ms);
            if (!pExtension->isVisible()) {
                return width;
            }
        }
        return -1;
    }

    // mudlet::setToolBarIconSize() drops a value equal to the one it holds, so
    // asking for the value already in force changes nothing - and a case that
    // has just sized the toolbar by hand needs the mapping applied again. The
    // step through a different value is what makes it run.
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
        mpEditor->resize(1200, 700);
        QTest::qWait(50ms);
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

    // The mapping itself: what a profile that has never touched the preference
    // gets is the size the rest of the window is drawn at
    void test_theDefaultPreferenceIsTheDesignLanguagesGlyph()
    {
        setPreference(scmDefaultPreference);
        qInfo().noquote() << qsl("  preference %1 -> toolbar %2px, sidebar %3px")
                                     .arg(QString::number(scmDefaultPreference),
                                          QString::number(mpEditor->toolBar->iconSize().width()),
                                          QString::number(mpEditor->mpListWidget_editorSidebar->iconSize().width()));

        QCOMPARE(mpEditor->toolBar->iconSize(), QSize(scmDesignLanguageGlyphSize, scmDesignLanguageGlyphSize));
    }

    // One scale for the window, whatever the preference says: the two bars sit
    // against each other, and glyphs of different sizes on them read as a
    // mistake at any setting
    void test_bothBarsFollowThePreferenceTogether()
    {
        for (const int value : {1, 2, 3, 4}) {
            setPreference(value);
            QVERIFY2(mpEditor->toolBar->iconSize() == mpEditor->mpListWidget_editorSidebar->iconSize(),
                     qPrintable(
                             qsl("At preference %1 the toolbar draws %2px and the sidebar %3px")
                                     .arg(QString::number(value), QString::number(mpEditor->toolBar->iconSize().width()), QString::number(mpEditor->mpListWidget_editorSidebar->iconSize().width()))));
        }
        // ...and the steps above the default still scale up for anyone who
        // needs a bigger target, 4 landing on the 24px the default used to draw
        setPreference(4);
        QCOMPARE(mpEditor->toolBar->iconSize(), QSize(24, 24));
    }

    // What the rebaselining was for, measured rather than argued. The bar it is
    // measured against is 24px outright - the size the old mapping drew at the
    // default - rather than a preference value, so the comparison says the same
    // thing whatever the mapping is.
    void test_theDefaultNeedsLessWidthBeforeActionsGoIntoTheOverflowMenu()
    {
        setPreference(scmDefaultPreference);
        mpEditor->toolBar->setIconSize(QSize(scmOldDefaultGlyphSize, scmOldDefaultGlyphSize));
        QTest::qWait(50ms);
        const int before = narrowestWidthWithNoOverflow();

        // ...against whatever the preference's default actually maps to, taken
        // through the preference rather than set here
        setPreference(scmDefaultPreference);
        const int after = narrowestWidthWithNoOverflow();

        qInfo().noquote() << qsl("  every action fits from %1px of window at the old default's %2px glyphs, and from %3px at the %4px it maps to now")
                                     .arg(QString::number(before), QString::number(scmOldDefaultGlyphSize), QString::number(after), QString::number(mpEditor->toolBar->iconSize().width()));
        QVERIFY2(before > 0 && after > 0, "The toolbar never stopped overflowing within the swept range, so there is nothing to compare");
        QVERIFY2(after < before,
                 qPrintable(qsl("The toolbar needs %1px of window at the preference's default before every action fits, against %2px at the size that default used to draw - no improvement")
                                    .arg(QString::number(after), QString::number(before))));
    }
};

#include "EditorIconScaleTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorIconScaleTest)
