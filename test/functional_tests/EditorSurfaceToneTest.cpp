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
 * The column an item is edited in and the panel of items beside it are one
 * surface, and the page tone is what both are drawn in.
 *
 * The form was reading as a different surface from the panel, and nothing was
 * painting it that colour - nothing was painting it at all. The trees, the
 * toolbar, the status bar and the sidebar all name the page tone; everything
 * from the frame an item is edited in down to the seven forms inside it is
 * transparent, and fell back on QPalette::Window. Those two agree only until a
 * palette answers the same colour to Window and to Base - macOS in light
 * appearance among them - where themeTokens() steps the page down under the
 * card and the edit column is left on the colour the page stepped away from.
 *
 * Measured either side of the fix: the panel is painted the page tone and the
 * form was not painted, so the cases below compare the two rather than assert a
 * colour on one of them.
 *
 * Run with: ctest -R EditorSurfaceToneTest -V
 */

#include <QLineEdit>
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
#include "TelnetServerStub.h"
#include "dlgTriggerEditor.h"
#include "dlgTriggersMainArea.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorSurfaceToneTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorSurfaceTone-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

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

    // Off a shot of the whole window rather than of the widget alone. What is
    // being measured here is a surface a widget shows through rather than one it
    // paints, and grab() renders only the widget it is called on and its
    // children - a transparent one comes back as an unpainted pixmap.
    QColor pixelOfWindowUnder(QWidget* pWidget, const QPoint& pointInWidget) const
    {
        const QPixmap shot = mpEditor->grab();
        return shot.toImage().pixelColor(pWidget->mapTo(mpEditor, pointInWidget));
    }

    // Both surfaces are flat fills, so the nearer of the two is the one that
    // was painted
    static int distanceBetween(const QColor& one, const QColor& other) { return std::abs(one.red() - other.red()) + std::abs(one.green() - other.green()) + std::abs(one.blue() - other.blue()); }

    static QString describe(const QColor& measured, const uiDesign::ThemeTokens& tokens)
    {
        return qsl("%1 - the page is %2, the card %3, the field %4").arg(measured.name(), tokens.page.name(), tokens.card.name(), tokens.field.name());
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
        mPort = QString::number(mpServer->serverPort());
        mudlet::start();
        mudlet::self()->setupConfig();
        QCOMPARE(mudlet::getMudletPath(enums::mainPath), qsl("%1/mudlet").arg(mConfigDir.path()));
        mudlet::self()->takeOwnershipOfInstanceCoordinator(std::make_unique<MudletInstanceCoordinator>(qsl("MudletInstanceCoordinator")));
        mudlet::self()->init();
        mudlet::self()->setStorePasswordsSecurely(false);
        deleteProfileDirectory(mProfileName);

        startProfile(mProfileName, mLocalhost, mPort);

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mpEditor->resize(1000, 800);

        // A trigger to edit, so the form is the one a user looks at
        mpEditor->slot_showTriggers();
        mpEditor->addTrigger(false);
        QTest::qWait(100ms);
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

    // The empty room under the last pattern row, which is what a form with one
    // pattern in it is mostly made of
    void test_theFormBehindThePatternsIsThePageTone()
    {
        auto* pPatternList = mpEditor->findChild<QWidget*>(qsl("editorPatternList"));
        QVERIFY2(pPatternList, "the widget the pattern rows are held in has gone, or been renamed");
        QVERIFY2(pPatternList->height() > 20 && pPatternList->width() > 20, "the pattern list is too small to read a pixel out of");

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QColor measured = pixelOfWindowUnder(pPatternList, QPoint(pPatternList->width() - 3, pPatternList->height() - 3));

        QVERIFY2(distanceBetween(measured, tokens.page) < distanceBetween(measured, tokens.field),
                 qPrintable(qsl("the form behind the patterns is painted %1 - it is nearer the field colour than the page, which is the sunken box the edit column used to read as")
                                    .arg(describe(measured, tokens))));
        QCOMPARE(measured.rgb(), tokens.page.rgb());
    }

    // ...and the panel of items beside it, so the two halves of the window are
    // provably one surface rather than two that happen to agree
    void test_theItemPanelIsPaintedTheSameToneAsTheForm()
    {
        auto* pPatternList = mpEditor->findChild<QWidget*>(qsl("editorPatternList"));
        QVERIFY(pPatternList);
        QWidget* pTreeViewport = mpEditor->treeWidget_triggers->viewport();
        QVERIFY2(pTreeViewport->height() > 20, "the trigger tree is too small to read a pixel out of");

        const QColor form = pixelOfWindowUnder(pPatternList, QPoint(pPatternList->width() - 3, pPatternList->height() - 3));
        const QColor panel = pixelOfWindowUnder(pTreeViewport, QPoint(pTreeViewport->width() - 3, pTreeViewport->height() - 3));

        QVERIFY2(form.rgb() == panel.rgb(), qPrintable(qsl("the form is painted %1 and the panel beside it %2").arg(form.name(), panel.name())));
    }

    // The one thing on the form that is still a field is the one typed into
    void test_theNameFieldKeepsTheFieldTone()
    {
        QLineEdit* pName = mpEditor->mpTriggersMainArea->lineEdit_trigger_name;
        QVERIFY2(pName->height() > 8 && pName->width() > 40, "the trigger name field is too small to read a pixel out of");

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QColor measured = pixelOfWindowUnder(pName, QPoint(pName->width() / 2, pName->height() / 2));

        QVERIFY2(distanceBetween(measured, tokens.field) < distanceBetween(measured, tokens.page),
                 qPrintable(qsl("the trigger name field is painted %1 - it has lost the sunken colour a field is filled with").arg(describe(measured, tokens))));
    }

    // The page tone is painted on the shell, which is Mudlet's own scaffolding
    // and nothing a profile's Lua stylesheet is offered. Named outright, so that
    // it draws that one widget and lets everything under it show through rather
    // than painting over what draws it.
    void test_theShellRuleNamesNothingButTheShell()
    {
        QWidget* pShell = mpEditor->findChild<QWidget*>(qsl("editorShell"));
        QVERIFY2(pShell, "the editor shell has gone, or been renamed");
        const QString sheet = pShell->styleSheet();

        QVERIFY2(sheet.contains(qsl("#editorShell {")), qPrintable(qsl("the shell is not painted at all, its stylesheet is \"%1\"").arg(sheet)));
        QVERIFY2(sheet.count(QLatin1Char('{')) == 1, qPrintable(qsl("the shell's stylesheet carries more than the one rule that paints it: \"%1\"").arg(sheet)));
        QVERIFY2(sheet.contains(uiDesign::themeTokens().page.name()), qPrintable(qsl("the shell is painted something other than the page tone: \"%1\"").arg(sheet)));
    }
};

#include "EditorSurfaceToneTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorSurfaceToneTest)
