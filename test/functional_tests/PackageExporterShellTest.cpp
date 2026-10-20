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
 * The package exporter drawn in the design language, and the things about that
 * which can go wrong without anything else noticing.
 *
 * Every sheet goes on one of the shell's own containers rather than on the
 * window, because mudlet assigns a profile's Lua stylesheet to a dialog on show
 * - so a sheet set on the dialog would simply be replaced by it and the window
 * would come back platform-drawn with nothing failing.
 *
 * The count of what is ticked reads off a chip beside the heading now, but the
 * group box the .ui file headed the tree with is still titled with that same
 * count: PackageExporterDialogTest reads it there. The two have to stay in step.
 *
 * The packages this one needs are a row of chips over the hidden combo box the
 * export actually writes into, and every other exporter test still drives that
 * combo and the two buttons beside it - so an edit made either way has to reach
 * the other.
 *
 * A row of the tree is drawn by the shared row recipe with the shared choice
 * mark on it, and the chosen row carries the accent bar down its leading edge -
 * which is read here as pixels, since no other reading of it is possible.
 *
 * Starting over clears every field, tick and file. The picker's first entry does
 * the same act, and both ask first when there are words on the form that only
 * exist here - so a case that never gets asked and a case that is asked and says
 * no are both read here, since a prompt that stopped appearing would leave the
 * happy path passing on its own.
 *
 * Run with: ctest -R PackageExporterShellTest -V
 */

#include <QAbstractButton>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest/QtTest>


#include "ChipRow.h"
#include "Host.h"
#include "MudletApp.h"
#include "ProfileSessionTestHelper.h"
#include "TTrigger.h"
#include "TriggerUnit.h"
#include "dlgPackageExporter.h"
#include "dlgSystemMessageArea.h"
#include "mudlet.h"
#include "uiDesign.h"
#include "utils.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class PackageExporterShellTest : public QObject
{
    Q_OBJECT

private:
    TestProfile::Session mSession;
    // Where an export writes, through the setting getActualPath() falls back to
    QTemporaryDir mExportDir;
    Host* mpHost = nullptr;
    dlgPackageExporter* mpDialog = nullptr;
    QTimer* mpModalAnswerTimer = nullptr;
    int mBoxesAnswered = 0;
    const QString mProfileName = qsl("PackageExporterShell-Test-Profile");
    const QString mFolderName = qsl("exporter shell folder");
    // The installed package whose words the picker's entries fill the form in from
    const QString mPackageName = qsl("exporter-shell-dep-one");
    const QString mPackageAuthor = qsl("An Exporting Author");
    const QString mPackageTitle = qsl("What this package is for, in one line");
    const QString mPackageVersion = qsl("3.2.1");
    const QString mPackageDescription = qsl("The notes this package ships with.");

    // How far a sampled pixel may sit from the colour the tokens mix. A styled
    // fill is composited over what is under it rather than blitted, and the bar
    // is three pixels wide with an antialiased corner at each end, so the other
    // style tests' slack is what is allowed here too.
    static constexpr int scmFillTolerance = 24;

    void settle() const
    {
        QCoreApplication::processEvents();
        QTest::qWait(30ms);
        QCoreApplication::processEvents();
    }

    // The one path an appearance change takes while a window is open
    void setAppearance(const enums::Appearance appearance)
    {
        mudlet::self()->setAppearance(appearance);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        QCoreApplication::processEvents();
        mpDialog->grab();
    }

    static bool readsAs(const QColor& read, const QColor& wanted)
    {
        return std::abs(read.red() - wanted.red()) <= scmFillTolerance && std::abs(read.green() - wanted.green()) <= scmFillTolerance && std::abs(read.blue() - wanted.blue()) <= scmFillTolerance;
    }

    // A grab is taken at the screen's own pixel ratio, so a point measured in
    // the widget's coordinates is not where it lands in the image
    static QColor sampled(const QImage& shot, const QPoint& at)
    {
        const qreal ratio = shot.devicePixelRatio() > 0.0 ? shot.devicePixelRatio() : 1.0;
        return shot.pixelColor(qBound(0, qRound(at.x() * ratio), shot.width() - 1), qBound(0, qRound(at.y() * ratio), shot.height() - 1));
    }

    QTreeWidget* tree() const { return mpDialog->findChild<QTreeWidget*>(qsl("treeWidget_exportSelection")); }
    QLabel* infoLabel() const { return mpDialog->findChild<QLabel*>(qsl("infoLabel")); }
    QLabel* countChip() const { return mpDialog->findChild<QLabel*>(qsl("exporterSelectionCount")); }
    QWidget* selectionColumn() const { return mpDialog->findChild<QWidget*>(qsl("exporterSelectionColumn")); }
    QWidget* detailsColumn() const { return mpDialog->findChild<QWidget*>(qsl("exporterDetailsColumn")); }
    QWidget* foot() const { return mpDialog->findChild<QWidget*>(qsl("exporterFoot")); }
    dlgSystemMessageArea* notice() const { return mpDialog->findChild<dlgSystemMessageArea*>(qsl("exporterNotice")); }
    uiDesign::ChipRow* chips() const { return mpDialog->findChild<uiDesign::ChipRow*>(qsl("exporterDependencies")); }
    QComboBox* comboNamed(const QString& name) const { return mpDialog->findChild<QComboBox*>(name); }
    QPushButton* buttonNamed(const QString& name) const { return mpDialog->findChild<QPushButton*>(name); }
    QLineEdit* fieldNamed(const QString& name) const { return mpDialog->findChild<QLineEdit*>(name); }
    QPushButton* startOverButton() const { return buttonNamed(qsl("exporterStartOverButton")); }
    QTextEdit* descriptionField() const { return mpDialog->findChild<QTextEdit*>(qsl("textEdit_description")); }
    QListWidget* filesToShip() const { return mpDialog->findChild<QListWidget*>(qsl("listWidget_addedFiles")); }
    QString selectionTitle() const { return mpDialog->findChild<QGroupBox*>(qsl("groupBox_exportSelection"))->title(); }
    QPushButton* exportButton() const { return mpDialog->findChild<QDialogButtonBox*>(qsl("buttonBox"))->button(QDialogButtonBox::Apply); }

    QTreeWidgetItem* folderRow() const
    {
        QTreeWidgetItem* pTriggers = tree()->topLevelItem(0);
        for (int child = 0; pTriggers && child < pTriggers->childCount(); ++child) {
            if (pTriggers->child(child)->text(0) == mFolderName) {
                return pTriggers->child(child);
            }
        }
        return nullptr;
    }

    // A fresh dialog per case: the tree's check states, the chips and the notice
    // all carry over otherwise, and a case reading one of them would be reading
    // what the case before it left
    void openExporter()
    {
        delete mpDialog;
        mpDialog = new dlgPackageExporter(nullptr, mpHost);
        mpDialog->show();
        QVERIFY(QTest::qWaitForWindowExposed(mpDialog));
        settle();
    }

    // QMessageBox::exec() spins its own event loop, so the answer has to come from
    // a timer armed before the call that puts the box up - and how many boxes it
    // answered is counted, since a prompt that stopped appearing at all would leave
    // every case that presses Yes passing.
    void answerNextMessageBox(const QMessageBox::StandardButton answer)
    {
        stopAnsweringMessageBoxes();
        mBoxesAnswered = 0;
        mpModalAnswerTimer = new QTimer(this);
        mpModalAnswerTimer->setInterval(20);
        connect(mpModalAnswerTimer, &QTimer::timeout, this, [this, answer]() {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box) {
                return;
            }
            if (auto* button = box->button(answer)) {
                ++mBoxesAnswered;
                button->click();
            }
            stopAnsweringMessageBoxes();
        });
        mpModalAnswerTimer->start();
    }

    void stopAnsweringMessageBoxes()
    {
        if (!mpModalAnswerTimer) {
            return;
        }
        mpModalAnswerTimer->stop();
        mpModalAnswerTimer->deleteLater();
        mpModalAnswerTimer = nullptr;
    }

    // The form as the window opened it, read field by field so that a failure says
    // which one came back wrong rather than that something did
    void verifyTheFormIsFresh(const QString& freshAuthor, const QString& freshVersion, const QString& freshDescription)
    {
        QVERIFY2(fieldNamed(qsl("lineEdit_packageName"))->text().isEmpty(), qPrintable(qsl("the name still reads \"%1\"").arg(fieldNamed(qsl("lineEdit_packageName"))->text())));
        QCOMPARE(comboNamed(qsl("packageList"))->currentIndex(), 0);
        QCOMPARE(fieldNamed(qsl("lineEdit_author"))->text(), freshAuthor);
        QVERIFY2(fieldNamed(qsl("lineEdit_title"))->text().isEmpty(), qPrintable(qsl("the one-line summary still reads \"%1\"").arg(fieldNamed(qsl("lineEdit_title"))->text())));
        QVERIFY2(fieldNamed(qsl("lineEdit_helpUrl"))->text().isEmpty(), qPrintable(qsl("the help page still reads \"%1\"").arg(fieldNamed(qsl("lineEdit_helpUrl"))->text())));
        QCOMPARE(fieldNamed(qsl("lineEdit_version"))->text(), freshVersion);
        QCOMPARE(descriptionField()->toPlainText(), freshDescription);
        QCOMPARE(chips()->count(), 0);
        QCOMPARE(comboNamed(qsl("comboBox_dependencies"))->count(), 0);
        QVERIFY2(mpDialog->mPackageIconPath.isEmpty(), qPrintable(qsl("the package's picture is still %1").arg(mpDialog->mPackageIconPath)));
        QVERIFY2(buttonNamed(qsl("pushButton_removeIcon"))->isHidden(), "the cross that takes the picture off is still offered with no picture to take off");
        QCOMPARE(filesToShip()->count(), 0);
        QCOMPARE(mpDialog->countCheckedItems(), 0);
        QVERIFY2(!startOverButton()->isEnabled(), "the clear button is still available on a form with nothing on it to clear");
        QVERIFY2(notice()->isHidden(), "the notice is still on show");
    }

    // The picker's entry for the installed package the form is filled in from
    int rowOfThePackage() const { return comboNamed(qsl("packageList"))->findText(mPackageName); }

    // The zip runs on a worker thread, and the Close button coming back is what
    // says the whole of it has finished. Read the same way PackageExporterTest
    // reads it, which is the only signal the dialog offers.
    bool waitForExportToSettle() const
    {
        auto* pClose = mpDialog->findChild<QDialogButtonBox*>(qsl("buttonBox"))->button(QDialogButtonBox::Close);
        const bool settled = QTest::qWaitFor(
                [pClose]() {
                    return !pClose->isHidden();
                },
                30000);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return settled;
    }

    // utils::unzip() joins its destination onto each archive entry without a
    // separator, so the trailing slash is load-bearing
    static bool unpackInto(const QString& packageFile, const QTemporaryDir& destination) { return utils::unzip(packageFile, qsl("%1/").arg(destination.path()), QDir(destination.path())); }

    // What the folder's box reads once the debounced pass has derived it
    bool folderReads(const Qt::CheckState wanted) const
    {
        return QTest::qWaitFor(
                [this, wanted]() {
                    return folderRow()->checkState(0) == wanted;
                },
                2s);
    }

    bool countChipReads(const QString& wanted) const
    {
        return QTest::qWaitFor(
                [this, wanted]() {
                    return countChip()->text().contains(wanted);
                },
                2s);
    }

private slots:
    void initTestCase()
    {
        if (TestProfile::Session::configCannotBeRedirected()) {
            QSKIP(TestProfile::Session::configCannotBeRedirectedReason());
        }

        QVERIFY2(mSession.startMudlet(), qPrintable(mSession.why()));
        // The words below are read as words rather than as pictures
        mudlet::self()->setInterfaceLanguage(qsl("en_US"));

        // getActualPath() falls back to this setting when nothing was browsed
        // for, which is the only way a test steers where a package lands
        QVERIFY(mExportDir.isValid());
        MudletApp::getQSettings()->setValue(qsl("lastFileDialogLocation"), mExportDir.path());

        QVERIFY2(mSession.openProfile(mProfileName), qPrintable(mSession.why()));
        mpHost = mSession.host();

        // A folder with exactly two triggers in it: ticking the folder ticks all
        // three, which is the count the chip and the hidden group box's title
        // both have to arrive at
        auto* pFolder = new TTrigger(nullptr, mpHost);
        pFolder->setName(mFolderName);
        pFolder->setIsFolder(true);
        mpHost->getTriggerUnit()->registerTrigger(pFolder);
        for (const QString& name : {qsl("exporter shell first"), qsl("exporter shell second")}) {
            auto* pChild = new TTrigger(pFolder, mpHost);
            pChild->setName(name);
            pChild->setRegexCodeList(QStringList{name}, QList<int>{REGEX_SUBSTRING});
            mpHost->getTriggerUnit()->registerTrigger(pChild);
        }

        // Two installed packages, so the chip row has something to offer and the
        // hidden picker has something to hand across
        mpHost->mInstalledPackages << mPackageName << qsl("exporter-shell-dep-two");

        // ...and everything the form fills in from the first of them, so that
        // choosing it in the picker writes words into every field starting over has
        // to put back. Written onto the host rather than installed: what is read
        // here is what the window does with them.
        mpHost->mPackageInfo.insert(mPackageName,
                                    QMap<QString, QString>{{qsl("mpackage"), mPackageName},
                                                           {qsl("author"), mPackageAuthor},
                                                           {qsl("title"), mPackageTitle},
                                                           {qsl("version"), mPackageVersion},
                                                           {qsl("description"), mPackageDescription},
                                                           {qsl("dependencies"), qsl("exporter-shell-dep-two")}});

        openExporter();
        QVERIFY2(tree(), "the exporter has no tree of things to export");
        QVERIFY2(folderRow(), "SETUP: the trigger folder this walks never reached the tree");
    }

    void cleanupTestCase()
    {
        stopAnsweringMessageBoxes();
        delete mpDialog;
        mpDialog = nullptr;
        QCoreApplication::processEvents();
        mpHost = nullptr;
        if (mudlet::self()) {
            mudlet::self()->setAppearance(enums::Appearance::systemSetting);
        }
        mSession.stop();
    }

    // Every sheet is on one of the shell's containers and none on the dialog,
    // whose own stylesheet a profile's Lua one is assigned to on every show
    void test_theSheetsAreSetOnTheContainersRatherThanOnTheWindow()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();

        QVERIFY2(mpDialog->styleSheet().isEmpty(), qPrintable(qsl("the dialog itself carries a sheet, which a profile's Lua stylesheet replaces on show: %1").arg(mpDialog->styleSheet().left(120))));

        const QString left = selectionColumn()->styleSheet();
        QVERIFY2(left.contains(tokens.pane.name()),
                 qPrintable(qsl("the selection column's sheet never names the pane tone %1, so it is not told apart from the page beside it").arg(tokens.pane.name())));
        QVERIFY2(left.contains(qsl("exporterSelectionCount")), qPrintable(qsl("the selection column's sheet draws no count chip: %1").arg(left.left(200))));

        const QString rows = tree()->styleSheet();
        const QString chosenWash = uiDesign::blend(tokens.pane, tokens.accent, uiDesign::scmAccentWashStrength).name();
        QVERIFY2(rows.contains(chosenWash), qPrintable(qsl("the tree's sheet never names %1, the wash the design fills a chosen row with: %2").arg(chosenWash, rows.left(240))));
        QVERIFY2(rows.contains(qsl("::indicator")), "the tree's rows carry no choice mark of the design's, so the ticks are platform-drawn");
        QVERIFY2(rows.contains(qsl("::branch")), "the tree's branch column is left to the platform, so the arrow that folds a group away is not the design's");
        QVERIFY2(rows.contains(qsl("QScrollBar")), "the tree's own scrollbars are left platform-drawn");

        const QString right = detailsColumn()->styleSheet();
        QVERIFY2(right.contains(tokens.page.name()), qPrintable(qsl("the details column's sheet never names the page tone %1").arg(tokens.page.name())));
        QVERIFY2(right.contains(qsl("QLineEdit")), qPrintable(qsl("the details column's sheet draws no field: %1").arg(right.left(200))));
        QVERIFY2(right.contains(qsl("QPushButton")), "the details column's sheet draws no button");
        QVERIFY2(right.contains(qsl("#editorChip")), "the details column's sheet draws no chips, so the packages this one needs are platform-drawn");
        QVERIFY2(right.contains(qsl("dashed")), "the zone the files to ship are dropped on has no dashed edge, so nothing says it takes a drop");

        const QString bottom = foot()->styleSheet();
        QVERIFY2(bottom.contains(tokens.page.name()), qPrintable(qsl("the foot's sheet never names the page tone %1").arg(tokens.page.name())));
        QVERIFY2(bottom.contains(qsl("border-top")), "the foot draws no seam on its top edge, so it runs into the columns above it");
        QVERIFY2(bottom.contains(qsl("QPushButton")), "the foot's sheet draws no button, so the Export button is platform-drawn");
    }

    // Every field the reader fills in stands on the form, the name included: it
    // came off the row across the top, where only the picker and the clear button
    // are left, and it is drawn as one field among the rest rather than as the
    // prominent one
    void test_theNameIsTheFirstFieldOfTheForm()
    {
        openExporter();
        auto* pName = fieldNamed(qsl("lineEdit_packageName"));
        QVERIFY(pName);
        QVERIFY2(detailsColumn()->isAncestorOf(pName), "the name field is not on the form at all, so the fields are read in two places");

        auto* pCaption = mpDialog->findChild<QLabel*>(qsl("exporterNameLabel"));
        QVERIFY2(pCaption, "the name field has no word over it");
        QCOMPARE(pCaption->text(), qsl("Package name"));
        QCOMPARE(pCaption->buddy(), pName);

        const QString topRow = mpDialog->findChild<QWidget*>(qsl("widget_newOrExistingPackageName"))->styleSheet();
        QVERIFY2(!topRow.contains(qsl("lineEdit_packageName")), qPrintable(qsl("the top row still draws the name field as the prominent one: %1").arg(topRow.left(240))));
    }

    // The count the reader sees is the chip beside the heading; the hidden group
    // box still carries it in its title, which is where the dialog's own tests
    // read it
    void test_theCountChipAndTheHiddenTitleAgree()
    {
        openExporter();
        QVERIFY2(countChip(), "there is no count chip beside the heading at all");
        QCOMPARE(countChip()->text(), qsl("nothing yet"));
        QVERIFY2(countChip()->property("exporterCountEmpty").toBool(), "the chip does not say it is empty, so it is drawn as a lit chip with no count on it");

        folderRow()->setCheckState(0, Qt::Checked);
        QVERIFY2(QTest::qWaitFor(
                         [this]() {
                             return countChip()->text() != qsl("nothing yet");
                         },
                         2s),
                 "the count chip never took the count");

        qInfo().noquote() << qsl("  the chip reads \"%1\" and the hidden title \"%2\"").arg(countChip()->text(), selectionTitle());
        // Three: the folder and both of the triggers in it, so counting only the
        // leaves or only the folder would each give a different answer. Read as
        // the number rather than as the whole phrase, since which plural form a
        // language picks is the translation's business
        QVERIFY2(countChip()->text().contains(QChar('3')), qPrintable(qsl("the chip reads \"%1\" rather than counting all three ticked things").arg(countChip()->text())));
        QVERIFY2(!countChip()->property("exporterCountEmpty").toBool(), "the chip still says it is empty with three things ticked");
        QVERIFY2(selectionTitle().contains(QChar('3')),
                 qPrintable(qsl("the hidden group box's title reads \"%1\" rather than carrying the count the dialog's own tests read off it").arg(selectionTitle())));

        folderRow()->setCheckState(0, Qt::Unchecked);
        QVERIFY2(QTest::qWaitFor(
                         [this]() {
                             return countChip()->text() == qsl("nothing yet");
                         },
                         2s),
                 "unticking everything left the count on the chip");
    }

    // A refusal is said in the notice under the form, in its error reading. The
    // hidden label still holds the words, which is where both of the exporter's
    // other tests read a result.
    void test_aRefusalIsSaidInTheNotice()
    {
        openExporter();
        QVERIFY2(notice(), "the exporter has no notice to say anything in");
        QVERIFY2(notice()->isHidden(), "the notice is on show before the window has anything to report");

        mpDialog->slot_exportPackage();
        settle();

        QVERIFY2(!notice()->isHidden(), "exporting with no package name said nothing in the notice");
        QVERIFY2(!notice()->notificationAreaIconLabelError->isHidden(), "the notice is showing, but not in its error reading - nothing says the export was refused");
        QVERIFY2(notice()->notificationAreaMessageBox->text().contains(qsl("Please enter the package name")),
                 qPrintable(qsl("the notice reads \"%1\" rather than the refusal").arg(notice()->notificationAreaMessageBox->text())));
        QVERIFY2(infoLabel()->text().contains(qsl("Please enter the package name")), qPrintable(qsl("the hidden label the exporter's other tests read holds \"%1\"").arg(infoLabel()->text())));
        QVERIFY2(!infoLabel()->text().contains(QChar('#')), qPrintable(qsl("the refusal still carries a colour written into it: \"%1\"").arg(infoLabel()->text())));

        notice()->messageAreaCloseButton->click();
        settle();
        QVERIFY2(notice()->isHidden(), "the cross on the notice does not close it");
    }

    // The chips are a view over the hidden combo box the export writes into, so
    // an edit made either way has to reach the other
    void test_theChipsAndTheHiddenComboStayInStep()
    {
        openExporter();
        QVERIFY2(chips(), "the packages this one needs are not drawn as a row of chips at all");
        auto* pAvailable = comboNamed(qsl("DependencyList"));
        auto* pRequired = comboNamed(qsl("comboBox_dependencies"));
        QVERIFY2(pAvailable && pRequired, "the two hidden pickers the export reads are gone");
        QVERIFY2(pAvailable->isHidden() && pRequired->isHidden() && buttonNamed(qsl("pushButton_addDependency"))->isHidden(), "the controls the chip row replaces are still on show beside it");
        QCOMPARE(chips()->count(), 0);

        // Added the old way: the hidden picker and its "+" button, which is what
        // every other exporter test presses
        pAvailable->setCurrentIndex(pAvailable->findText(qsl("exporter-shell-dep-two")));
        buttonNamed(qsl("pushButton_addDependency"))->click();
        settle();
        qInfo().noquote() << qsl("  after the hidden + the row shows %1").arg(chips()->items().join(qsl(", ")));
        QCOMPARE(pRequired->count(), 1);
        QCOMPARE(chips()->items(), QStringList{qsl("exporter-shell-dep-two")});

        // ...and taken away the new way: the row is told what it holds, and says
        // so the way a chip's own cross does
        chips()->setItems({});
        emit chips() -> itemsChanged();
        settle();
        qInfo().noquote() << qsl("  after the row was emptied the hidden combo holds %1").arg(QString::number(pRequired->count()));
        QCOMPARE(pRequired->count(), 0);
        QVERIFY2(pAvailable->findText(qsl("exporter-shell-dep-two")) > 0, "a package taken off the row is no longer offered back");
    }

    // A row of the tree is the design's row: the pane under it, the accent wash
    // on the chosen one and the accent bar down its leading edge
    void test_theTreeRowsAreTheDesignsRows()
    {
        openExporter();
        tree()->setCurrentItem(tree()->topLevelItem(0));
        settle();

        const QRect row = tree()->visualItemRect(tree()->topLevelItem(0));
        QVERIFY2(!row.isEmpty(), "the chosen row has no rectangle on the viewport, so there is nothing to read");
        const QImage shot = tree()->viewport()->grab().toImage();
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();

        const QColor wash = sampled(shot, QPoint(row.right() - 3, row.center().y()));
        const QColor wanted = uiDesign::blend(tokens.pane, tokens.accent, uiDesign::scmAccentWashStrength);
        qInfo().noquote() << qsl("  the chosen row reads %1 against a wash of %2, and its gutter %3 against an accent of %4")
                                     .arg(wash.name(), wanted.name(), sampled(shot, QPoint(row.left() + 1, row.center().y())).name(), tokens.accent.name());
        QVERIFY2(readsAs(wash, wanted), qPrintable(qsl("the chosen row reads %1 where the design fills it with %2").arg(wash.name(), wanted.name())));

        const QColor bar = sampled(shot, QPoint(row.left() + 1, row.center().y()));
        QVERIFY2(readsAs(bar, tokens.accent),
                 qPrintable(qsl("the chosen row's leading gutter reads %1 where the accent is %2 - the bar that says which row is chosen is not being painted").arg(bar.name(), tokens.accent.name())));

        // ...and an unchosen row is the pane it stands on rather than anything
        // the platform fills a view with
        const QRect plain = tree()->visualItemRect(tree()->topLevelItem(1));
        const QColor pane = sampled(shot, QPoint(plain.right() - 3, plain.center().y()));
        QVERIFY2(readsAs(pane, tokens.pane), qPrintable(qsl("an unchosen row reads %1 where the column it is on is %2").arg(pane.name(), tokens.pane.name())));
    }

    // Export is the one thing this window is for, so it is the one button drawn
    // as an invitation - and it is not offered at all until the package is named
    void test_theExportButtonIsThePrimaryOne()
    {
        openExporter();
        QVERIFY2(exportButton(), "the exporter has no Export button");
        QVERIFY2(exportButton()->property(uiDesign::scmProp_primaryButton).toBool(), "Export does not carry the primary-button property, so it is drawn as one control among several");
        QVERIFY2(!exportButton()->icon().isNull(), "Export carries no glyph");
        QVERIFY2(foot()->styleSheet().contains(QLatin1StringView(uiDesign::scmProp_primaryButton)), "the foot's sheet has no rule for the primary button, so the property draws nothing");

        auto* pName = mpDialog->findChild<QLineEdit*>(qsl("lineEdit_packageName"));
        QVERIFY(pName);
        QVERIFY2(pName->text().isEmpty(), "SETUP: the name field is not empty");
        QVERIFY2(!exportButton()->isEnabled(), "Export is offered with no package name to export under");
        pName->setText(qsl("exporter-shell-named"));
        settle();
        QVERIFY2(exportButton()->isEnabled(), "Export stayed unavailable after the package was named");
        pName->clear();
        settle();
    }

    // Making a module is the same window saying it is doing something else, and
    // all three places that say which have to say the same one
    void test_moduleModeSwapsTheWords()
    {
        openExporter();
        QCOMPARE(mpDialog->findChild<QLabel*>(qsl("exporterSelectionHeading"))->text(), qsl("Select what to export"));
        QCOMPARE(mpDialog->findChild<QLabel*>(qsl("exporterDetailsHeading"))->text(), qsl("About the package"));
        QCOMPARE(mpDialog->findChild<QLabel*>(qsl("exporterSaveCaption"))->text(), qsl("Saves to"));

        mpDialog->setModuleCreationMode(true);
        settle();
        QCOMPARE(mpDialog->findChild<QLabel*>(qsl("exporterSelectionHeading"))->text(), qsl("Select items to include in module"));
        QCOMPARE(mpDialog->findChild<QLabel*>(qsl("exporterDetailsHeading"))->text(), qsl("About the module"));
        QCOMPARE(mpDialog->findChild<QLabel*>(qsl("exporterSaveCaption"))->text(), qsl("Saves into the profile as"));
        QVERIFY2(comboNamed(qsl("packageList"))->isHidden(), "the 'start from an installed package' picker has no meaning while a module is being made");
        QCOMPARE(buttonNamed(qsl("pushButton_packageLocation"))->text(), qsl("Change folder"));
    }

    // Nothing outside this window restyles it: it is a QDialog no other window
    // owns, so the appearance signal is the whole of what moves it. A pass that
    // stopped running leaves the sheets of the theme that has been left.
    void test_anAppearanceChangeRebuildsEverySheet()
    {
        openExporter();
        QMap<QString, QString> sheets;
        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            setAppearance(appearance.second);
            const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
            qInfo().noquote() << qsl("  %1: pane %2, page %3").arg(appearance.first, tokens.pane.name(), tokens.page.name());
            QVERIFY2(selectionColumn()->styleSheet().contains(tokens.pane.name()),
                     qPrintable(qsl("on the %1 appearance the selection column's sheet does not name the pane tone %2 that appearance mixes").arg(appearance.first, tokens.pane.name())));
            QVERIFY2(detailsColumn()->styleSheet().contains(tokens.page.name()),
                     qPrintable(qsl("on the %1 appearance the details column's sheet does not name the page tone %2 that appearance mixes").arg(appearance.first, tokens.page.name())));
            QVERIFY2(foot()->styleSheet().contains(tokens.page.name()),
                     qPrintable(qsl("on the %1 appearance the foot's sheet does not name the page tone %2 that appearance mixes").arg(appearance.first, tokens.page.name())));
            QVERIFY2(!exportButton()->icon().isNull(), qPrintable(qsl("on the %1 appearance Export lost its glyph").arg(appearance.first)));
            sheets.insert(appearance.first, detailsColumn()->styleSheet());
        }
        QVERIFY2(sheets.value(qsl("dark")) != sheets.value(qsl("light")), "both appearances leave the details column with the same sheet, so the pass is not being re-run at all");
    }

    // A folder's own box says what is ticked inside it, which is the only thing a
    // collapsed folder can say about its contents: the dash while some of what is
    // in it is ticked, nothing while none of it is.
    void test_aFolderShowsTheDashForWhatIsTickedInsideIt()
    {
        openExporter();
        QVERIFY2(fieldNamed(qsl("lineEdit_packageName"))->text().isEmpty(), "SETUP: the name field has words on it, so the folder could be read as the package's own");
        QVERIFY2(!folderRow()->isExpanded(), "SETUP: the folder is open, so its contents are readable without the folder's own box saying anything");
        QCOMPARE(folderRow()->checkState(0), Qt::Unchecked);
        QCOMPARE(folderRow()->childCount(), 2);

        folderRow()->child(0)->setCheckState(0, Qt::Checked);
        QVERIFY2(folderReads(Qt::PartiallyChecked),
                 qPrintable(qsl("one of two things ticked inside it and the folder reads %1 rather than the dash").arg(static_cast<int>(folderRow()->checkState(0)))));
        QVERIFY2(countChipReads(qsl("1")), qPrintable(qsl("the count chip reads \"%1\" with one thing ticked").arg(countChip()->text())));

        folderRow()->child(1)->setCheckState(0, Qt::Checked);
        QVERIFY2(countChipReads(qsl("2")), qPrintable(qsl("the count chip reads \"%1\" with both of the folder's triggers ticked").arg(countChip()->text())));
        // Still the dash: the folder itself was never ticked, so it is not one of
        // the things that goes into the file
        QCOMPARE(folderRow()->checkState(0), Qt::PartiallyChecked);

        folderRow()->child(0)->setCheckState(0, Qt::Unchecked);
        folderRow()->child(1)->setCheckState(0, Qt::Unchecked);
        QVERIFY2(folderReads(Qt::Unchecked), qPrintable(qsl("with nothing ticked inside it the folder still reads %1").arg(static_cast<int>(folderRow()->checkState(0)))));
        QVERIFY2(countChipReads(qsl("nothing yet")), qPrintable(qsl("the count chip reads \"%1\" with nothing ticked at all").arg(countChip()->text())));

        // ...and a folder the reader ticks themselves keeps that tick, which is
        // the folder going into the file alongside what is in it
        folderRow()->setCheckState(0, Qt::Checked);
        QVERIFY2(countChipReads(qsl("3")), qPrintable(qsl("the count chip reads \"%1\" where the folder and both of its triggers are three things").arg(countChip()->text())));
        QCOMPARE(folderRow()->checkState(0), Qt::Checked);

        folderRow()->setCheckState(0, Qt::Unchecked);
        QVERIFY(folderReads(Qt::Unchecked));
    }

    // Install makes a folder named after the package again and reads the package's
    // items under it, so that folder stands for its contents: its box reads them,
    // and it is never written into the file itself.
    void test_aFolderNamedAfterThePackageStandsForItsContents()
    {
        openExporter();
        auto* pName = fieldNamed(qsl("lineEdit_packageName"));
        pName->setText(mFolderName);
        settle();

        folderRow()->setCheckState(0, Qt::Checked);
        QVERIFY2(countChipReads(qsl("2")), qPrintable(qsl("the count chip reads \"%1\" where only the two triggers inside the folder named after the package go in").arg(countChip()->text())));
        QCOMPARE(folderRow()->child(0)->checkState(0), Qt::Checked);
        QCOMPARE(folderRow()->child(1)->checkState(0), Qt::Checked);
        QVERIFY2(folderRow()->checkState(0) == Qt::Checked,
                 qPrintable(qsl("with all of its contents ticked the folder reads %1 rather than a full tick").arg(static_cast<int>(folderRow()->checkState(0)))));

        folderRow()->child(1)->setCheckState(0, Qt::Unchecked);
        QVERIFY2(folderReads(Qt::PartiallyChecked),
                 qPrintable(qsl("with one of two things ticked inside it the folder reads %1 rather than the dash").arg(static_cast<int>(folderRow()->checkState(0)))));
        QVERIFY2(countChipReads(qsl("1")), qPrintable(qsl("the count chip reads \"%1\" with one trigger left ticked").arg(countChip()->text())));

        // Taking the name off the form makes it an ordinary folder again, whose own
        // tick was never set - so it goes on showing what is ticked inside it
        pName->clear();
        settle();
        QVERIFY2(folderReads(Qt::PartiallyChecked), qPrintable(qsl("as an ordinary folder with one thing ticked inside it the folder reads %1").arg(static_cast<int>(folderRow()->checkState(0)))));

        // ...and the half of it that matters: what reaches the file
        pName->setText(mFolderName);
        folderRow()->setCheckState(0, Qt::Checked);
        QVERIFY2(countChipReads(qsl("2")), qPrintable(qsl("SETUP: the count chip reads \"%1\" before the export").arg(countChip()->text())));

        const QString packageFile = qsl("%1/%2.mpackage").arg(mExportDir.path(), mFolderName);
        QFile::remove(packageFile);
        mpDialog->slot_exportPackage();
        QVERIFY2(waitForExportToSettle(), "the export never finished");
        QVERIFY2(QFileInfo::exists(packageFile), qPrintable(qsl("no package file was written; the window said \"%1\"").arg(infoLabel()->text())));

        QTemporaryDir unpacked;
        QVERIFY(unpacked.isValid());
        QVERIFY2(unpackInto(packageFile, unpacked), "the exported package is not a readable archive");
        QFile items(qsl("%1/%2.xml").arg(unpacked.path(), mFolderName));
        QVERIFY2(items.open(QIODevice::ReadOnly | QIODevice::Text), "the package has no items XML");
        const QString itemsXml = QString::fromUtf8(items.readAll());
        for (const QString& name : {qsl("exporter shell first"), qsl("exporter shell second")}) {
            QVERIFY2(itemsXml.contains(name), qPrintable(qsl("%1 was inside the ticked folder but is not in the exported XML").arg(name)));
        }
        QVERIFY2(!itemsXml.contains(mFolderName), qPrintable(qsl("the folder named after the package is in the XML, so installing it gives Triggers/%1/%1: %2").arg(mFolderName, itemsXml.left(400))));

        folderRow()->setCheckState(0, Qt::Unchecked);
        pName->clear();
        settle();
    }

    // Clearing the form is the one act on this window that cannot be undone, so it
    // is unavailable while there is nothing on the form to clear - on show either
    // way, so the row does not change shape on the first keystroke
    void test_startOverIsOfferedOnlyOnceThereIsSomethingToClear()
    {
        openExporter();
        QVERIFY2(startOverButton(), "the top row carries no clear button at all");
        QVERIFY2(!startOverButton()->isHidden(), "the clear button is hidden rather than standing on the row unavailable");
        QVERIFY2(!startOverButton()->isEnabled(), "the clear button is available on a form nobody has touched yet");

        auto* pName = fieldNamed(qsl("lineEdit_packageName"));
        pName->setText(qsl("exporter-shell-named"));
        settle();
        QVERIFY2(startOverButton()->isEnabled(), "naming the package left the clear button unavailable, so there is no way back off a form that now has something on it");

        pName->clear();
        settle();
        QVERIFY2(!startOverButton()->isEnabled(), "clearing the name by hand left the clear button available with nothing left to clear");

        // A tick is something on the form too, and it reaches the button through the
        // debounced recount rather than through a field's textChanged
        folderRow()->setCheckState(0, Qt::Checked);
        QVERIFY2(QTest::qWaitFor(
                         [this]() {
                             return startOverButton()->isEnabled();
                         },
                         2s),
                 "ticking the tree never made the clear button available, so a form with three things ticked on it says there is nothing to clear");

        folderRow()->setCheckState(0, Qt::Unchecked);
        QVERIFY2(QTest::qWaitFor(
                         [this]() {
                             return !startOverButton()->isEnabled();
                         },
                         2s),
                 "unticking everything left the clear button available");
    }

    // ...and what it does: every field, every tick, the picture and the files back
    // to how the window opened, from a form an installed package filled in
    void test_startOverPutsTheFormBackToHowItOpened()
    {
        openExporter();
        // Read off the fresh form rather than written down here: the author is
        // remembered between sessions and the description is a template in the .ui
        const QString freshAuthor = fieldNamed(qsl("lineEdit_author"))->text();
        const QString freshVersion = fieldNamed(qsl("lineEdit_version"))->text();
        const QString freshDescription = descriptionField()->toPlainText();
        QVERIFY2(freshVersion == qsl("1"), qPrintable(qsl("SETUP: the version field opens on \"%1\" rather than on the .ui file's 1").arg(freshVersion)));
        QVERIFY2(!freshDescription.isEmpty(), "SETUP: the description opens empty, so putting the template back cannot be told from clearing it");

        const int at = rowOfThePackage();
        QVERIFY2(at > 0, "SETUP: the installed package this walks never reached the picker");
        comboNamed(qsl("packageList"))->setCurrentIndex(at);
        settle();

        // The package's words really are on the form, or every check below would
        // pass on a form that was empty all along
        QCOMPARE(fieldNamed(qsl("lineEdit_packageName"))->text(), mPackageName);
        QCOMPARE(fieldNamed(qsl("lineEdit_author"))->text(), mPackageAuthor);
        QCOMPARE(fieldNamed(qsl("lineEdit_title"))->text(), mPackageTitle);
        QCOMPARE(fieldNamed(qsl("lineEdit_version"))->text(), mPackageVersion);
        QCOMPARE(chips()->items(), QStringList{qsl("exporter-shell-dep-two")});
        QVERIFY2(descriptionField()->toPlainText() != freshDescription, "SETUP: the package's notes never replaced the template in the description");

        folderRow()->setCheckState(0, Qt::Checked);
        QVERIFY2(QTest::qWaitFor(
                         [this]() {
                             return mpDialog->countCheckedItems() > 0;
                         },
                         2s),
                 "SETUP: ticking the folder never reached the tree");
        filesToShip()->addItem(qsl("/exporter-shell/a-file-to-ship.png"));
        // A picture can only otherwise be chosen through a file dialog, so the path
        // is written where the picker would have written it
        mpDialog->mPackageIconPath = qsl(":/icons/aardwolf_mud.png");
        mpDialog->showChosenIcon();
        settle();
        QVERIFY2(!buttonNamed(qsl("pushButton_removeIcon"))->isHidden(), "SETUP: the picture never took, so clearing it cannot be read");
        QVERIFY2(startOverButton()->isEnabled(), "SETUP: the clear button is unavailable on a form an installed package filled in");

        // Nothing here was typed by hand, so nothing should be asked - and the
        // answer is armed anyway, or a prompt appearing would hang the case
        answerNextMessageBox(QMessageBox::Yes);
        startOverButton()->click();
        settle();
        stopAnsweringMessageBoxes();
        QCOMPARE(mBoxesAnswered, 0);

        verifyTheFormIsFresh(freshAuthor, freshVersion, freshDescription);
    }

    // The picker's first entry is the same act. It used to untick the tree and clear
    // the name, leaving the author, the notes, the required packages, the picture
    // and the files standing as the chosen package left them.
    void test_choosingTheFirstEntryAgainStartsOver()
    {
        openExporter();
        const QString freshAuthor = fieldNamed(qsl("lineEdit_author"))->text();
        const QString freshVersion = fieldNamed(qsl("lineEdit_version"))->text();
        const QString freshDescription = descriptionField()->toPlainText();

        auto* pList = comboNamed(qsl("packageList"));
        const int at = rowOfThePackage();
        QVERIFY2(at > 0, "SETUP: the installed package this walks never reached the picker");
        pList->setCurrentIndex(at);
        settle();
        QCOMPARE(fieldNamed(qsl("lineEdit_author"))->text(), mPackageAuthor);
        QCOMPARE(chips()->count(), 1);

        answerNextMessageBox(QMessageBox::Yes);
        pList->setCurrentIndex(0);
        settle();
        stopAnsweringMessageBoxes();
        QVERIFY2(mBoxesAnswered == 0, "the reader was asked before a form they never typed into was cleared");

        verifyTheFormIsFresh(freshAuthor, freshVersion, freshDescription);
    }

    // Ticks, chips, pictures and files are one gesture to make again; a paragraph
    // nobody kept a copy of is not. So words written here are asked about first,
    // from the button and from the picker's first entry alike.
    void test_startOverAsksBeforeDiscardingTypedWords()
    {
        openExporter();
        const QString typedTitle = qsl("Written here and nowhere else");
        fieldNamed(qsl("lineEdit_title"))->setText(typedTitle);
        settle();
        QVERIFY2(startOverButton()->isEnabled(), "SETUP: the clear button is unavailable after a summary was typed");

        answerNextMessageBox(QMessageBox::No);
        startOverButton()->click();
        settle();
        stopAnsweringMessageBoxes();
        QVERIFY2(mBoxesAnswered == 1, "Start over cleared the typed summary without asking");
        QCOMPARE(fieldNamed(qsl("lineEdit_title"))->text(), typedTitle);
        QVERIFY2(startOverButton()->isEnabled(), "the declined form has words on it still, but the clear button is no longer available");

        answerNextMessageBox(QMessageBox::Yes);
        startOverButton()->click();
        settle();
        stopAnsweringMessageBoxes();
        QCOMPARE(mBoxesAnswered, 1);
        QVERIFY2(fieldNamed(qsl("lineEdit_title"))->text().isEmpty(), "answering the prompt left the typed summary standing");
        QVERIFY2(!startOverButton()->isEnabled(), "the cleared form still offers the clear button");

        // ...and the same rule reached through the picker, where declining has one
        // more thing to put back: the entry the form's words came from
        auto* pList = comboNamed(qsl("packageList"));
        const int at = rowOfThePackage();
        pList->setCurrentIndex(at);
        settle();
        auto* pDescription = descriptionField();
        const QString typedNotes = qsl("Paragraphs nobody else has a copy of");
        pDescription->setPlainText(qsl("%1\n\n%2").arg(pDescription->toPlainText(), typedNotes));
        settle();

        answerNextMessageBox(QMessageBox::No);
        pList->setCurrentIndex(0);
        settle();
        stopAnsweringMessageBoxes();
        QVERIFY2(mBoxesAnswered == 1, "choosing the first entry discarded the typed notes without asking");
        QCOMPARE(pList->currentIndex(), at);
        QVERIFY2(pDescription->toPlainText().contains(typedNotes), "declining still cleared the typed notes");
        QCOMPARE(fieldNamed(qsl("lineEdit_packageName"))->text(), mPackageName);
    }

    // The list of files to ship and the description above it share the one stretch
    // of room this column has. The list asks for every file plus an empty line and
    // takes it off the description; the description keeps four lines whatever the
    // list wants, and once it is down to those the list scrolls instead of growing.
    void test_theFileListGrowsWithItsRowsAndTheDescriptionGivesWay()
    {
        openExporter();
        QListWidget* pFiles = filesToShip();
        QTextEdit* pDescription = descriptionField();
        QVERIFY(pFiles && pDescription);
        // The fit is deferred, so the heights read below are the ones the first
        // real layout pass settled on rather than the .ui file's
        settle();
        QVERIFY(QTest::qWaitFor(
                [pFiles]() {
                    return pFiles->height() > 0;
                },
                2s));

        const int descriptionHeight0 = pDescription->height();
        const int listHeight0 = pFiles->height();
        QVERIFY2(descriptionHeight0 > 0 && listHeight0 > 0, "SETUP: neither flexing row has been laid out, so nothing can be measured off them");

        // Measured the way the dialog measures it, so this reads the rule rather
        // than a number written down twice
        const auto descriptionFloor = [pDescription]() {
            return QFontMetrics(pDescription->font()).lineSpacing() * 4 + qRound(2 * pDescription->document()->documentMargin()) + (pDescription->height() - pDescription->viewport()->height());
        };
        const auto listScrolls = [pFiles]() {
            return pFiles->verticalScrollBar()->maximum() > 0;
        };
        // setFixedHeight() resizes the list on the spot, while the column round it
        // is laid out again afterwards - so a height is only read once the rows
        // above it have moved to match
        const auto layoutSettles = [this]() {
            settle();
            settle();
        };
        const auto heightSettlesAt = [pFiles, layoutSettles](const int wanted) {
            const bool reached = QTest::qWaitFor(
                    [pFiles, wanted]() {
                        return pFiles->height() == wanted;
                    },
                    2s);
            layoutSettles();
            return reached && pFiles->height() == wanted;
        };

        int added = 0;
        const auto addAFile = [&added, pFiles]() {
            pFiles->addItem(qsl("/exporter-shell/ship-%1.png").arg(++added));
        };

        // One file: the zone's floor is two lines, so one file still fits the room
        // an empty zone had - and the empty line under it is on show, which is what
        // says the list would take another
        addAFile();
        settle();
        const int rowHeight = pFiles->sizeHintForRow(0);
        QVERIFY2(rowHeight > 0, "SETUP: a row of the list has no height, so nothing below can be counted in rows");
        QVERIFY(heightSettlesAt(listHeight0));
        QVERIFY2(!listScrolls(), qPrintable(qsl("one file in a zone %1px tall already scrolls, with a row standing %2px").arg(pFiles->height()).arg(rowHeight)));

        // From the second file on, every file is a row more of room, taken off the
        // description above
        addAFile();
        QVERIFY2(heightSettlesAt(listHeight0 + rowHeight),
                 qPrintable(qsl("two files left the zone at %1px rather than growing to %2px, so the second file is not shown").arg(pFiles->height()).arg(listHeight0 + rowHeight)));
        QVERIFY2(!listScrolls(), "two files in a zone grown to hold three rows already scrolls");
        QVERIFY2(pDescription->height() < descriptionHeight0, "the zone grew without the description giving up the room, so the column grew instead");

        // ...until the description is down to its four lines. Then the list stops
        // growing and scrolls, and the description never goes below that floor.
        int heightBefore = pFiles->height();
        int descriptionLow = pDescription->height();
        bool stopped = false;
        for (int more = 0; more < 30 && !stopped; ++more) {
            addAFile();
            layoutSettles();
            descriptionLow = std::min(descriptionLow, pDescription->height());
            QVERIFY2(pDescription->height() >= descriptionFloor(),
                     qPrintable(qsl("with %1 files the description is down to %2px, under the %3px four lines of it need").arg(added).arg(pDescription->height()).arg(descriptionFloor())));
            if (pFiles->height() == heightBefore) {
                stopped = true;
            } else {
                heightBefore = pFiles->height();
            }
        }
        QVERIFY2(stopped, qPrintable(qsl("the zone was still growing after %1 files, at %2px, so nothing holds it to the room the column has").arg(added).arg(pFiles->height())));
        QVERIFY2(descriptionLow >= descriptionFloor(), qPrintable(qsl("the description was squeezed to %1px on the way, under its %2px floor").arg(descriptionLow).arg(descriptionFloor())));
        QVERIFY2(listScrolls(),
                 qPrintable(qsl("the zone stopped growing at %1px with %2 files in it and does not scroll, so the files past the %3rd cannot be reached")
                                    .arg(pFiles->height())
                                    .arg(added)
                                    .arg(pFiles->height() / rowHeight)));

        // ...and the room comes back when the files go
        pFiles->clear();
        QVERIFY2(heightSettlesAt(listHeight0), qPrintable(qsl("the emptied zone stands at %1px rather than back at its two lines' %2px").arg(pFiles->height()).arg(listHeight0)));
        QVERIFY2(QTest::qWaitFor(
                         [pDescription, descriptionHeight0]() {
                             return pDescription->height() >= descriptionHeight0;
                         },
                         2s),
                 qPrintable(qsl("the description is left at %1px after the files went, rather than back at the %2px it opened with").arg(pDescription->height()).arg(descriptionHeight0)));
    }
};

#include "PackageExporterShellTest.moc"
MUDLET_GROUPED_TEST_MAIN(PackageExporterShellTest)
