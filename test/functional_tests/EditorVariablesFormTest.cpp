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
 * The variables form, and the three things around it.
 *
 * The two type pickers were tucked into the right hand columns of the form's
 * grid behind words that pointed at them with arrows; they are one row under
 * the name now, with the switch that keeps the variable out of the tree on the
 * row under them. The switch over the tree starts on the line the search field
 * above it starts on. The caret readout in the status bar belongs to the code
 * pane, so it goes when the pane does. And the heading over that pane names a
 * value here rather than a Lua script.
 *
 * Run with: ctest -R EditorVariablesFormTest -V
 */

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest/QtTest>
#include <chrono>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TLuaInterpreter.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgSourceEditorArea.h"
#include "dlgTriggerEditor.h"
#include "dlgVarsMainArea.h"
#include "mudlet.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorVariablesFormTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    QTreeWidget* mpVariablesTree = nullptr;
    const QString mProfileName = qsl("EditorVariablesForm-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");
    // The two arrows the words leading the pickers used to point with, written
    // as code points rather than as themselves so that this file stays readable
    // wherever it is opened
    static constexpr char16_t scmLeftPointingArrow = 0x23f4;
    static constexpr char16_t scmDownPointingArrow = 0x23f7;

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

    // The one frame every control can be compared in: each is nested a
    // different number of layouts deep
    int leftEdgeOf(QWidget* pWidget) const { return pWidget->mapTo(mpEditor, QPoint(0, 0)).x(); }
    int topEdgeOf(QWidget* pWidget) const { return pWidget->mapTo(mpEditor, QPoint(0, 0)).y(); }

    dlgVarsMainArea* form() const { return mpEditor->mpVarsMainArea; }
    // What the code pane's heading reads out about the caret. The reading moved
    // off the window's status bar onto the strip over the pane, so it is empty
    // when that strip is away - which is what it is when the pane is.
    QString caretReadoutSays() const
    {
        QLabel* pCaret = mpEditor->findChild<QLabel*>(qsl("editorCodeCaret"));
        return (pCaret && pCaret->isVisible()) ? pCaret->text() : QString();
    }

    void openTheVariablesView()
    {
        mpEditor->slot_showVariables();
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
    }

    QTreeWidgetItem* rootRow() const { return mpVariablesTree->topLevelItem(0); }

    QTreeWidgetItem* variableRow(const QString& name) const
    {
        QTreeWidgetItem* pBase = rootRow();
        for (int i = 0; pBase && i < pBase->childCount(); ++i) {
            if (pBase->child(i)->text(0) == name) {
                return pBase->child(i);
            }
        }
        return nullptr;
    }

    // What clicking a row does. Both calls are deliberate: setting the current
    // item already reaches slot_variableSelected() through itemSelectionChanged,
    // and the explicit call stands in for the itemClicked on the mouse release.
    void selectVariable(QTreeWidgetItem* pItem)
    {
        mpVariablesTree->setCurrentItem(pItem);
        mpEditor->slot_variableSelected(pItem);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
    }

    // Returns false rather than asserting: QVERIFY expands to a bare return,
    // which would leave the caller working with nothing selected
    bool showTheStringVariable()
    {
        openTheVariablesView();
        QTreeWidgetItem* pRow = variableRow(qsl("myString"));
        if (!pRow) {
            return false;
        }
        selectVariable(pRow);
        return true;
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

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mpEditor->resize(1100, 900);
        QTest::qWait(50ms);
        mpVariablesTree = mpEditor->treeWidget_variables;
        QVERIFY2(mpVariablesTree != nullptr, "the editor has no variables tree");

        // Two variables of the profile's own to pick out of everything Lua
        // holds: one with a value the code pane can show, and one without
        QVERIFY2(mpHost->mLuaInterpreter.compileAndExecuteScript(qsl("myString = \"north\"; myTable = { a = 1 }")), "the two variables this walks could not be made");
        mpEditor->repopulateVars();
        QTest::qWait(50ms);
        QVERIFY2(showTheStringVariable(), "the Variables view did not show the string variable this walks");
    }

    void cleanupTestCase()
    {
        mpVariablesTree = nullptr;
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

    // The two readings are one row, in the place every other form puts the row
    // under its head row, and the switch is the row under them
    void test_theTwoTypePickersAreOneRowUnderTheName()
    {
        QVERIFY2(showTheStringVariable(), "the string variable is not showing");

        QComboBox* pKeyType = form()->comboBox_variable_key_type;
        QComboBox* pValueType = form()->comboBox_variable_value_type;
        QVERIFY2(pKeyType->isVisible() && pValueType->isVisible(), "the variables form is not showing both of its type pickers");
        QCOMPARE(pKeyType->parentWidget()->objectName(), qsl("editorVariableTypes"));
        QCOMPARE(pValueType->parentWidget()->objectName(), qsl("editorVariableTypes"));
        QVERIFY2(topEdgeOf(pKeyType) == topEdgeOf(pValueType),
                 qPrintable(
                         qsl("the two type pickers are on different lines: the key's at %1 and the value's at %2").arg(QString::number(topEdgeOf(pKeyType)), QString::number(topEdgeOf(pValueType)))));

        QCOMPARE(form()->label_variable_key->text(), qsl("Key"));
        QCOMPARE(form()->label_variable_value->text(), qsl("Value"));
        for (const QLabel* pLabel : {form()->label_variable_key, form()->label_variable_value}) {
            QVERIFY2(!pLabel->text().contains(QChar(scmLeftPointingArrow)) && !pLabel->text().contains(QChar(scmDownPointingArrow)),
                     qPrintable(qsl("\"%1\" still points at its picker with an arrow").arg(pLabel->text())));
        }

        // ...and the whole row starts where the name above it is typed
        const int nameEdge = leftEdgeOf(form()->lineEdit_var_name);
        qInfo().noquote() << qsl("  the name is typed at %1, the key picker starts at %2 and the switch under it at %3")
                                     .arg(QString::number(nameEdge), QString::number(leftEdgeOf(pKeyType)), QString::number(leftEdgeOf(form()->checkBox_variable_hidden)));
        QVERIFY2(leftEdgeOf(pKeyType) == nameEdge,
                 qPrintable(qsl("the key picker starts at %1 while the name above it is typed at %2").arg(QString::number(leftEdgeOf(pKeyType)), QString::number(nameEdge))));
        QVERIFY2(leftEdgeOf(form()->checkBox_variable_hidden) == nameEdge,
                 qPrintable(qsl("the switch that hides the variable starts at %1 while the picker above it starts at %2")
                                    .arg(QString::number(leftEdgeOf(form()->checkBox_variable_hidden)), QString::number(nameEdge))));

        for (QComboBox* pBox : {pKeyType, pValueType}) {
            for (int i = 0; i < pBox->count(); ++i) {
                QVERIFY2(!pBox->itemText(i).contains(QChar('\n')), qPrintable(qsl("\"%1\" is offered over more than one line").arg(pBox->itemText(i).simplified())));
            }
        }
    }

    // The switch over the tree is a piece of the panel's head, so it starts on
    // the line the search field above it and the tree rows below it start on
    void test_theHiddenVariablesSwitchStartsWhereTheSearchFieldDoes()
    {
        openTheVariablesView();

        QCheckBox* pSwitch = mpEditor->checkBox_displayAllVariables;
        QVERIFY2(pSwitch->isVisible(), "the Variables view is not offering the switch that shows hidden variables");
        const int searchEdge = leftEdgeOf(mpEditor->comboBox_searchTerms);
        qInfo().noquote() << qsl("  the search field starts at %1 and the switch under it at %2").arg(QString::number(searchEdge), QString::number(leftEdgeOf(pSwitch)));
        QVERIFY2(leftEdgeOf(pSwitch) == searchEdge,
                 qPrintable(qsl("the switch starts at %1 while the search field above it starts at %2").arg(QString::number(leftEdgeOf(pSwitch)), QString::number(searchEdge))));
        QCOMPARE(pSwitch->text(), qsl("Show hidden variables"));
    }

    // What the heading says about the caret belongs to the code pane: a root
    // row has no value under it, so neither the pane nor the readout is there
    void test_theCaretReadoutGoesWithTheCodePane()
    {
        // The pane's state on the way in is whatever the case before this left,
        // so it is put somewhere known rather than assumed
        openTheVariablesView();
        selectVariable(rootRow());

        QVERIFY2(showTheStringVariable(), "the string variable is not showing");
        QVERIFY2(mpEditor->mpSourceEditorArea->isVisible(), "the code pane is not showing the value of a string variable");
        QVERIFY2(!caretReadoutSays().isEmpty(), "the code pane came back without the caret readout that belongs to it");

        selectVariable(rootRow());
        QVERIFY2(!mpEditor->mpSourceEditorArea->isVisible(), "the code pane is still showing on the variables root row");
        QVERIFY2(caretReadoutSays().isEmpty(), qPrintable(qsl("the heading still reads out \"%1\" for a code pane that is not on show").arg(caretReadoutSays())));

        QVERIFY2(showTheStringVariable(), "the string variable is not showing again");
        QVERIFY2(!caretReadoutSays().isEmpty(), "the caret readout did not come back with the code pane");
    }

    // Six views type Lua under that heading; this one holds the value of
    // whatever the tree has chosen, and the heading says so
    void test_theCodePaneHeadingNamesWhatIsUnderIt()
    {
        openTheVariablesView();

        QLabel* pTitle = mpEditor->findChild<QLabel*>(qsl("editorCodeHeaderTitle"));
        QVERIFY2(pTitle != nullptr, "the code pane carries no heading");
        QCOMPARE(pTitle->text(), qsl("Value"));

        mpEditor->slot_showTriggers();
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        QCOMPARE(pTitle->text(), qsl("Lua script"));
    }
};

#include "EditorVariablesFormTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorVariablesFormTest)
