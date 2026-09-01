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
 * The row at the top of each of the editor's trees stands for exactly what the
 * row beside it in the sidebar stands for - Triggers, Aliases, Scripts - so the
 * two carry the same picture.
 *
 * They did not. The sidebar was redrawn from the monochrome set the whole window
 * is tinted from, while the heading rows kept the bitmaps the trees were built
 * with a decade ago: a wizard's hat for triggers, a stopwatch for timers, a pair
 * of people for aliases. Sitting a couple of hundred pixels apart, that reads as
 * two different applications.
 *
 * Checked pixel for pixel rather than by file name, for all seven views, because
 * the glyph is tinted at runtime and the tint is half of what makes the pair
 * match: a heading drawn from the right file in the wrong colour is still wrong.
 * The palette is then moved and both are read again - a QIcon on a tree item is
 * re-derived by nothing, so the one thing that could go wrong here is a theme
 * change reaching the sidebar and leaving the headings behind.
 *
 * Run with: ctest -R EditorTreeHeadingIconTest -V
 */

#include <QImage>
#include <QListWidget>
#include <QPalette>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest/QtTest>
#include <chrono>

#include "EditorCommand.h"
#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorTreeHeadingIconTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorTreeHeadingIcon-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    // The size the comparison is made at. Both pictures are held at the
    // resolution they were drawn at and scaled by whatever draws them, so any
    // one size settles it - and this is the size the window actually draws
    // them at, which makes a failure something a reader could have seen.
    static constexpr int scmComparisonSize = 18;

    // Which sidebar row a heading is paired with. Kept on the row by
    // dlgTriggerEditor rather than looked up by position, so a row added to the
    // list later cannot quietly re-pair the seven.
    static constexpr int scmRole_editorSidebarView = Qt::UserRole + 1;

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

    QList<QPair<QString, QPair<QTreeWidgetItem*, EditorViewType>>> headings() const
    {
        return {{qsl("Triggers"), {mpEditor->mpTriggerBaseItem, EditorViewType::cmTriggerView}},
                {qsl("Aliases"), {mpEditor->mpAliasBaseItem, EditorViewType::cmAliasView}},
                {qsl("Scripts"), {mpEditor->mpScriptsBaseItem, EditorViewType::cmScriptView}},
                {qsl("Timers"), {mpEditor->mpTimerBaseItem, EditorViewType::cmTimerView}},
                {qsl("Keys"), {mpEditor->mpKeyBaseItem, EditorViewType::cmKeysView}},
                {qsl("Buttons"), {mpEditor->mpActionBaseItem, EditorViewType::cmActionView}},
                {qsl("Variables"), {mpEditor->mpVarBaseItem, EditorViewType::cmVarsView}}};
    }

    QListWidgetItem* sidebarRowFor(const EditorViewType view) const
    {
        QListWidget* pSidebar = mpEditor->mpListWidget_editorSidebar;
        for (int row = 0, rows = pSidebar ? pSidebar->count() : 0; row < rows; ++row) {
            QListWidgetItem* pItem = pSidebar->item(row);
            if (static_cast<EditorViewType>(pItem->data(scmRole_editorSidebarView).toInt()) == view) {
                return pItem;
            }
        }
        return nullptr;
    }

    static QImage drawnAt(const QIcon& icon, const int size) { return icon.pixmap(QSize(size, size)).toImage(); }

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
        // The variables tree is built the first time it is asked for, and its
        // heading row is the one of the seven that is made afresh every time
        mpEditor->slot_showVariables();
        QTest::qWait(100ms);
        mpEditor->slot_showTriggers();
        QTest::qWait(50ms);

        for (const auto& heading : headings()) {
            QVERIFY2(heading.second.first != nullptr, qPrintable(qsl("The %1 tree has no heading row to read").arg(heading.first)));
            QVERIFY2(sidebarRowFor(heading.second.second) != nullptr, qPrintable(qsl("The sidebar has no row for %1").arg(heading.first)));
        }
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

    // The whole point, for all seven: one picture, drawn from one file in one
    // colour, whichever of the two places it is shown in
    void test_everyHeadingCarriesItsSidebarRowsGlyph()
    {
        QStringList mismatched;
        QStringList measured;
        for (const auto& heading : headings()) {
            const QImage onTheHeading = drawnAt(heading.second.first->icon(0), scmComparisonSize);
            const QImage inTheSidebar = drawnAt(sidebarRowFor(heading.second.second)->icon(), scmComparisonSize);
            measured << qsl("%1 %2").arg(heading.first, inkOf(onTheHeading));
            if (onTheHeading.isNull() || inTheSidebar.isNull()) {
                mismatched << qsl("%1: one of the two has no picture at all").arg(heading.first);
                continue;
            }
            if (onTheHeading != inTheSidebar) {
                mismatched << qsl("%1: the heading is inked %2 and the sidebar row %3").arg(heading.first, inkOf(onTheHeading), inkOf(inTheSidebar));
            }
        }
        qInfo().noquote() << qsl("  headings at %1px: %2").arg(QString::number(scmComparisonSize), measured.join(qsl(", ")));

        QVERIFY2(mismatched.isEmpty(), qPrintable(qsl("a tree's heading row does not carry its sidebar row's glyph: %1").arg(mismatched.join(qsl("; ")))));
    }

    // ...and it stays one picture when the theme moves. Nothing re-derives a
    // QIcon once it has been set on a tree item, so a heading left out of the
    // restyle would sit in the old theme's ink for the rest of the session.
    void test_aThemeChangeRetintsTheHeadingsWithTheSidebar()
    {
        const QPalette savedPalette = QApplication::palette();
        const QImage before = drawnAt(mpEditor->mpTriggerBaseItem->icon(0), scmComparisonSize);

        QPalette movedPalette(savedPalette);
        movedPalette.setColor(QPalette::Window, savedPalette.color(QPalette::Window).lightness() < 128 ? QColor(0xec, 0xec, 0xec) : QColor(0x2c, 0x2c, 0x2e));
        movedPalette.setColor(QPalette::WindowText, savedPalette.color(QPalette::Window).lightness() < 128 ? QColor(Qt::black) : QColor(Qt::white));
        QApplication::setPalette(movedPalette);
        QTest::qWait(100ms);

        const QImage after = drawnAt(mpEditor->mpTriggerBaseItem->icon(0), scmComparisonSize);
        qInfo().noquote() << qsl("  the Triggers heading was inked %1 and is now %2").arg(inkOf(before), inkOf(after));

        QStringList mismatched;
        for (const auto& heading : headings()) {
            const QImage onTheHeading = drawnAt(heading.second.first->icon(0), scmComparisonSize);
            const QImage inTheSidebar = drawnAt(sidebarRowFor(heading.second.second)->icon(), scmComparisonSize);
            if (onTheHeading != inTheSidebar) {
                mismatched << qsl("%1: the heading is inked %2 and the sidebar row %3").arg(heading.first, inkOf(onTheHeading), inkOf(inTheSidebar));
            }
        }

        QApplication::setPalette(savedPalette);
        QTest::qWait(100ms);

        // Read before the pair is compared: a restyle that did nothing at all
        // would leave the two agreeing and say nothing
        QVERIFY2(before != after, qPrintable(qsl("the theme moved and the heading's picture did not: it is still inked %1").arg(inkOf(after))));
        QVERIFY2(mismatched.isEmpty(), qPrintable(qsl("a theme change reached the sidebar and left a heading behind: %1").arg(mismatched.join(qsl("; ")))));
    }
};

#include "EditorTreeHeadingIconTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorTreeHeadingIconTest)
