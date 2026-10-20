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
 * The Mapper page's cards were grids whose columns took the width of their
 * longest member, so one long German label widened every row and held the
 * whole page past the reading column. Each row now flows: its pieces keep their
 * own widths and the trailing one drops to the next line only when that row
 * runs out of room. The six map view check boxes are two columns of three that
 * stay whole, side by side while both fit and one under the other when they do
 * not, and each map colour is a word and its well that move on together.
 *
 * Run with: ctest -R SettingsMapperPageLayoutTest -V
 */

#include <QDir>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QTranslator>

#include "FlowLayout.h"
#include "MudletApp.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "SettingsTestHelper.h"
#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "TelnetServerStub.h"
#include "dlgProfilePreferences.h"
#include "mudlet.h"

#include "GroupedTest.h"

class SettingsMapperPageLayoutTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    Host* mpHost = nullptr;
    dlgProfilePreferences* mpPreferences = nullptr;
    const QString mProfileName = qsl("SettingsMapperPageLayout-Test");
    QString mPort; // assigned the stub's actual ephemeral port in initTestCase()
    const QString mLocalhost = qsl("localhost");

    // Reports rather than asserting: a QVERIFY here would only return from this
    // helper, leaving the caller reading widgets off a dialog that never opened.
    bool openPreferences()
    {
        mpPreferences = new dlgProfilePreferences(mudlet::self(), mpHost);
        mpPreferences->resize(1060, 760);
        mpPreferences->show();
        if (!QTest::qWaitForWindowExposed(mpPreferences)) {
            return false;
        }
        mpPreferences->setTab(qsl("mapper"));
        qApp->processEvents();
        return true;
    }

    // What a card leaves round a row that spans it, so a test can hand the row a
    // width of its choosing by capping the card
    static int cardChrome(const QWidget* pCard, const QWidget* pRow) { return pCard->width() - pRow->width(); }

    // Centred on one line: an odd difference in height leaves a pixel to one side
    static bool levelWith(const QWidget* pOne, const QWidget* pOther) { return qAbs(pOne->geometry().center().y() - pOther->geometry().center().y()) <= 1; }

    // Puts back what a case's own setGeometry() on the layout moved, before the page is read again
    static void relayout(QLayout* pLayout)
    {
        pLayout->invalidate();
        pLayout->activate();
    }

private slots:
    void initTestCase()
    {
        if (portableMarkerPresent()) {
            QSKIP("portable.txt present - it takes precedence over XDG_CONFIG_HOME, so the config dir cannot be redirected");
        }

        // A config root of this process's own - see SettingsMapControlsTest
        QVERIFY(mConfigDir.isValid());
        QVERIFY(QDir().mkpath(qsl("%1/mudlet/profiles").arg(mConfigDir.path())));
        mSavedXdg = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", mConfigDir.path().toUtf8());

        mpServer = new TelnetServerStub(qApp);
        mpServer->start(mLocalhost, 0); // ephemeral OS-assigned port avoids collisions across concurrent test runs
        QVERIFY2(mpServer->serverPort() != 0, "the telnet stub did not start listening");
        mPort = QString::number(mpServer->serverPort());
        mudlet::start();
        mudlet::self()->setupConfig();
        QCOMPARE(MudletApp::getMudletPath(enums::mainPath), qsl("%1/mudlet").arg(mConfigDir.path()));
        mudlet::self()->takeOwnershipOfInstanceCoordinator(std::make_unique<MudletInstanceCoordinator>(qsl("MudletInstanceCoordinator")));
        mudlet::self()->init();
        mudlet::self()->setStorePasswordsSecurely(false);
        TestSettings::deleteProfileDirectory(mProfileName);

        mpHost = TestProfile::create(mProfileName, mLocalhost, mPort);
        QVERIFY2(mpHost, "No active host after profile creation");
    }

    void cleanupTestCase()
    {
        mpHost = nullptr;
        delete mpServer;
        mpServer = nullptr;
        // Null when initTestCase skipped or failed ahead of mudlet::start()
        if (mudlet::self()) {
            TestSettings::deleteProfileDirectory(mProfileName);
            delete mudlet::self();
        }
        mSavedXdg.isNull() ? qunsetenv("XDG_CONFIG_HOME") : qputenv("XDG_CONFIG_HOME", mSavedXdg);
    }

    void cleanup()
    {
        delete mpPreferences;
        mpPreferences = nullptr;
    }

    // Each column is as wide as its own longest words, not as the longest of
    // all six, and the second moves under the first whole the moment one pixel
    // is missing - never a check box at a time
    void test_theMapViewCheckBoxesAreTwoColumnsThatStackWhole()
    {
        QVERIFY2(openPreferences(), "the settings dialog never appeared");
        auto* pChoices = mpPreferences->findChild<QWidget*>(qsl("settingsMapViewChoices"));
        auto* pLeading = mpPreferences->findChild<QWidget*>(qsl("settingsMapViewLeadingChoices"));
        auto* pTrailing = mpPreferences->findChild<QWidget*>(qsl("settingsMapViewTrailingChoices"));
        QVERIFY2(pChoices && pLeading && pTrailing, "the Map view card has no row holding two columns of check boxes");
        auto* pFlow = dynamic_cast<uiDesign::FlowLayout*>(pChoices->layout());
        QVERIFY2(pFlow, "the two columns are not laid out by a flow, so the second cannot move under the first");

        const QList<QCheckBox*> leadingBoxes{mpPreferences->mMapperUseAntiAlias, mpPreferences->checkBox_drawUpperLowerLevels, mpPreferences->checkBox_invertMapZoom};
        const QList<QCheckBox*> trailingBoxes{mpPreferences->checkbox_mMapperShowRoomBorders, mpPreferences->checkBox_largeAreaExitArrows, mpPreferences->checkBox_showDefaultArea};
        for (const auto& [pGroup, boxes] : {std::pair{pLeading, leadingBoxes}, std::pair{pTrailing, trailingBoxes}}) {
            int widest = 0;
            for (auto* pBox : boxes) {
                QVERIFY2(pBox->parentWidget() == pGroup, qPrintable(qsl("'%1' is not in the '%2' column").arg(pBox->objectName(), pGroup->objectName())));
                if (pBox->isVisibleTo(pGroup)) {
                    widest = std::max(widest, pBox->sizeHint().width());
                }
            }
            QVERIFY2(qAbs(pGroup->width() - widest) <= 2,
                     qPrintable(qsl("the '%1' column is %2px wide for check boxes whose widest is %3px").arg(pGroup->objectName()).arg(pGroup->width()).arg(widest)));
        }

        // Exactly the room both need, and then one pixel less
        const int both = pLeading->sizeHint().width() + pFlow->horizontalSpacing() + pTrailing->sizeHint().width();
        pFlow->setGeometry(QRect(0, 0, both, pFlow->heightForWidth(both)));
        QVERIFY2(pTrailing->geometry().left() > pLeading->geometry().right(),
                 qPrintable(qsl("given the %1px both columns need, the second starts at %2px, inside the first, which ends at %3px")
                                    .arg(both)
                                    .arg(pTrailing->geometry().left())
                                    .arg(pLeading->geometry().right())));
        QCOMPARE(pTrailing->geometry().top(), pLeading->geometry().top());
        pFlow->setGeometry(QRect(0, 0, both - 1, pFlow->heightForWidth(both - 1)));
        QVERIFY2(pTrailing->geometry().top() > pLeading->geometry().bottom(),
                 qPrintable(qsl("one pixel short of the room both columns need, the second starts at %1px, beside the first rather than under it").arg(pTrailing->geometry().top())));
        QCOMPARE(pTrailing->geometry().left(), pLeading->geometry().left());

        // ...and the card gives the row its whole width, so the page decides by
        // the same rule
        relayout(pFlow);
        qApp->processEvents();
        QGroupBox* pCard = mpPreferences->groupBox_mapViewOptions;
        QCOMPARE(pChoices->width(), pCard->contentsRect().width());
        pCard->setMaximumWidth(cardChrome(pCard, pChoices) + both - 1);
        qApp->processEvents();
        QCOMPARE(pChoices->width(), both - 1);
        QVERIFY2(pTrailing->geometry().top() > pLeading->geometry().bottom(), "a card one pixel too narrow for both columns keeps them side by side");
        QCOMPARE(pTrailing->geometry().left(), pLeading->geometry().left());
    }

    // A row keeps every piece at the width its contents need and breaks after
    // the last piece that still fits, rather than squeezing or stretching any
    void test_aMapFileRowBreaksAfterTheLastControlThatFits()
    {
        QVERIFY2(openPreferences(), "the settings dialog never appeared");
        auto* pRow = mpPreferences->findChild<QWidget*>(qsl("settingsMapperRow_loadHistoricMap"));
        QVERIFY2(pRow, "the Map files card has no row for loading an older map");
        auto* pFlow = dynamic_cast<uiDesign::FlowLayout*>(pRow->layout());
        QVERIFY2(pFlow, "the older-map row is not laid out by a flow");
        QLabel* pLabel = mpPreferences->label_loadHistoricMap;
        QComboBox* pCombo = mpPreferences->comboBox_mapHistory;
        QPushButton* pButton = mpPreferences->pushButton_loadHistoricMap;
        for (QWidget* pPiece : std::initializer_list<QWidget*>{pLabel, pCombo, pButton}) {
            QVERIFY2(pPiece->parentWidget() == pRow, qPrintable(qsl("'%1' is not on the older-map row").arg(pPiece->objectName())));
        }

        const int gap = pFlow->horizontalSpacing();
        const int oneLine = pLabel->width() + gap + pCombo->width() + gap + pButton->width();
        QVERIFY2(pRow->width() >= oneLine, qPrintable(qsl("the card gives the row %1px, less than the %2px it needs on one line, so the wide case cannot be shown").arg(pRow->width()).arg(oneLine)));
        QVERIFY2(levelWith(pCombo, pLabel) && levelWith(pButton, pLabel), "the older-map row's label, list and button are not level on one line");
        QVERIFY(pCombo->geometry().left() > pLabel->geometry().right());
        QVERIFY(pButton->geometry().left() > pCombo->geometry().right());
        const int comboWidth = pCombo->width();
        QCOMPARE(comboWidth, pCombo->sizeHint().width());
        QCOMPARE(mpPreferences->comboBox_mapFileSaveFormatVersion->width(), mpPreferences->comboBox_mapFileSaveFormatVersion->sizeHint().width());

        QGroupBox* pCard = mpPreferences->groupBox_mapFiles;
        const int narrow = cardChrome(pCard, pRow) + oneLine - 1;
        QVERIFY2(pCard->minimumSizeHint().width() <= narrow,
                 qPrintable(qsl("the Map files card cannot be narrowed below %1px, so the row cannot be shown one pixel short of one line").arg(pCard->minimumSizeHint().width())));
        pCard->setMaximumWidth(narrow);
        qApp->processEvents();
        QCOMPARE(pRow->width(), oneLine - 1);
        QVERIFY2(levelWith(pCombo, pLabel), "the list left the line of the label it belongs to");
        QVERIFY2(pButton->geometry().top() > pCombo->geometry().bottom(),
                 qPrintable(qsl("one pixel short of one line, the load button is at %1px, beside the list rather than under it").arg(pButton->geometry().top())));
        QCOMPARE(pButton->geometry().left(), pLabel->geometry().left());
        QCOMPARE(pCombo->width(), comboWidth);
    }

    // The player room marker's style is a row like the Map files rows: its
    // list beside its label while both fit, and under it once they do not
    void test_thePlayerRoomStyleListDropsUnderItsLabel()
    {
        QVERIFY2(openPreferences(), "the settings dialog never appeared");
        auto* pRow = mpPreferences->findChild<QWidget*>(qsl("settingsMapperRow_playerRoomStyle"));
        QVERIFY2(pRow, "the Player room marker card has no row for the marker style");
        auto* pFlow = dynamic_cast<uiDesign::FlowLayout*>(pRow->layout());
        QVERIFY2(pFlow, "the marker style row is not laid out by a flow");
        QLabel* pLabel = mpPreferences->label_playerRoomStyle;
        QComboBox* pCombo = mpPreferences->comboBox_playerRoomStyle;
        QVERIFY2(pLabel->parentWidget() == pRow && pCombo->parentWidget() == pRow, "the marker style's label and list are not two pieces of the one row");

        const int oneLine = pLabel->width() + pFlow->horizontalSpacing() + pCombo->width();
        QVERIFY2(pRow->width() >= oneLine, qPrintable(qsl("the card gives the row %1px, less than the %2px it needs on one line, so the wide case cannot be shown").arg(pRow->width()).arg(oneLine)));
        QVERIFY2(levelWith(pCombo, pLabel), "the marker style's list is not level with its label");
        QVERIFY(pCombo->geometry().left() > pLabel->geometry().right());
        const int comboWidth = pCombo->width();

        QGroupBox* pCard = mpPreferences->groupBox_playerRoomStyle;
        const int narrow = cardChrome(pCard, pRow) + oneLine - 1;
        QVERIFY2(pCard->minimumSizeHint().width() <= narrow,
                 qPrintable(qsl("the Player room marker card cannot be narrowed below %1px, so the row cannot be shown one pixel short of one line").arg(pCard->minimumSizeHint().width())));
        pCard->setMaximumWidth(narrow);
        qApp->processEvents();
        QCOMPARE(pRow->width(), oneLine - 1);
        QVERIFY2(pCombo->geometry().top() > pLabel->geometry().bottom(),
                 qPrintable(qsl("one pixel short of one line, the marker style list is at %1px, beside its label rather than under it").arg(pCombo->geometry().top())));
        QCOMPARE(pCombo->geometry().left(), pLabel->geometry().left());
        QCOMPARE(pCombo->width(), comboWidth);
    }

    // Each size is read with its own stepper, so the two stay on one line and a
    // pair moves on whole; as many pairs as fit share a line
    void test_eachFeatureSizeStaysWithItsStepper()
    {
        QVERIFY2(openPreferences(), "the settings dialog never appeared");
        auto* pSizes = mpPreferences->findChild<QWidget*>(qsl("settingsMapperFeatureSizes"));
        QVERIFY2(pSizes, "the Feature sizes group has no row of sizes");
        auto* pFlow = dynamic_cast<uiDesign::FlowLayout*>(pSizes->layout());
        QVERIFY2(pFlow && pFlow->count() == 4, "the four feature sizes are not four pieces of one flow");

        const QList<std::pair<QWidget*, QWidget*>> pairs{{mpPreferences->label_roomSize, mpPreferences->spinBox_roomSize},
                                                         {mpPreferences->label_exitSize, mpPreferences->spinBox_exitSize},
                                                         {mpPreferences->label_borderSize, mpPreferences->spinBox_borderSize},
                                                         {mpPreferences->label_gridSize, mpPreferences->doubleSpinBox_gridSize}};
        QList<QWidget*> pieces;
        for (const auto& [pLabel, pStepper] : pairs) {
            QWidget* pPiece = pLabel->parentWidget();
            QVERIFY2(pStepper->parentWidget() == pPiece && pPiece->parentWidget() == pSizes,
                     qPrintable(qsl("'%1' and '%2' are not one piece of the sizes row").arg(pLabel->objectName(), pStepper->objectName())));
            pieces.append(pPiece);
        }

        // Room for the first two pairs and not the third
        const int two = pieces.at(0)->sizeHint().width() + pFlow->horizontalSpacing() + pieces.at(1)->sizeHint().width();
        pFlow->setGeometry(QRect(0, 0, two, pFlow->heightForWidth(two)));
        QCOMPARE(pieces.at(1)->geometry().top(), pieces.at(0)->geometry().top());
        QVERIFY2(pieces.at(2)->geometry().top() > pieces.at(0)->geometry().bottom(), "a third pair shares a line that only has room for two");
        QCOMPARE(pieces.at(2)->geometry().left(), pieces.at(0)->geometry().left());
        for (const auto& [pLabel, pStepper] : pairs) {
            QVERIFY2(levelWith(pStepper, pLabel), qPrintable(qsl("'%1' is not level with its stepper").arg(pLabel->objectName())));
            QVERIFY(pStepper->geometry().left() > pLabel->geometry().right());
        }
    }

    // A colour is a word and its well, one piece that never splits, and the
    // pieces flow like the rows above: side by side while they fit, the next
    // pair on the next line when they do not, each as wide as it was
    void test_aColourStaysWithItsWordAndThePairsFlow()
    {
        QVERIFY2(openPreferences(), "the settings dialog never appeared");
        auto* pColours = mpPreferences->findChild<QWidget*>(qsl("settingsMapAnsiColours"));
        QVERIFY2(pColours, "the Map colors card has no row of the sixteen colours");
        auto* pFlow = dynamic_cast<uiDesign::FlowLayout*>(pColours->layout());
        QVERIFY2(pFlow && pFlow->count() == 16, "the sixteen colours are not sixteen pieces of one flow");

        const QList<std::pair<QLabel*, QPushButton*>> firstTwo{{mpPreferences->label_17, mpPreferences->pushButton_black_2}, {mpPreferences->label_49, mpPreferences->pushButton_Lblack_2}};
        QList<QWidget*> pairs;
        QList<int> widths;
        for (const auto& [pLabel, pWell] : firstTwo) {
            QWidget* pPair = pWell->parentWidget();
            QVERIFY2(pLabel->parentWidget() == pPair && pPair->parentWidget() == pColours,
                     qPrintable(qsl("'%1' and '%2' are not one piece of the colours row").arg(pLabel->objectName(), pWell->objectName())));
            QVERIFY2(levelWith(pWell, pLabel) && pWell->geometry().left() > pLabel->geometry().right(), qPrintable(qsl("'%1' is not level with the well after it").arg(pLabel->objectName())));
            pairs.append(pPair);
            widths.append(pPair->width());
        }

        const int both = widths.at(0) + pFlow->horizontalSpacing() + widths.at(1);
        QVERIFY2(pColours->width() >= both, qPrintable(qsl("the card gives the colours %1px, less than the %2px two pairs need, so the wide case cannot be shown").arg(pColours->width()).arg(both)));
        QCOMPARE(pairs.at(1)->geometry().top(), pairs.at(0)->geometry().top());
        QVERIFY(pairs.at(1)->geometry().left() > pairs.at(0)->geometry().right());

        pFlow->setGeometry(QRect(0, 0, both - 1, pFlow->heightForWidth(both - 1)));
        QVERIFY2(pairs.at(1)->geometry().top() > pairs.at(0)->geometry().bottom(), "one pixel short of the room two pairs need, the second still shares the first one's line");
        QCOMPARE(pairs.at(1)->geometry().left(), pairs.at(0)->geometry().left());
        for (int index = 0; index < pairs.size(); ++index) {
            QCOMPARE(pairs.at(index)->width(), widths.at(index));
        }
        relayout(pFlow);
    }

    // However far the window is narrowed, the page fits beside the sidebar or
    // its rail; walked from the widest the window goes to the narrowest
    void test_theMapperPageNeverScrollsSideways()
    {
        QVERIFY2(openPreferences(), "the settings dialog never appeared");
        QScrollArea* pPage = TestSettings::pageOf(mpPreferences, qsl("mapper"));
        QVERIFY2(pPage && pPage->widget(), "there is no Mapper page");
        mpPreferences->resize(mpPreferences->maximumWidth(), 760);
        qApp->processEvents();
        const int widest = mpPreferences->width();
        const int narrowest = mpPreferences->minimumWidth();
        for (int width = widest; width > narrowest - 8; width -= 8) {
            mpPreferences->resize(std::max(width, narrowest), 760);
            qApp->processEvents();
            // Waited on rather than read once: in the one pass in which the sidebar folds, the page has been
            // squeezed and not yet given the room back, and the bar is on show until the next
            QTRY_VERIFY2_WITH_TIMEOUT(!pPage->horizontalScrollBar()->isVisible(), qPrintable(qsl("the Mapper page scrolls sideways in a %1px window").arg(mpPreferences->width())), 1000);
            QVERIFY2(pPage->widget()->geometry().right() < pPage->viewport()->width(),
                     qPrintable(qsl("the Mapper page's cards end at %1px in a %2px viewport, in a %3px window")
                                        .arg(pPage->widget()->geometry().right() + 1)
                                        .arg(pPage->viewport()->width())
                                        .arg(mpPreferences->width())));
        }
    }

    // The case the flow was made for: German at the 13pt macOS reads the
    // interface at, where the page was 1001px wide
    void test_theMapperPageFitsTheReadingColumnInGermanAt13pt()
    {
#if !defined(Q_OS_MACOS)
        QSKIP("the 13pt interface font this case measures is macOS's own; elsewhere the family falls back to one whose widths say nothing about it");
#endif
        auto* pGerman = new QTranslator(qApp);
        QVERIFY2(pGerman->load(qsl("mudlet_de_DE"), qsl(":/lang")), "no German translation in the binary's resources, so nothing here is long enough to measure");
        QVERIFY(qApp->installTranslator(pGerman));
        const QFont priorFont = QApplication::font();
        QFont systemFont(qsl(".AppleSystemUIFont"));
        systemFont.setPointSize(13);
        QApplication::setFont(systemFont);
        auto restore = qScopeGuard([pGerman, priorFont]() {
            QApplication::setFont(priorFont);
            qApp->removeTranslator(pGerman);
            delete pGerman;
        });
        QVERIFY2(openPreferences(), "the settings dialog never appeared");

        // Card by card first, so a failure names the card that is too wide
        for (QGroupBox* pCard : {mpPreferences->groupBox_mapFiles,
                                 mpPreferences->groupBox_downloadMapOptions,
                                 mpPreferences->groupBox_mapViewOptions,
                                 mpPreferences->groupBox_mapperColors,
                                 mpPreferences->groupBox_playerRoomStyle}) {
            QVERIFY2(pCard->minimumSizeHint().width() <= 640,
                     qPrintable(qsl("the German '%1' card needs %2px at 13pt, past the 640px reading column").arg(pCard->title()).arg(pCard->minimumSizeHint().width())));
        }
        QWidget* pColumn = TestSettings::pageOf(mpPreferences, qsl("mapper"))->widget();
        QVERIFY2(pColumn->minimumSizeHint().width() <= 640, qPrintable(qsl("the German Mapper page needs %1px at 13pt, past the 640px reading column").arg(pColumn->minimumSizeHint().width())));
    }
};

#include "SettingsMapperPageLayoutTest.moc"
MUDLET_GROUPED_TEST_MAIN(SettingsMapperPageLayoutTest)
