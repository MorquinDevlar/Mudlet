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
 * A row whose item will not compile is marked in red, not in the grey every
 * other mark is drawn in.
 *
 * Every mark on an editor tree row used to be tinted mutedText, the errors
 * glyph included - so the one mark that reports a state rather than labelling a
 * kind was the same colour as the folder above it, and a broken script had to
 * be found by reading its glyph rather than by seeing it. It is now inked with
 * uiDesign::errorInk(), which is the very value the compile note over the code
 * pane is written in.
 *
 * The two cases below read it off a grab of the tree rather than off the
 * delegate's own colours: what the reader sees is the mark as the style
 * actually painted it, on the surface the rows sit on, in both appearances -
 * and a red that cannot be read on that surface is no better than a grey one.
 *
 * Run with: ctest -R EditorTreeErrorMarkTest -V
 */

#include <QImage>
#include <QStyleOptionViewItem>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest/QtTest>
#include <chrono>

#include "EditorTreeDelegate.h"
#include "EditorTreeRowMetrics.h"
#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TScript.h"
#include "TTreeWidget.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorTreeErrorMarkTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorTreeErrorMark-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    int mBrokenScriptId = 0;
    int mScriptFolderId = 0;

    // How far a reading may be from the colour it should be and still be that
    // colour, summed over the three channels. A glyph is drawn through a
    // painter at the screen's own ratio and composed onto the row, so its most
    // opaque pixel is near the ink rather than exactly it.
    static constexpr int scmInkSlack = 30;

    void deleteProfileDirectory(const QString& profileName)
    {
        QDir dir(mudlet::getMudletPath(enums::profileHomePath, profileName));
        if (dir.exists()) {
            dir.removeRecursively();
        }
    }

    // A folder with two scripts in it: one that compiles and one that does not,
    // so the red is read beside the grey it has to be told apart from
    void buildTheProfilesItems()
    {
        auto* pFolder = new TScript(qsl("A group of scripts"), mpHost);
        pFolder->setIsFolder(true);
        pFolder->setIsActive(true);
        pFolder->registerScript();
        mScriptFolderId = pFolder->getID();

        auto* pWorking = new TScript(pFolder, mpHost);
        pWorking->setName(qsl("A working script"));
        pWorking->setScript(qsl("-- nothing to do\n"));
        pWorking->setIsActive(true);
        pWorking->registerScript();

        auto* pBroken = new TScript(pFolder, mpHost);
        pBroken->setName(qsl("A broken script"));
        // An unclosed parenthesis and words that are not Lua: nothing the
        // interpreter can compile, which is the whole of what leaves the item
        // in the state the errors glyph stands for
        pBroken->setScript(qsl("this is not lua ((\n"));
        pBroken->registerScript();
        mBrokenScriptId = pBroken->getID();
    }

    static QTreeWidgetItem* rowFor(QTreeWidgetItem* pParent, const int id)
    {
        for (int row = 0, rows = pParent->childCount(); row < rows; ++row) {
            QTreeWidgetItem* pChild = pParent->child(row);
            if (pChild->data(0, Qt::UserRole).toInt() == id) {
                return pChild;
            }
            if (QTreeWidgetItem* pFound = rowFor(pChild, id)) {
                return pFound;
            }
        }
        return nullptr;
    }

    // Where the row's mark is painted, in the viewport's coordinates. The style
    // is asked where the row's picture lands rather than the number being
    // worked out here - the trees carry a stylesheet, and its padding is
    // nothing this could guess at - and the mark is the last square of that
    // picture, after the room the row's depth holds it in and the state dot.
    QRect markRectOf(TTreeWidget* pTree, QTreeWidgetItem* pItem) const
    {
        auto* pDelegate = qobject_cast<uiDesign::EditorTreeDelegate*>(pTree->itemDelegateForIndex(QModelIndex()));
        if (!pDelegate || !pItem) {
            return {};
        }
        const QRect rowRect = pTree->visualItemRect(pItem);
        if (rowRect.isEmpty()) {
            return {};
        }
        QStyleOptionViewItem option = pTree->viewItemOption();
        option.rect = rowRect;
        pDelegate->initStyleOption(&option, pTree->indexAt(rowRect.center()));
        const QRect decoration = pTree->style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &option, pTree);
        if (decoration.width() < uiDesign::scmTreeMarkSize) {
            return {};
        }
        return QRect(
                decoration.right() + 1 - uiDesign::scmTreeMarkSize, decoration.top() + (decoration.height() - uiDesign::scmTreeMarkSize) / 2, uiDesign::scmTreeMarkSize, uiDesign::scmTreeMarkSize);
    }

    static int distance(const QColor& one, const QColor& other) { return std::abs(one.red() - other.red()) + std::abs(one.green() - other.green()) + std::abs(one.blue() - other.blue()); }

    // The nearest any pixel of a square comes to a colour, and which pixel that
    // was - so a failure can say what the mark is actually drawn in
    static QPair<int, QColor> nearest(const QImage& shot, const QRect& square, const qreal ratio, const QColor& wanted)
    {
        int best = 3 * 255;
        QColor read;
        for (int y = square.top(); y <= square.bottom(); ++y) {
            for (int x = square.left(); x <= square.right(); ++x) {
                const QPoint inShot(qRound(x * ratio), qRound(y * ratio));
                if (!shot.rect().contains(inShot)) {
                    continue;
                }
                const QColor sample = shot.pixelColor(inShot);
                const int away = distance(sample, wanted);
                if (away < best) {
                    best = away;
                    read = sample;
                }
            }
        }
        return {best, read};
    }

    static QList<QPair<QString, enums::Appearance>> appearances() { return {{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}; }

    void showTheAppearance(const enums::Appearance appearance) const
    {
        mudlet::self()->setAppearance(appearance);
        QCoreApplication::processEvents();
        QTest::qWait(150ms);
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

        mpHost = TestProfile::create(mProfileName, mLocalhost, mPort);
        QVERIFY2(mpHost != nullptr, "No active host available for the test");
        QSignalSpy spy(&(mpHost->mTelnet), &cTelnet::signal_connected);
        QVERIFY2(spy.wait(2000), "Could not connect with the host");

        buildTheProfilesItems();

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mpEditor->resize(1000, 800);
        if (!QTest::qWaitForWindowExposed(mpEditor, 2000)) {
            qInfo().noquote() << qsl("  the editor window was never exposed; the readings below are off a rendered widget tree alone");
        }

        // The editor is built with the profile, so its trees were filled before
        // the scripts above existed
        mpEditor->treeWidget_scripts->clear();
        mpEditor->fillout_form();
        mpEditor->slot_showScripts();
        QTest::qWait(50ms);
        mpEditor->treeWidget_scripts->expandAll();
        QTest::qWait(50ms);

        // The fixture is the whole of what these cases read: a script that
        // compiled after all would leave them measuring a folder's grey and
        // reporting it as a fault of the ink rather than of the fixture
        TScript* pBroken = mpHost->getScriptUnit()->getScript(mBrokenScriptId);
        QVERIFY2(pBroken && !pBroken->state(), "The script the fixture meant to leave broken compiles after all");
        QVERIFY2(rowFor(mpEditor->mpScriptsBaseItem, mBrokenScriptId) != nullptr, "The broken script has no row in the Scripts tree");
        QVERIFY2(rowFor(mpEditor->mpScriptsBaseItem, mScriptFolderId) != nullptr, "The folder holding it has no row in the Scripts tree");
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

    // The mark on a broken item's row is the red the note over the code pane is
    // written in, and the mark on the folder beside it is still the chrome's
    // grey - read off a grab of the tree in both appearances
    void test_aBrokenItemsMarkIsPaintedInTheErrorInk()
    {
        const auto appearanceBefore = mudlet::self()->mAppearance;
        auto restore = qScopeGuard([appearanceBefore]() {
            mudlet::self()->setAppearance(appearanceBefore);
        });

        TTreeWidget* pTree = mpEditor->treeWidget_scripts;
        QStringList misses;
        QStringList measured;
        for (const auto& appearance : appearances()) {
            showTheAppearance(appearance.second);
            const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
            const QColor red = uiDesign::errorInk(tokens);

            const QRect brokenMark = markRectOf(pTree, rowFor(mpEditor->mpScriptsBaseItem, mBrokenScriptId));
            const QRect folderMark = markRectOf(pTree, rowFor(mpEditor->mpScriptsBaseItem, mScriptFolderId));
            QVERIFY2(!brokenMark.isEmpty() && !folderMark.isEmpty(), qPrintable(qsl("%1: one of the two marks has no rectangle to sample").arg(appearance.first)));

            const QImage shot = pTree->viewport()->grab().toImage();
            const qreal ratio = shot.devicePixelRatio();
            const auto redOnTheBroken = nearest(shot, brokenMark, ratio, red);
            const auto greyOnTheBroken = nearest(shot, brokenMark, ratio, tokens.mutedText);
            const auto greyOnTheFolder = nearest(shot, folderMark, ratio, tokens.mutedText);
            const auto redOnTheFolder = nearest(shot, folderMark, ratio, red);
            measured
                    << qsl("%1: the broken row's mark comes within %2 of the red %3 (nearest pixel %4) and within %5 of the muted %6; the folder's comes within %7 of the muted tone and %8 of the red")
                               .arg(appearance.first, QString::number(redOnTheBroken.first), red.name(), redOnTheBroken.second.name())
                               .arg(greyOnTheBroken.first)
                               .arg(tokens.mutedText.name())
                               .arg(greyOnTheFolder.first)
                               .arg(redOnTheFolder.first);

            if (redOnTheBroken.first > scmInkSlack) {
                misses << qsl("%1: no pixel of the broken row's mark is the error ink %2 - the nearest is %3, %4 away")
                                  .arg(appearance.first, red.name(), redOnTheBroken.second.name(), QString::number(redOnTheBroken.first));
            }
            if (greyOnTheBroken.first <= scmInkSlack) {
                misses << qsl("%1: the broken row's mark holds a pixel within %2 of the chrome's %3, so it cannot be told from a folder's")
                                  .arg(appearance.first, QString::number(greyOnTheBroken.first), tokens.mutedText.name());
            }
            // ...and the reading only means anything while the folder beside
            // it is still grey. Which of the two tones it is nearer, rather
            // than how near: a 1.5px stroke scaled into a 16px square never
            // reaches the full ink at any one pixel, so an absolute floor here
            // would be measuring the glyph's antialiasing rather than its
            // colour.
            if (greyOnTheFolder.first >= redOnTheFolder.first) {
                misses << qsl("%1: the folder's mark is nearer the red %2 (%3 away) than the chrome's %4 (%5 away), so this case is reading the wrong squares")
                                  .arg(appearance.first, red.name(), QString::number(redOnTheFolder.first), tokens.mutedText.name(), QString::number(greyOnTheFolder.first));
            }
        }
        qInfo().noquote() << qsl("  %1").arg(measured.join(qsl("\n  ")));
        QVERIFY2(misses.isEmpty(), qPrintable(qsl("\n  %1").arg(misses.join(qsl("\n  ")))));
    }

    // A red nobody can see on the surface the rows sit on says no more than the
    // grey it replaced, so the ink is held to the floor a mark is held to
    void test_theErrorInkCanBeReadOnThePanelTheRowsSitOn()
    {
        const auto appearanceBefore = mudlet::self()->mAppearance;
        auto restore = qScopeGuard([appearanceBefore]() {
            mudlet::self()->setAppearance(appearanceBefore);
        });

        TTreeWidget* pTree = mpEditor->treeWidget_scripts;
        QStringList misses;
        QStringList measured;
        for (const auto& appearance : appearances()) {
            showTheAppearance(appearance.second);
            const QColor red = uiDesign::errorInk(uiDesign::themeTokens());

            // Off the rows rather than beside them: what the mark is drawn on
            // is the viewport's own surface, read where nothing is painted over
            // it
            const QImage shot = pTree->viewport()->grab().toImage();
            const qreal ratio = shot.devicePixelRatio();
            const QRect lastRow = pTree->visualItemRect(pTree->topLevelItem(0));
            const QPoint below(pTree->viewport()->width() / 2, pTree->viewport()->height() - qMax(4, lastRow.height() / 2));
            const QPoint inShot(qRound(below.x() * ratio), qRound(below.y() * ratio));
            QVERIFY2(shot.rect().contains(inShot), qPrintable(qsl("%1: the tree has no empty room in it to read the surface off").arg(appearance.first)));
            const QColor surface = shot.pixelColor(inShot);
            const qreal ratioAgainstSurface = uiDesign::contrastRatio(red, surface);
            measured << qsl("%1: the error ink %2 reads at %3:1 on the panel's %4").arg(appearance.first, red.name(), QString::number(ratioAgainstSurface, 'f', 2), surface.name());
            if (ratioAgainstSurface < uiDesign::scmQuietMinimumRatio) {
                misses << qsl("%1: the error ink %2 reads at only %3:1 against the %4 the rows sit on").arg(appearance.first, red.name(), QString::number(ratioAgainstSurface, 'f', 2), surface.name());
            }
        }
        qInfo().noquote() << qsl("  %1").arg(measured.join(qsl("\n  ")));
        QVERIFY2(misses.isEmpty(), qPrintable(qsl("\n  %1").arg(misses.join(qsl("\n  ")))));
    }
};

MUDLET_GROUPED_TEST_MAIN(EditorTreeErrorMarkTest)
#include "EditorTreeErrorMarkTest.moc"
