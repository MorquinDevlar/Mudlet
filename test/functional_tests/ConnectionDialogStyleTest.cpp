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
 * The connection dialog's two tabs and its buttons are drawn from the design
 * language: the fields, the marks and the labels on the tabs come from the
 * shared recipes rather than from the platform, and a field the validator has
 * something to say about is marked with a property a rule paints from - not
 * with a palette, which a stylesheet's own background-color would beat anyway.
 *
 * The buttons are the same recipe on both containers - Remove, Copy and New
 * over the games list, and the skip button and the button box under it - with
 * Copy staying a tool button, since its trailing half opens a menu, and
 * Connect drawn as the one invitation on the row.
 *
 * Run with: ctest -R ConnectionDialogStyleTest -V
 */

#include "PortableModeTestHelper.h"
#include "MudletInstanceCoordinator.h"
#include "dlgConnectionProfiles.h"
#include "mudlet.h"
#include "uiDesign.h"

#include <QtTest/QtTest>

#include <QFontMetrics>
#include <QImage>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QStyleOptionToolButton>
#include <QTabWidget>
#include <QToolButton>

#include "GroupedTest.h"

using namespace std::chrono_literals;

class ConnectionDialogStyleTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mXdgDir;
    QByteArray mSavedXdg;

    const QString mProfile = qsl("Aardvark-ConnDialogFieldStyle");

    // a saved profile keeps the first-launch tutorial invitation - which hides
    // the whole connection form - out of the way
    bool makeProfileFolder(const QString& name) const { return QDir().mkpath(qsl("%1/mudlet/profiles/%2").arg(mXdgDir.path(), name)); }

    dlgConnectionProfiles* dialog() const { return mudlet::self()->mpConnectionDialog.data(); }

    // The fields only carry text, and the validator only runs, once a profile
    // is the one being edited
    void selectTheTestProfile() const
    {
        auto* pDialog = dialog();
        const auto items = pDialog->findData(*pDialog->listWidget_profiles, mProfile, dlgConnectionProfiles::csmNameRole);
        QVERIFY2(!items.isEmpty(), "The test profile is missing from the games list");
        pDialog->listWidget_profiles->setCurrentItem(items.first());
        QCoreApplication::processEvents();
    }

    static QString fieldState(const QLineEdit* pField) { return pField->property("connectionFieldState").toString(); }

    // How far a reading may be from the hairline it should be and still be that
    // hairline: a rounded edge is antialiased against what is behind it, and a
    // screen that doubles its pixels draws the one line over two rows
    static constexpr int scmHairlineInkSlack = 24;

private slots:
    void initTestCase()
    {
        if (portableMarkerPresent()) {
            QSKIP("portable.txt present - cannot redirect the config dir for this test");
        }

        mSavedXdg = qgetenv("XDG_CONFIG_HOME");
        QVERIFY(mXdgDir.isValid());
        QVERIFY(QDir().mkpath(qsl("%1/mudlet/profiles").arg(mXdgDir.path()))); // profiles/ = XDG opt-in
        QVERIFY(makeProfileFolder(mProfile));
        qputenv("XDG_CONFIG_HOME", mXdgDir.path().toUtf8());

        mudlet::start();
        mudlet::self()->setupConfig();
        QVERIFY(mudlet::getMudletPath(enums::profilesPath).startsWith(mXdgDir.path()));
        mudlet::self()->takeOwnershipOfInstanceCoordinator(std::make_unique<MudletInstanceCoordinator>("MudletInstanceCoordinator"));
        mudlet::self()->init();
        mudlet::self()->setStorePasswordsSecurely(false);

        mudlet::self()->startAutoLogin({});
        // the dialog is only shown from a queued lambda, so the pointer turning
        // up is not enough
        QVERIFY(QTest::qWaitFor(
                []() {
                    return mudlet::self()->mpConnectionDialog && mudlet::self()->mpConnectionDialog->isVisible();
                },
                5000));
    }

    void cleanupTestCase()
    {
        mSavedXdg.isNull() ? qunsetenv("XDG_CONFIG_HOME") : qputenv("XDG_CONFIG_HOME", mSavedXdg);
        delete mudlet::self();
    }

    void test_theTabsFieldsAreDrawnFromTheDesign()
    {
        auto* pDialog = dialog();
        QVERIFY2(pDialog, "No connection dialog to test against");

        const QString field = uiDesign::themeTokens().field.name();
        QVERIFY2(pDialog->tab_connection_info->styleSheet().contains(field),
                 qPrintable(qsl("the Connect-to tab is not drawn on the design's field surface (%1): its sheet is \"%2\"").arg(field, pDialog->tab_connection_info->styleSheet().left(120))));
        QVERIFY2(pDialog->tab_options->styleSheet().contains(field), "the Options tab is not drawn on the design's field surface");

        // The .ui pinned these three at 9pt, which read smaller than the very
        // same controls on the Options tab beside them
        for (const QLineEdit* pField : {pDialog->profile_name_entry, pDialog->host_name_entry, pDialog->port_entry}) {
            QVERIFY2(pField->font().pointSize() == pDialog->font().pointSize(),
                     qPrintable(qsl("%1 is drawn at %2pt against the dialog's own %3pt")
                                        .arg(pField->objectName(), QString::number(pField->font().pointSize()), QString::number(pDialog->font().pointSize()))));
        }

        // Five digits and what the field recipe leaves either side of them: the
        // widest port there is has to fit without eliding
        const QFontMetrics fm(pDialog->port_entry->font());
        const int leastNeeded = fm.horizontalAdvance(qsl("65535")) + 2 * (uiDesign::scmInputPaddingHorizontal + uiDesign::scmInputBorderWidth);
        QVERIFY2(pDialog->port_entry->minimumWidth() >= leastNeeded,
                 qPrintable(qsl("the port field is %1px wide, against the %2px five digits need in it").arg(QString::number(pDialog->port_entry->minimumWidth()), QString::number(leastNeeded))));
    }

    // Every button on the window is the shared recipe: the three over the games
    // list and the row under it, with Copy staying a tool button because its
    // trailing half opens a menu, and Connect the one invitation among them
    void test_theButtonsAreDrawnFromTheDesign()
    {
        auto* pDialog = dialog();
        QVERIFY2(pDialog, "No connection dialog to test against");
        selectTheTestProfile();
        // The validator is what lets Connect out of its disabled state, and the
        // primary fill is only claimed by an enabled default button
        pDialog->host_name_entry->setText(qsl("mudlet.org"));
        pDialog->port_entry->setText(qsl("23"));
        QCoreApplication::processEvents();
        QTest::qWait(50ms);

        for (const QWidget* pContainer : {static_cast<QWidget*>(pDialog->profileAdminArea), static_cast<QWidget*>(pDialog->widget_bottom)}) {
            QVERIFY2(
                    pContainer->styleSheet().contains(qsl("QPushButton")),
                    qPrintable(qsl("%1 carries no push button rules, so its buttons are still the platform's: its sheet is \"%2\"").arg(pContainer->objectName(), pContainer->styleSheet().left(120))));
        }

        QVERIFY2(pDialog->copy_profile_toolbutton->property(uiDesign::scmProp_menuButton).toBool(),
                 "the Copy button does not say it is a tool button standing on a form as a button, so no rule reaches it");
        QVERIFY2(pDialog->profileAdminArea->styleSheet().contains(qsl("QToolButton[%1").arg(QString::fromLatin1(uiDesign::scmProp_menuButton))),
                 "the admin row's sheet draws push buttons only, so the Copy button is left to the platform");
        QCOMPARE(pDialog->copy_profile_toolbutton->popupMode(), QToolButton::MenuButtonPopup);
        // Both actions are still on the button itself - that is what makes it
        // the widget they are associated with, which is where the accessible
        // names are set - even though the menu is what they are chosen from
        // now. Named rather than counted: setMenu() puts the menu's own action
        // on the button as well.
        QStringList carried;
        for (const QAction* pAction : pDialog->copy_profile_toolbutton->actions()) {
            carried << pAction->objectName();
        }
        QVERIFY2(carried.contains(qsl("copyProfile")) && carried.contains(qsl("copyProfileSettingsOnly")),
                 qPrintable(qsl("the Copy button no longer carries both of its actions, so the accessible names set on their widget have nowhere to land: it holds %1").arg(carried.join(qsl(", ")))));

        // The split is still a split: the style is asked where the half that
        // opens the menu is, and it has to be a real rectangle at the trailing
        // end of the button rather than the nothing a sheet that took the
        // sub-control away would leave. Asked rather than clicked - a popup
        // menu on macOS runs an event loop of its own that a test cannot get
        // back out of.
        QStyleOptionToolButton copyOption;
        copyOption.initFrom(pDialog->copy_profile_toolbutton);
        copyOption.rect = pDialog->copy_profile_toolbutton->rect();
        copyOption.features = QStyleOptionToolButton::Menu | QStyleOptionToolButton::MenuButtonPopup;
        copyOption.subControls = QStyle::SC_ToolButton | QStyle::SC_ToolButtonMenu;
        copyOption.text = pDialog->copy_profile_toolbutton->text();
        const QRect menuHalf = pDialog->copy_profile_toolbutton->style()->subControlRect(QStyle::CC_ToolButton, &copyOption, QStyle::SC_ToolButtonMenu, pDialog->copy_profile_toolbutton);
        qInfo().noquote()
                << qsl("  the Copy button is %1px wide and hands its trailing %2px to the menu, at x %3").arg(pDialog->copy_profile_toolbutton->width()).arg(menuHalf.width()).arg(menuHalf.left());
        QVERIFY2(menuHalf.width() > 0 && menuHalf.height() > 0, "the Copy button hands no rectangle at all to its menu, so the split is gone");
        QVERIFY2(menuHalf.left() > pDialog->copy_profile_toolbutton->width() / 2, "the half that opens the Copy menu is not the trailing one");

        auto* pConnect = pDialog->findChild<QPushButton*>(qsl("connectButton"));
        QVERIFY2(pConnect, "the Connect button is not there to be read");
        QVERIFY2(pConnect->isDefault(), "Connect is not the dialog's default button, so Return no longer reaches it");
        QVERIFY2(pDialog->widget_bottom->styleSheet().contains(qsl(":default")), "the button row's sheet says nothing about the default button, so Connect reads as one control among four");

        // The design's hairline round the New button, read off a shot of the
        // window rather than off the button alone: a platform bevel is a
        // gradient of its own and lands nowhere near this tone. The middle of
        // the top edge is a straight run of the line rather than the corner it
        // is rounded through.
        QPushButton* pNew = pDialog->new_profile_button;
        QVERIFY2(pNew->isVisible() && pNew->width() > 8, "the New button is not on show to be measured");
        const QImage shot = pDialog->grab().toImage();
        const qreal shotRatio = shot.devicePixelRatio();
        const QPoint topMiddle = pNew->mapTo(pDialog, QPoint(pNew->width() / 2, 0));
        const QColor border = uiDesign::themeTokens().border;
        int nearest = 3 * 255;
        QColor read = border;
        for (int row = 0, rows = qMax(1, qRound(shotRatio)); row < rows; ++row) {
            const QPoint inShot(qRound(topMiddle.x() * shotRatio), qRound(topMiddle.y() * shotRatio) + row);
            if (!shot.rect().contains(inShot)) {
                continue;
            }
            const QColor sample = shot.pixelColor(inShot);
            const int distance = std::abs(sample.red() - border.red()) + std::abs(sample.green() - border.green()) + std::abs(sample.blue() - border.blue());
            if (distance < nearest) {
                nearest = distance;
                read = sample;
            }
        }
        qInfo().noquote() << qsl("  the New button's top edge reads %1 against a hairline of %2, %3 away").arg(read.name(), border.name()).arg(nearest);
        QVERIFY2(nearest <= scmHairlineInkSlack, qPrintable(qsl("the New button's outline reads %1 rather than the design's %2").arg(read.name(), border.name())));
    }

    // Copy is two click areas, and the seam down the leading edge of its
    // trailing half is what says so before the pointer arrives. The same
    // recipe draws the editor toolbar's Save Profile button, so a split button
    // is one control kind in both windows rather than two that happen to agree.
    void test_theCopyButtonShowsItsTwoHalves()
    {
        auto* pDialog = dialog();
        QVERIFY2(pDialog, "No connection dialog to test against");
        selectTheTestProfile();
        QCoreApplication::processEvents();

        QToolButton* pButton = pDialog->copy_profile_toolbutton;
        QVERIFY2(pButton->isVisible() && pButton->width() > 3 * uiDesign::scmInputDropDownWidth,
                 qPrintable(qsl("the Copy button is %1px wide and %2on show, which is nothing to read").arg(pButton->width()).arg(pButton->isVisible() ? QString() : qsl("not "))));

        const QString sheet = pDialog->profileAdminArea->styleSheet();
        const int arrowAt = sheet.indexOf(qsl("::menu-arrow"));
        QVERIFY2(arrowAt >= 0, "the admin row's sheet says nothing about the chevron on the menu half, so the platform's own mark is still on it");
        QVERIFY2(sheet.mid(arrowAt, 80).contains(qsl("image:")), qPrintable(qsl("the menu half's chevron is named without a picture: \"%1\"").arg(sheet.mid(arrowAt, 80))));
        QVERIFY2(sheet.contains(qsl("::menu-button:hover")), "the menu half has no hover of its own, so it lights only as part of the button");

        QStyleOptionToolButton option;
        option.initFrom(pButton);
        option.rect = pButton->rect();
        option.text = pButton->text();
        option.icon = pButton->icon();
        option.iconSize = pButton->iconSize();
        option.toolButtonStyle = pButton->toolButtonStyle();
        option.features = QStyleOptionToolButton::Menu | QStyleOptionToolButton::MenuButtonPopup;
        option.subControls = QStyle::SC_ToolButton | QStyle::SC_ToolButtonMenu;
        const QRect half = pButton->style()->subControlRect(QStyle::CC_ToolButton, &option, QStyle::SC_ToolButtonMenu, pButton);
        QVERIFY2(half.width() > 4 && half.height() > 8, qPrintable(qsl("the style hands the menu a %1x%2 rectangle, which is nothing to read").arg(half.width()).arg(half.height())));

        const QImage shot = pDialog->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        const qreal ratio = shot.devicePixelRatio();
        const QPoint corner = pButton->mapTo(pDialog, half.topLeft());
        const int seamAt = qRound(corner.x() * ratio);
        const int top = qRound(corner.y() * ratio);
        const int height = qRound(half.height() * ratio);
        QVERIFY2(shot.rect().contains(QPoint(seamAt, top + height / 2)), "the Copy button's menu half does not land inside the grab of the window");

        const QColor border = uiDesign::themeTokens().border;
        int onTheLine = 0;
        int rows = 0;
        QColor readAs;
        for (int y = top + height / 5; y <= top + height - height / 5; ++y) {
            if (!shot.rect().contains(QPoint(seamAt, y))) {
                continue;
            }
            ++rows;
            const QColor sample = shot.pixelColor(seamAt, y);
            if (!readAs.isValid()) {
                readAs = sample;
            }
            const int distance = std::abs(sample.red() - border.red()) + std::abs(sample.green() - border.green()) + std::abs(sample.blue() - border.blue());
            if (distance <= scmHairlineInkSlack) {
                ++onTheLine;
            }
        }
        const QColor justInside = shot.pixelColor(seamAt + qMax(3, qRound(3 * ratio)), top + height / 3);
        const int insideDistance = std::abs(justInside.red() - border.red()) + std::abs(justInside.green() - border.green()) + std::abs(justInside.blue() - border.blue());
        qInfo().noquote() << qsl("  the Copy button's menu half starts at x %1; its leading edge reads %2 against the hairline's %3 on %4 of %5 rows, and %6 a few pixels in")
                                     .arg(half.left())
                                     .arg(readAs.name(), border.name())
                                     .arg(onTheLine)
                                     .arg(rows)
                                     .arg(justInside.name());
        QVERIFY2(rows > 2 && onTheLine * 5 >= rows * 4,
                 qPrintable(qsl("the half's leading edge carries no seam: %1 of %2 rows read the design's hairline of %3, the first of them %4")
                                    .arg(onTheLine)
                                    .arg(rows)
                                    .arg(border.name(), readAs.name())));
        QVERIFY2(insideDistance > scmHairlineInkSlack,
                 qPrintable(qsl("the half is filled with the hairline's own tone (%1 a few pixels in), so what was read at its edge is a fill rather than a seam").arg(justInside.name())));
    }

    void test_aBadPortMarksTheFieldAndAGoodOneClearsIt()
    {
        auto* pDialog = dialog();
        QVERIFY2(pDialog, "No connection dialog to test against");
        selectTheTestProfile();

        // so that the port is the only thing the validator can object to
        pDialog->host_name_entry->setText(qsl("mudlet.org"));
        QCoreApplication::processEvents();

        // the non-digit branch chops the last character off and marks the field
        pDialog->port_entry->setText(qsl("12a"));
        QCoreApplication::processEvents();
        QCOMPARE(fieldState(pDialog->port_entry), qsl("error"));

        pDialog->port_entry->setText(qsl("23"));
        QCoreApplication::processEvents();
        QVERIFY2(fieldState(pDialog->port_entry).isEmpty(), qPrintable(qsl("a port of 23 left the field marked \"%1\"").arg(fieldState(pDialog->port_entry))));
    }

    void test_anEmptyServerAddressMarksTheField()
    {
        auto* pDialog = dialog();
        QVERIFY2(pDialog, "No connection dialog to test against");
        selectTheTestProfile();
        pDialog->port_entry->setText(qsl("23"));
        QCoreApplication::processEvents();

        // Whitespace rather than an empty field: slot_updateUrl() takes an
        // empty one as the user still typing and never reaches the validator,
        // so a wholly empty field is not the state this rule can be seen in.
        // Trimmed, this is the same nothing to the validator.
        pDialog->host_name_entry->setText(qsl(" "));
        QCoreApplication::processEvents();
        QCOMPARE(fieldState(pDialog->host_name_entry), qsl("error"));

        pDialog->host_name_entry->setText(qsl("mudlet.org"));
        QCoreApplication::processEvents();
        QVERIFY2(fieldState(pDialog->host_name_entry).isEmpty(), qPrintable(qsl("a server address left the field marked \"%1\"").arg(fieldState(pDialog->host_name_entry))));
    }

    // Copy's two answers hang off a menu of its own rather than off the
    // temporary one Qt builds out of the button's actions on every press: a
    // menu that exists between presses is one the style pass can reach, and it
    // is drawn as the design's menu - the card surface, the hairline and the
    // corner a combo box's list is opened at.
    void test_theCopyMenuIsDrawnFromTheDesign()
    {
        auto* pDialog = dialog();
        QVERIFY2(pDialog, "No connection dialog to test against");

        QMenu* pMenu = pDialog->copy_profile_toolbutton->menu();
        QVERIFY2(pMenu, "the Copy button carries no menu of its own, so nothing the style pass writes can reach one");
        const QList<QAction*> rows = pMenu->actions();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0)->objectName(), qsl("copyProfile"));
        QCOMPARE(rows.at(1)->objectName(), qsl("copyProfileSettingsOnly"));

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QString sheet = pDialog->profileAdminArea->styleSheet();
        QVERIFY2(sheet.contains(qsl("QMenu")), qPrintable(qsl("the admin row's sheet says nothing about a QMenu, so Copy's menu is the platform's: its sheet is \"%1\"").arg(sheet.left(120))));
        QVERIFY2(sheet.contains(tokens.card.name()), qPrintable(qsl("the admin row's sheet never names the card surface (%1) the menu is drawn on").arg(tokens.card.name())));
        QVERIFY2(pMenu->testAttribute(Qt::WA_TranslucentBackground), "the Copy menu's window was never asked to be see-through, so its corner is opaque");

        // Popped up rather than clicked: a menu run through exec() takes an
        // event loop of its own that a case cannot get back out of
        pMenu->popup(pDialog->copy_profile_toolbutton->mapToGlobal(QPoint(0, pDialog->copy_profile_toolbutton->height())));
        QTest::qWaitForWindowExposed(pMenu);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        const QImage shot = pMenu->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        pMenu->hide();
        QCoreApplication::processEvents();

        const QColor corner = QColor::fromRgba(shot.pixel(0, 0));
        const QColor surface = QColor::fromRgba(shot.pixel(qRound(2 * shot.devicePixelRatio()), shot.height() / 2));
        QVERIFY2(corner.alpha() == 0, qPrintable(qsl("the Copy menu's corner is painted %1 rather than cut away, so its radius has a square window behind it").arg(corner.name(QColor::HexArgb))));
        const int distance = std::abs(surface.red() - tokens.card.red()) + std::abs(surface.green() - tokens.card.green()) + std::abs(surface.blue() - tokens.card.blue());
        QVERIFY2(surface.alpha() == 255 && distance <= scmHairlineInkSlack,
                 qPrintable(qsl("the Copy menu is painted %1 rather than the card surface's %2").arg(surface.name(QColor::HexArgb), tokens.card.name())));
    }

    void test_anAppearanceChangeRedrawsTheFields()
    {
        auto* pDialog = dialog();
        QVERIFY2(pDialog, "No connection dialog to test against");

        const bool wasDark = mudlet::self()->inDarkMode();
        const QString before = pDialog->tab_connection_info->styleSheet();

        mudlet::self()->setAppearance(wasDark ? enums::Appearance::light : enums::Appearance::dark);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);

        const QString after = pDialog->tab_connection_info->styleSheet();
        QVERIFY2(after != before, "the appearance moved and the tab's sheet did not, so nothing re-ran the style pass");
        QVERIFY2(after.contains(uiDesign::themeTokens().field.name()), "the tab's sheet was rebuilt without the field surface of the appearance it was rebuilt in");

        mudlet::self()->setAppearance(wasDark ? enums::Appearance::dark : enums::Appearance::light);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
    }
};

MUDLET_GROUPED_TEST_MAIN(ConnectionDialogStyleTest)
#include "ConnectionDialogStyleTest.moc"
