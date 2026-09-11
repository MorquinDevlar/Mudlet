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
 * A trigger's options are one row of its form - the options strip, led by the
 * word "Options" - between the row its name is typed on and the list of its
 * patterns. They were a 280px column of four cards beside those patterns,
 * folded away by the window being either short or narrow; then two rows, which
 * still wanted a very wide window before they stopped wrapping.
 *
 * The strip is opened by the Options button on the head row, and the editor
 * opens with it closed - so init() opens it, and every case below measures an
 * open strip. What it is opened and closed by is EditorOptionsPanelDefaultTest.
 *
 * What the strip is held to here:
 *
 * - It sits between the head row and the pattern list, and the word leading it
 *   starts where every other form's lead word does.
 * - A narrow form costs the strip a line rather than costing the pattern rows
 *   their width: at 1000px of strip it is two lines and no more, taller than at
 *   1400px, the list grows no horizontal bar, and the form pane follows the
 *   height.
 * - Everything on one line of the strip is read at one height: a check box and
 *   a spin box beside it share a vertical centre rather than the check box
 *   riding at the top of the line the FlowLayout put them both on.
 * - A number box on the strip is the width of the largest number it can hold
 *   and no wider, and that number still fits inside it.
 * - The code pane still keeps its third of the two panes underneath it.
 * - The two matching modes are radio buttons drawn as joined segments, so a
 *   screen reader still says which of the two is chosen; they write into
 *   spinBox_lineMargin, which is where the trigger is saved from.
 * - The one hairline those two share belongs to whichever of them is chosen, so
 *   the accent goes all the way round it; neither segment moves when the choice
 *   changes.
 * - A spin box says it is being used: the chevron under the pointer takes the
 *   accent, and gives it back when the pointer leaves.
 * - With one pattern there is nothing to combine, so the segments and the line
 *   count are greyed out and the caption saying why is on show.
 * - The switches are not gates: choosing a sound file turns the sound on, and
 *   choosing a colour turns the highlight on.
 * - Every control on the strip carries an accessible name and a plain-text
 *   description, since Qt would otherwise read a screen reader the rich text a
 *   tooltip is written in - and the four check boxes whose words were shortened
 *   to fit the strip are still named in full to it.
 * - The tab chain runs the head row, the Options button, the strip left to
 *   right, and then the patterns - and with the strip put away it runs from the
 *   button straight to the first pattern.
 * - The accent a mark takes on focus is the keyboard's: a click turns an option
 *   on without leaving the focus, and so the accent, behind on it.
 *
 * Run with: ctest -R EditorTriggerOptionsStripTest -V
 */

#include <QAbstractButton>
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QScopeGuard>
#include <QSplitter>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyleOptionSpinBox>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest/QtTest>
#include <chrono>
#include <cstdlib>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TTrigger.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgTriggerEditor.h"
#include "dlgTriggerPatternEdit.h"
#include "dlgTriggersMainArea.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorTriggerOptionsStripTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    QTreeWidgetItem* mpThreePatternRow = nullptr;
    QTreeWidgetItem* mpOnePatternRow = nullptr;
    const QString mProfileName = qsl("EditorTriggerOptionsStrip-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    // The two widths every wrapping case is measured between are the strip's
    // own, not the window's: the sidebar and the item tree take a fixed several
    // hundred pixels off the left of the editor before the form sees any of it,
    // so a window width says nothing about how much the strip has to wrap in.
    // resizeToStripWidth() is what turns one into the other.
    //
    // 1400px is what the whole strip comes to on one line, which is what makes
    // a single vertical centre a thing there is to measure; 1000px is about
    // what a maximised editor leaves the form, and is where it wraps once.
    static constexpr int scmWideStrip = 1400;
    static constexpr int scmNarrowStrip = 1000;
    static constexpr int scmTallEditor = 900;
    // Short enough that the form asks for more of the two panes than the code
    // pane's share leaves it, which is the whole of what that case measures
    static constexpr int scmShortEditor = 500;
    // The seam is placed by hand often enough to be a pixel or two off what was
    // asked for
    static constexpr int scmSeamTolerance = 2;
    // Mirrors scmEditorSegmentPaddingHorizontal in src/dlgTriggerEditor.cpp,
    // which is file-local to it, and what the hairline either side of it comes
    // to. An indicator that came back would add its own width and the gap after
    // it - some twenty pixels - on top of these.
    static constexpr int scmSegmentPadding = 12;
    static constexpr int scmSegmentSlack = 6;
    // Mirrors scmEditorOptionsSpinBoxSlack, also file-local to the editor: what
    // a number box keeps between its last digit and the arrows beside it
    static constexpr int scmSpinBoxSlack = 2;
    // Mirrors scmEditorRowControlGap, which the strip's FlowLayout is given as
    // its vertical spacing - what stands between one wrapped line and the next
    static constexpr int scmStripLineGap = 8;
    // A control mapped into the window lands on a whole pixel, so two that are
    // laid out level can still be read a pixel apart
    static constexpr int scmCentreSlack = 1;
    // What a window sized to leave the strip a given width may be out by, since
    // what stands to the left of the form is measured rather than named
    static constexpr int scmStripWidthSlack = 2;
    // An ink read off a grab is held to nearness rather than to a match, summed
    // over the three channels. A hairline is drawn flat, so it comes out as the
    // colour itself and this is only slack; the segment beside it is 379 away.
    static constexpr int scmInkSlack = 40;
    // A chevron 7px across is all edge, so its darkest pixel is a blend rather
    // than the tint it was drawn in: measured 64 from the accent under the
    // pointer against 152 at rest, so this sits between the two rather than
    // near zero
    static constexpr int scmChevronInkSlack = 100;
    // ...and what the pointer is worth on top of that, so that a chevron which
    // never changed cannot pass by sitting near the line
    static constexpr int scmChevronInkShift = 40;
    // How far into a segment its own fill is read at: well inside the padding,
    // so that neither a hairline at the edge nor the word in the middle is what
    // comes back
    static constexpr int scmFillDepth = 4;
    // How far a pixel inside a mark's box has to be from that box's fill before
    // it counts as part of the mark rather than as the fill's own noise
    static constexpr int scmMarkInkSlack = 40;

    void deleteProfileDirectory(const QString& profileName)
    {
        QDir dir(mudlet::getMudletPath(enums::profileHomePath, profileName));
        if (dir.exists()) {
            dir.removeRecursively();
        }
    }

    void settle()
    {
        QCoreApplication::processEvents();
        QTest::qWait(80ms);
        QCoreApplication::processEvents();
    }

    // Qt measures a control once and keeps the answer, and a check state
    // changing does not ask it again - so a segment whose padding did not make
    // up for the hairline it gave up would only be caught out the next time
    // something re-laid the form. This is that moment, brought forward: a style
    // change is what clears the cached hint, and the layout is run again after.
    void remeasure(const QList<QWidget*>& widgets)
    {
        for (QWidget* pWidget : widgets) {
            QEvent styleChange(QEvent::StyleChange);
            QCoreApplication::sendEvent(pWidget, &styleChange);
            pWidget->updateGeometry();
        }
        settle();
    }

    dlgTriggersMainArea* form() const { return mpEditor->mpTriggersMainArea; }

    QGroupBox* optionsRow() const { return form()->findChild<QGroupBox*>(qsl("editorOptionsRow")); }

    int topEdgeOf(const QWidget* pWidget) const { return pWidget->mapTo(mpEditor, QPoint(0, 0)).y(); }

    int leftEdgeOf(const QWidget* pWidget) const { return pWidget->mapTo(mpEditor, QPoint(0, 0)).x(); }

    void chooseTrigger(QTreeWidgetItem* pRow)
    {
        mpEditor->slot_showTriggers();
        settle();
        mpEditor->treeWidget_triggers->setCurrentItem(pRow);
        mpEditor->slot_triggerSelected(pRow);
        settle();
        mpEditor->hideSystemMessageArea();
        settle();
    }

    // Settled twice: the width the form is given is answered on the next pass
    // through the event loop, and the refit that follows moves the seam on the
    // pass after that
    void resizeTheEditor(const int width, const int height)
    {
        mpEditor->resize(width, height);
        settle();
        settle();
    }

    // The window width that leaves the strip the width being measured at.
    // Measured rather than named: what stands to the left of the form is the
    // sidebar and the item tree, and a case that hard-coded the sum of them
    // would quietly start measuring a different width the day either moves.
    void resizeToStripWidth(const int stripWidth, const int height)
    {
        resizeTheEditor(stripWidth + 600, height);
        resizeTheEditor(stripWidth + mpEditor->width() - optionsRow()->width(), height);
        QVERIFY2(std::abs(optionsRow()->width() - stripWidth) <= scmStripWidthSlack,
                 qPrintable(qsl("the strip is %1px wide in a %2px window, where %3px was asked for").arg(optionsRow()->width()).arg(mpEditor->width()).arg(stripWidth)));
    }

    // Every control on the strip that the keyboard can reach
    QList<QWidget*> focusableControlsOnTheStrip() const
    {
        QList<QWidget*> controls;
        for (QWidget* pWidget : optionsRow()->findChildren<QWidget*>()) {
            // Qt's own parts of a control - a spin box's inner line edit - are
            // the control as far as a screen reader is concerned, and are named
            // by the control around them
            if (pWidget->objectName().startsWith(qsl("qt_"))) {
                continue;
            }
            if (pWidget->focusPolicy() != Qt::NoFocus && pWidget->isVisibleTo(mpEditor)) {
                controls << pWidget;
            }
        }
        return controls;
    }

    // Where the middle of a control lands on the editor, which is what two
    // controls on one line of the strip have to agree on
    int verticalCentreOf(const QWidget* pWidget) const { return pWidget->mapTo(mpEditor, pWidget->rect().center()).y(); }

    // The chain the keyboard actually walks, from the command field onwards,
    // over what is visible and can hold the focus
    QList<QWidget*> tabChainFromTheCommandField() const
    {
        QList<QWidget*> chain;
        QWidget* pAt = form()->lineEdit_trigger_command;
        for (int step = 0; step < 400; ++step) {
            pAt = pAt->nextInFocusChain();
            if (!pAt || pAt == form()->lineEdit_trigger_command) {
                break;
            }
            if (pAt->focusPolicy() != Qt::NoFocus && pAt->isVisibleTo(mpEditor)) {
                chain << pAt;
            }
        }
        return chain;
    }

    static QString describe(const QWidget* pWidget) { return pWidget->objectName().isEmpty() ? QString::fromLatin1(pWidget->metaObject()->className()) : pWidget->objectName(); }

    // How far apart two inks are, summed over the three channels: a hairline
    // and a chevron are both drawn against a fill of their own, so what is
    // measured is nearness rather than a match
    static int distanceBetween(const QColor& one, const QColor& other) { return std::abs(one.red() - other.red()) + std::abs(one.green() - other.green()) + std::abs(one.blue() - other.blue()); }

    // A widget's box in the pixels a grab is made of, which are the widget's
    // own multiplied by whatever the screen doubles them by
    static QRect inTheGrabsPixels(const QImage& shot, const QRect& box)
    {
        const qreal ratio = shot.devicePixelRatio();
        return QRect(QPoint(qRound(box.left() * ratio), qRound(box.top() * ratio)), QPoint(qRound((box.right() + 1) * ratio) - 1, qRound((box.bottom() + 1) * ratio) - 1)).intersected(shot.rect());
    }

    // The ink at a given depth into one side of a segment, taken from a grab of
    // the pair and read at the height the segment's middle is at. A depth of 0
    // is the outermost pixel, which is where a border a pixel wide is drawn; a
    // few pixels in is well inside the padding, so it is the fill that border
    // is drawn against and nothing else
    static QColor colourInsideTheEdge(const QImage& shot, const QWidget* pSegment, const bool rightHandSide, const int depth)
    {
        const QRect box = inTheGrabsPixels(shot, pSegment->geometry().adjusted(depth, 0, -depth, 0));
        return shot.pixelColor(rightHandSide ? box.right() : box.left(), box.center().y());
    }

    // Where the style puts the mark on a check box, in the pixels of a grab of
    // the window around it. Read off the window rather than off the control or
    // its form: either is grabbed against its own palette where it paints no
    // background of its own, so the column the form actually sits on is only
    // there in a grab that holds the column too.
    static QRect markBoxIn(const QAbstractButton* pButton, const QWidget* pWindow, const QImage& shot)
    {
        QStyleOptionButton option;
        option.initFrom(pButton);
        QRect box = pButton->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, pButton);
        box.moveTopLeft(pButton->mapTo(pWindow, box.topLeft()));
        return inTheGrabsPixels(shot, box);
    }

    // How many pixels well inside that box are not the fill it is drawn on:
    // none in an empty box, a good few once the tick is in it
    static int markedPixelsIn(const QImage& shot, const QRect& box, const QColor& fill)
    {
        const QRect inside = box.adjusted(box.width() / 4, box.height() / 4, -box.width() / 4, -box.height() / 4);
        int marked = 0;
        for (int y = inside.top(); y <= inside.bottom(); ++y) {
            for (int x = inside.left(); x <= inside.right(); ++x) {
                if (distanceBetween(shot.pixelColor(x, y), fill) > scmMarkInkSlack) {
                    ++marked;
                }
            }
        }
        return marked;
    }

    // The strongest colour the box is outlined in, read against the form
    // showing through beside it
    static qreal outlineRatioOf(const QImage& shot, const QRect& box, const QColor& behind)
    {
        QColor outline = behind;
        for (int x = box.left(); x <= box.right(); ++x) {
            const QColor sample = shot.pixelColor(x, box.top());
            if (uiDesign::contrastRatio(sample, behind) > uiDesign::contrastRatio(outline, behind)) {
                outline = sample;
            }
        }
        return uiDesign::contrastRatio(outline, behind);
    }

    // The one path an appearance change takes while this window is open: the
    // application style is replaced, and the StyleChange that follows is what
    // the editor restyles itself on
    void takeTheEditorTo(const enums::Appearance appearance)
    {
        mudlet::self()->setAppearance(appearance);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        QCoreApplication::processEvents();
        mpEditor->grab();
    }

    // How near the accent the nearest pixel inside a box comes - a chevron
    // tinted with it reaches it, one tinted with the quiet ink does not
    static int nearestToTheAccentIn(const QImage& shot, const QRect& box, const QColor& accent)
    {
        int nearest = 3 * 255;
        const QRect scanned = inTheGrabsPixels(shot, box);
        for (int y = scanned.top(); y <= scanned.bottom(); ++y) {
            for (int x = scanned.left(); x <= scanned.right(); ++x) {
                nearest = std::min(nearest, distanceBetween(shot.pixelColor(x, y), accent));
            }
        }
        return nearest;
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
        QVERIFY2(mpHost != nullptr, "No active host available for the test.");
        QSignalSpy connected(&(mpHost->mTelnet), &cTelnet::signal_connected);
        QVERIFY2(connected.wait(2000), "Could not connect with the host.");

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mpEditor->resize(scmWideStrip + 600, scmTallEditor);
        settle();

        mpEditor->slot_showTriggers();
        mpEditor->addTrigger(false);
        settle();
        mpThreePatternRow = mpEditor->mpCurrentTriggerItem;
        QVERIFY2(mpThreePatternRow != nullptr, "the three-pattern trigger these cases edit could not be made");
        mpEditor->showPatternItems(3);
        mpEditor->mTriggerPatternEdit.at(0)->singleLineTextEdit_pattern->setPlainText(qsl("There's water ahead of you. You'll have to swim in that direction."));
        mpEditor->mTriggerPatternEdit.at(1)->singleLineTextEdit_pattern->setPlainText(qsl("You'll have to swim to make it through the water in that direction."));
        mpEditor->mTriggerPatternEdit.at(2)->singleLineTextEdit_pattern->setPlainText(qsl("The water is too deep for you to walk that way, you must swim."));
        mpEditor->slot_saveEdits();
        settle();

        mpEditor->addTrigger(false);
        settle();
        mpOnePatternRow = mpEditor->mpCurrentTriggerItem;
        QVERIFY2(mpOnePatternRow != nullptr, "the one-pattern trigger these cases edit could not be made");
        mpEditor->showPatternItems(1);
        mpEditor->mTriggerPatternEdit.at(0)->singleLineTextEdit_pattern->setPlainText(qsl("You'll have to SWIM"));
        mpEditor->slot_saveEdits();
        settle();
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

    void init()
    {
        if (!mpEditor) {
            return;
        }
        resizeTheEditor(scmWideStrip + 600, scmTallEditor);
        // Every case here measures an open strip, and the editor opens with it
        // closed
        mpEditor->setTriggerOptionsShown(true);
        settle();
    }

    // (a) Where the strip is, and what it lines up with
    void test_theStripSitsBetweenTheNameAndThePatterns()
    {
        chooseTrigger(mpThreePatternRow);

        QVERIFY2(optionsRow() != nullptr, "the trigger form has no options strip");
        QVERIFY2(optionsRow()->isVisible(), "the options strip was opened in init() and is not on show");

        const int headTop = topEdgeOf(form()->widget_top);
        const int stripTop = topEdgeOf(optionsRow());
        const int patternsTop = topEdgeOf(form()->widget_left);
        qInfo().noquote() << qsl("  head row at %1, the strip at %2, patterns at %3").arg(headTop).arg(stripTop).arg(patternsTop);

        QVERIFY2(headTop < stripTop, "the options strip is not under the row the name is typed on");
        QVERIFY2(stripTop < patternsTop, "the pattern list is not under the options strip");

        // The word leading the strip starts where the Name label does, and the
        // strip after it starts where the Name field does - which is the whole
        // of what one lead width buys
        QLabel* pName = form()->label_trigger_name;
        QLabel* pLead = mpEditor->mpLabel_optionsRow;
        QVERIFY2(pLead->property("editorRowLabel").toBool(), qPrintable(qsl("the word \"%1\" is not written in the quiet ink a form's scaffolding takes").arg(pLead->text())));
        QCOMPARE(leftEdgeOf(pLead), leftEdgeOf(pName));
        qInfo().noquote() << qsl("  the Name label is %1px wide and the lead word \"%2\" %3px, with the Name field at x=%4 and the strip at x=%5")
                                     .arg(pName->width())
                                     .arg(pLead->text())
                                     .arg(pLead->width())
                                     .arg(leftEdgeOf(form()->lineEdit_trigger_name))
                                     .arg(leftEdgeOf(optionsRow()));
        QCOMPARE(leftEdgeOf(optionsRow()), leftEdgeOf(form()->lineEdit_trigger_name));
        // ...and the strip is a grouping a screen reader is told about, named
        // by that same word rather than by a title of its own
        QCOMPARE(optionsRow()->accessibleName(), pLead->text());
    }

    // (b) A narrow editor costs the strip a line, not the pattern rows their
    // width - and no more than one line at the width an ordinary editor leaves
    // the form, which is what the shorter words on it are for. This is what
    // replaces the fold that used to take the options away.
    void test_aNarrowFormWrapsTheStripRatherThanThePatternRows()
    {
        chooseTrigger(mpThreePatternRow);
        resizeToStripWidth(scmWideStrip, scmTallEditor);
        const int wideStrip = optionsRow()->height();
        const int widePane = mpEditor->splitter_right->sizes().at(0);

        resizeToStripWidth(scmNarrowStrip, scmTallEditor);
        const int narrowStrip = optionsRow()->height();
        const int narrowPane = mpEditor->splitter_right->sizes().at(0);
        const int twoLines = 2 * uiDesign::scmInputHeight + scmStripLineGap;
        qInfo().noquote() << qsl("  a %1px strip is %2px tall in a form pane of %3; a %4px one is %5px in a pane of %6, where two lines are %7px")
                                     .arg(scmWideStrip)
                                     .arg(wideStrip)
                                     .arg(widePane)
                                     .arg(scmNarrowStrip)
                                     .arg(narrowStrip)
                                     .arg(narrowPane)
                                     .arg(twoLines);

        QVERIFY2(narrowStrip > wideStrip,
                 qPrintable(qsl("the strip is %1px tall at %2px wide and %3px at %4px - it did not wrap onto another line").arg(narrowStrip).arg(scmNarrowStrip).arg(wideStrip).arg(scmWideStrip)));
        QVERIFY2(narrowStrip <= twoLines,
                 qPrintable(qsl("the strip is %1px tall at %2px wide, which is more than the %3px two lines come to - the words on it are too long for this width")
                                    .arg(narrowStrip)
                                    .arg(scmNarrowStrip)
                                    .arg(twoLines)));
        QVERIFY2(!mpEditor->mpScrollArea->horizontalScrollBar()->isVisible(),
                 qPrintable(qsl("the pattern list is scrolling sideways in a %1px window, with %2px for a row that wants %3px")
                                    .arg(mpEditor->width())
                                    .arg(mpEditor->mpScrollArea->viewport()->width())
                                    .arg(mpEditor->mpWidget_triggerItems->minimumSizeHint().width())));
        QVERIFY2(narrowPane > widePane,
                 qPrintable(qsl("the strip grew by %1px and the form pane stayed at %2px, so the extra line is being drawn over the patterns").arg(narrowStrip - wideStrip).arg(narrowPane)));
    }

    // (b2) A FlowLayout puts every item at the top of the line it lands on, so
    // a 20px check box beside a 30px spin box would be read a few pixels above
    // it. Everything on the strip is one control tall, and each group centres
    // what is inside it, so one line has one middle.
    void test_everythingOnOneLineOfTheStripSharesAVerticalCentre()
    {
        chooseTrigger(mpThreePatternRow);
        resizeToStripWidth(scmWideStrip, scmTallEditor);

        const QList<QWidget*> controls = focusableControlsOnTheStrip();
        QVERIFY2(controls.size() >= 9, qPrintable(qsl("only %1 focusable controls were found on the strip, so this walk is not covering it").arg(controls.size())));

        QStringList read;
        QStringList failures;
        const int centre = verticalCentreOf(controls.first());
        for (QWidget* pControl : controls) {
            const int at = verticalCentreOf(pControl);
            read << qsl("%1 at %2").arg(describe(pControl)).arg(at);
            if (std::abs(at - centre) > scmCentreSlack) {
                failures << qsl("%1 is centred at %2 against %3 for %4, %5px out").arg(describe(pControl)).arg(at).arg(centre).arg(describe(controls.first())).arg(std::abs(at - centre));
            }
        }
        qInfo().noquote() << qsl("  a %1px strip is %2px tall and holds: %3").arg(scmWideStrip).arg(optionsRow()->height()).arg(read.join(qsl(", ")));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(qsl("\n"))));
        // ...and one line is what they were all on, so that a centre they agree
        // on is one middle rather than a coincidence between two lines
        QCOMPARE(optionsRow()->height(), uiDesign::scmInputHeight);
    }

    // (b2) A number box on the strip is as wide as the largest number it can
    // hold and no wider. Both were a flat 72px, which is room for a couple of
    // digits neither of them will ever show.
    void test_theNumberBoxesAreOnlyAsWideAsTheirLargestNumber()
    {
        chooseTrigger(mpThreePatternRow);
        resizeToStripWidth(scmWideStrip, scmTallEditor);

        QStringList misses;
        QStringList read;
        for (QSpinBox* pBox : {mpEditor->mpSpinBox_matchWithinLines, form()->spinBox_stayOpen}) {
            QVERIFY2(pBox->isVisible(), qPrintable(qsl("%1 is not on show, so its width says nothing").arg(pBox->objectName())));

            const QFontMetrics metrics = pBox->fontMetrics();
            const QString largest = QString::number(pBox->maximum());
            QChar widest = QLatin1Char('0');
            for (char digit = '0'; digit <= '9'; ++digit) {
                if (metrics.horizontalAdvance(QLatin1Char(digit)) > metrics.horizontalAdvance(widest)) {
                    widest = QLatin1Char(digit);
                }
            }
            const int room = metrics.horizontalAdvance(QString(largest.length(), widest)) + 2 * (uiDesign::scmInputPaddingHorizontal + uiDesign::scmInputBorderWidth) + uiDesign::scmInputStepperWidth
                             + scmSpinBoxSlack;

            QStyleOptionSpinBox option;
            option.initFrom(pBox);
            option.subControls = QStyle::SC_All;
            const QRect editField = pBox->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxEditField, pBox);
            const int largestNumber = metrics.horizontalAdvance(largest);

            read << qsl("%1 is %2px wide against the %3px %4 comes to, and types it in %5px").arg(pBox->objectName()).arg(pBox->width()).arg(room).arg(largest).arg(editField.width());

            if (pBox->width() > room) {
                misses << qsl("%1 is %2px wide, where %3 and the room the field's rule leaves round it come to %4px").arg(pBox->objectName()).arg(pBox->width()).arg(largest).arg(room);
            }
            if (editField.width() < largestNumber) {
                misses << qsl("%1 leaves %2px to type in, where %3 is %4px wide").arg(pBox->objectName()).arg(editField.width()).arg(largest).arg(largestNumber);
            }
        }
        qInfo().noquote() << qsl("  %1").arg(read.join(qsl("; ")));
        QVERIFY2(misses.isEmpty(), qPrintable(qsl("\n  %1").arg(misses.join(qsl("\n  ")))));
    }

    // (c) ...and the code pane still keeps its third of the two panes, in a
    // window short enough for the form to want more than that
    void test_theCodePaneKeepsItsFloorInAShortWindow()
    {
        chooseTrigger(mpThreePatternRow);
        resizeToStripWidth(scmNarrowStrip, scmShortEditor);

        const QList<int> sizes = mpEditor->splitter_right->sizes();
        QVERIFY2(sizes.size() >= 2, "the right hand splitter has lost a pane");
        const int paneTotal = sizes.at(0) + sizes.at(1);
        const int floor = mpEditor->codePaneFloor(paneTotal);
        qInfo().noquote() << qsl("  in a %1x%2 window the panes are %3 / %4 of %5, where the code pane keeps %6")
                                     .arg(mpEditor->width())
                                     .arg(scmShortEditor)
                                     .arg(sizes.at(0))
                                     .arg(sizes.at(1))
                                     .arg(paneTotal)
                                     .arg(floor);

        // Held at the ceiling rather than sitting under it: a window the form
        // fits in with room to spare says nothing about what the code pane keeps
        QVERIFY2(std::abs(sizes.at(0) - (paneTotal - floor)) <= scmSeamTolerance,
                 qPrintable(qsl("the form is at %1 of the %2 the two panes have, rather than at the %3 the code pane's share leaves it - this window is not short enough for the case to say anything")
                                    .arg(sizes.at(0))
                                    .arg(paneTotal)
                                    .arg(paneTotal - floor)));
        QVERIFY2(sizes.at(1) >= floor - scmSeamTolerance,
                 qPrintable(qsl("the form was given %1 of the %2 the two panes have, leaving the code pane %3 against the %4 it keeps").arg(sizes.at(0)).arg(paneTotal).arg(sizes.at(1)).arg(floor)));
        // ...and that floor is a third of the two panes, written out here rather
        // than read from codePaneFloor(), which is the thing being held
        QVERIFY2(sizes.at(1) >= paneTotal / 3 - scmSeamTolerance,
                 qPrintable(qsl("the code pane was left %1 of the %2 the two panes have, where a third of them is %3").arg(sizes.at(1)).arg(paneTotal).arg(paneTotal / 3)));
    }

    // (d) The two modes are one control of two segments, and they are still the
    // radio pair spinBox_lineMargin is written from
    void test_theSegmentsWriteTheMatchingModeIntoTheHiddenSpinBox()
    {
        chooseTrigger(mpThreePatternRow);

        QRadioButton* pAny = mpEditor->mpRadioButton_matchAny;
        QRadioButton* pAll = mpEditor->mpRadioButton_matchAll;
        QSpinBox* pWithin = mpEditor->mpSpinBox_matchWithinLines;

        pAll->setChecked(true);
        settle();
        QVERIFY2(!pAny->isChecked(), "both segments are on at once, so they are no longer one exclusive choice");
        QVERIFY2(mpEditor->mpWidget_matchWithinRow->isEnabled(), "the line count is greyed out in the All mode, which is the mode it belongs to");
        pWithin->setValue(4);
        settle();
        QCOMPARE(form()->spinBox_lineMargin->value(), 4);

        pAny->setChecked(true);
        settle();
        QVERIFY2(!pAll->isChecked(), "choosing Any left All chosen as well");
        QVERIFY2(!mpEditor->mpWidget_matchWithinRow->isEnabled(), "the line count is still available in the Any mode, which carries none");
        QCOMPARE(form()->spinBox_lineMargin->value(), -1);

        // Drawn as a box round the words rather than as a dot beside them: the
        // indicator is given no size and no gap, so the segment is its own text
        // and the padding round it and nothing else
        const int textWidth = pAll->fontMetrics().horizontalAdvance(pAll->text());
        const int roundTheText = pAll->width() - textWidth;
        qInfo().noquote() << qsl("  \"%1\" is %2px wide against %3px of text, so %4px is round it").arg(pAll->text()).arg(pAll->width()).arg(textWidth).arg(roundTheText);
        QVERIFY2(pAll->property("editorSegment").toBool(), "the segment does not carry the property its rules select on");
        QVERIFY2(roundTheText <= 2 * (scmSegmentPadding + uiDesign::scmInputBorderWidth) + scmSegmentSlack,
                 qPrintable(qsl("\"%1\" is %2px wide for %3px of text - %4px round it, which is a dot and the gap after it rather than the padding alone")
                                    .arg(pAll->text())
                                    .arg(pAll->width())
                                    .arg(textWidth)
                                    .arg(roundTheText)));
    }

    // (e) With one pattern there is nothing to combine, so the choice is greyed
    // out and the caption at the end of the strip says why
    void test_onceMorePatternsAreThereTheModesBecomeAvailable()
    {
        chooseTrigger(mpOnePatternRow);
        QCOMPARE(mpEditor->mVisiblePatternCount, 1);
        QVERIFY2(!mpEditor->mpWidget_matchModeRows->isEnabled(), "the two segments are available on a trigger with one pattern, which has nothing to combine");
        QVERIFY2(!mpEditor->mpWidget_matchWithinRow->isEnabled(), "the line count is available on a trigger with one pattern");
        QVERIFY2(!mpEditor->mpLabel_matchModeHint->isHidden(), "the caption saying why the modes are greyed out is not on show");

        chooseTrigger(mpThreePatternRow);
        QCOMPARE(mpEditor->mVisiblePatternCount, 3);
        QVERIFY2(mpEditor->mpWidget_matchModeRows->isEnabled(), "the two segments are still greyed out on a trigger with three patterns");
        QVERIFY2(mpEditor->mpLabel_matchModeHint->isHidden(), "the caption is still on show on a trigger that has patterns to combine");
    }

    // (f) The switches are not gates over what stands beside them: a choice
    // made in either turns its own switch on
    void test_aChoiceTurnsItsOwnSwitchOn()
    {
        chooseTrigger(mpThreePatternRow);
        form()->checkBox_soundTrigger->setChecked(false);
        form()->checkBox_triggerColorizer->setChecked(false);
        mpEditor->showTriggerSoundFile(QString());
        settle();
        QVERIFY2(!form()->toolButton_clearSoundFile->isVisible(), "the cross that forgets the sound file is there with no file to forget");

        const QString chosen = qsl("/tmp/EditorTriggerOptionsStripTest/water-splash.wav");
        mpEditor->mpUndoStack->clear();
        mpEditor->acceptTriggerSoundFile(chosen);
        settle();
        QVERIFY2(form()->checkBox_soundTrigger->isChecked(), "choosing a sound file left the sound switched off, so the file is set and silent");
        QCOMPARE(mpEditor->triggerSoundFilePath(), chosen);
        QVERIFY2(form()->lineEdit_soundFile->text().contains(qsl("water-splash")), qPrintable(qsl("the field reads \"%1\" rather than the file's name").arg(form()->lineEdit_soundFile->text())));
        QVERIFY2(form()->toolButton_clearSoundFile->isVisible(), "the cross that forgets the sound file is away with a file set");
        // The file and the switch, one entry each and in that order
        qInfo().noquote() << qsl("  choosing a sound file pushed %1 undo entries").arg(mpEditor->mpUndoStack->count());
        QCOMPARE(mpEditor->mpUndoStack->count(), 2);

        mpEditor->slot_clearSoundFile();
        settle();
        QVERIFY2(!form()->toolButton_clearSoundFile->isVisible(), "the cross is still there after the file it forgets was forgotten");
        QVERIFY2(mpEditor->triggerSoundFilePath().isEmpty(), "the field still stands for a file after being cleared");

        mpEditor->mpUndoStack->clear();
        mpEditor->turnTriggerHighlightOn();
        settle();
        QVERIFY2(form()->checkBox_triggerColorizer->isChecked(), "choosing a colour left the highlight switched off, so the colour is set and unused");
        QCOMPARE(mpEditor->mpUndoStack->count(), 1);
    }

    // (g) A screen reader is told what each control is and what it does, in
    // words rather than in the rich text a tooltip is written in
    void test_everyControlOnTheStripNamesItselfToAScreenReader()
    {
        chooseTrigger(mpThreePatternRow);

        const QList<QWidget*> controls = focusableControlsOnTheStrip();
        QVERIFY2(controls.size() >= 9, qPrintable(qsl("only %1 focusable controls were found on the strip, so this walk is not covering it").arg(controls.size())));
        QStringList failures;
        for (QWidget* pControl : controls) {
            if (pControl->accessibleName().isEmpty()) {
                failures << qsl("%1 has no accessible name").arg(describe(pControl));
            }
            const QString description = pControl->accessibleDescription();
            if (description.isEmpty()) {
                failures << qsl("%1 has no accessible description, so a screen reader falls back to its rich text tooltip").arg(describe(pControl));
            } else if (description.contains(QLatin1Char('<'))) {
                failures << qsl("%1 is described as \"%2\", which is markup being read out").arg(describe(pControl), description);
            }
        }
        qInfo().noquote() << qsl("  %1 focusable controls on the strip were read").arg(controls.size());
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(qsl("\n"))));

        // The words on the strip are as short as they can be said in; what a
        // screen reader is given is the whole of what each one is, since it
        // reaches a check box with nothing else beside it
        const QList<QPair<QWidget*, QString>> saidInFull{{mpEditor->mpRadioButton_matchAny, qsl("Any pattern")},
                                                         {mpEditor->mpRadioButton_matchAll, qsl("All patterns")},
                                                         {form()->checkBox_perlSlashGOption, qsl("Every occurrence in a line")},
                                                         {form()->checkBox_filterTrigger, qsl("Only pass matches to children")},
                                                         {form()->checkBox_soundTrigger, qsl("Play a sound")},
                                                         {form()->checkBox_triggerColorizer, qsl("Highlight matches")}};
        for (const auto& [pControl, name] : saidInFull) {
            QVERIFY2(pControl->accessibleName() == name,
                     qPrintable(qsl("%1 reads \"%2\" and is named \"%3\" to a screen reader rather than \"%4\"")
                                        .arg(describe(pControl), qobject_cast<QAbstractButton*>(pControl)->text(), pControl->accessibleName(), name)));
        }
    }

    // (h) The keyboard reads the form the way the eye does
    void test_theTabChainRunsTheStripBeforeThePatterns()
    {
        chooseTrigger(mpThreePatternRow);

        const QList<QWidget*> chain = tabChainFromTheCommandField();
        const auto at = [&chain](QWidget* pWidget) {
            return chain.indexOf(pWidget);
        };

        const int anySegment = at(mpEditor->mpRadioButton_matchAny);
        const int allSegment = at(mpEditor->mpRadioButton_matchAll);
        const int within = at(mpEditor->mpSpinBox_matchWithinLines);
        const int everyOccurrence = at(form()->checkBox_perlSlashGOption);
        const int keepFiring = at(form()->spinBox_stayOpen);
        const int onlyMatches = at(form()->checkBox_filterTrigger);
        const int playSound = at(form()->checkBox_soundTrigger);
        const int soundFile = at(form()->lineEdit_soundFile);
        const int highlight = at(form()->checkBox_triggerColorizer);
        const int foreground = at(form()->pushButtonFgColor);
        const int background = at(form()->pushButtonBgColor);
        const int firstPattern = at(mpEditor->mTriggerPatternEdit.at(0)->singleLineTextEdit_pattern);

        QStringList reading;
        for (QWidget* pWidget : chain) {
            reading << describe(pWidget);
            if (reading.size() >= 16) {
                break;
            }
        }
        qInfo().noquote() << qsl("  the chain from the command field reads: %1").arg(reading.join(qsl(" -> ")));

        const QList<QPair<QString, int>> ordered{{qsl("the Options button"), at(form()->toolButton_toggleExtraControls)},
                                                 {qsl("Any pattern"), anySegment},
                                                 {qsl("All patterns"), allSegment},
                                                 {qsl("within lines"), within},
                                                 {qsl("Every occurrence"), everyOccurrence},
                                                 {qsl("Keep firing"), keepFiring},
                                                 {qsl("Only pass matches"), onlyMatches},
                                                 {qsl("Sound"), playSound},
                                                 {qsl("sound file"), soundFile},
                                                 {qsl("Highlight"), highlight},
                                                 {qsl("Foreground"), foreground},
                                                 {qsl("Background"), background},
                                                 {qsl("the first pattern"), firstPattern}};
        for (const auto& [name, index] : ordered) {
            QVERIFY2(index >= 0, qPrintable(qsl("%1 is not in the tab chain at all").arg(name)));
        }
        for (int step = 1; step < ordered.size(); ++step) {
            QVERIFY2(ordered.at(step - 1).second < ordered.at(step).second,
                     qPrintable(qsl("%1 is reached at %2 and %3 at %4, so the chain does not read left to right along the strip")
                                        .arg(ordered.at(step - 1).first)
                                        .arg(ordered.at(step - 1).second)
                                        .arg(ordered.at(step).first)
                                        .arg(ordered.at(step).second)));
        }
    }

    // ...and with the strip put away the keyboard walks past it rather than
    // into controls nobody can see
    void test_aClosedStripTakesItsControlsOutOfTheTabChain()
    {
        chooseTrigger(mpThreePatternRow);
        mpEditor->setTriggerOptionsShown(false);
        settle();
        auto reopenTheStrip = qScopeGuard([this]() {
            mpEditor->setTriggerOptionsShown(true);
            settle();
        });

        const QList<QWidget*> chain = tabChainFromTheCommandField();
        const auto at = [&chain](QWidget* pWidget) {
            return chain.indexOf(pWidget);
        };

        QStringList reading;
        for (QWidget* pWidget : chain) {
            reading << describe(pWidget);
            if (reading.size() >= 8) {
                break;
            }
        }
        qInfo().noquote() << qsl("  with the strip away the chain from the command field reads: %1").arg(reading.join(qsl(" -> ")));

        const int optionsButton = at(form()->toolButton_toggleExtraControls);
        const int firstPattern = at(mpEditor->mTriggerPatternEdit.at(0)->singleLineTextEdit_pattern);
        QVERIFY2(optionsButton >= 0, "the Options button is not in the tab chain, so a closed strip cannot be opened from the keyboard");
        QVERIFY2(firstPattern >= 0, "the first pattern is not in the tab chain");
        QVERIFY2(
                optionsButton < firstPattern,
                qPrintable(qsl("the Options button is reached at %1 and the first pattern at %2, so the chain does not run from the head row into the patterns").arg(optionsButton).arg(firstPattern)));

        const QList<QPair<QString, QWidget*>> away{
                {qsl("Any pattern"), mpEditor->mpRadioButton_matchAny}, {qsl("Every occurrence"), form()->checkBox_perlSlashGOption}, {qsl("Background"), form()->pushButtonBgColor}};
        for (const auto& [name, pControl] : away) {
            QVERIFY2(at(pControl) < 0, qPrintable(qsl("%1 is still in the tab chain at %2 with the strip put away").arg(name).arg(at(pControl))));
        }
    }

    // (i) A well with no colour of its own still says what it does, in the one
    // word that fits on it
    void test_aColourlessWellStillReadsKeep()
    {
        // Chosen first, so that the colours written below are not saved over by
        // the trigger being left; loaded again without leaving it
        chooseTrigger(mpThreePatternRow);
        TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(mpThreePatternRow->data(0, Qt::UserRole).toInt());
        QVERIFY2(pT != nullptr, "the trigger these cases edit is gone from the profile");
        pT->setColorizerFgColor(QColorConstants::Transparent);
        pT->setColorizerBgColor(QColorConstants::Transparent);
        mpEditor->slot_triggerSelected(mpThreePatternRow);
        settle();
        qInfo().noquote() << qsl("  the wells read \"%1\" and \"%2\", and are described as \"%3\" and \"%4\"")
                                     .arg(form()->pushButtonFgColor->text(),
                                          form()->pushButtonBgColor->text(),
                                          form()->pushButtonFgColor->accessibleDescription(),
                                          form()->pushButtonBgColor->accessibleDescription());
        // The words themselves, not merely that there are some: a well left
        // saying the last colour it held, or described by its own name, reads
        // as something it is not
        const QString keep = QCoreApplication::translate("dlgTriggerEditor", "keep");
        const QString keepsTheGamesColour = QCoreApplication::translate("dlgTriggerEditor", "keeps the game's colour");
        QCOMPARE(form()->pushButtonFgColor->text(), keep);
        QCOMPARE(form()->pushButtonBgColor->text(), keep);
        QCOMPARE(form()->pushButtonFgColor->accessibleDescription(), keepsTheGamesColour);
        QCOMPARE(form()->pushButtonBgColor->accessibleDescription(), keepsTheGamesColour);
    }

    // (j) The two segments meet on one hairline, and it belongs to whichever of
    // them is chosen: the accent runs all the way round the chosen segment
    // rather than stopping at the seam and being closed by its neighbour's
    // grey. The segment that gives the edge up takes its width back as padding,
    // so the pair does not shift under the pointer as the choice changes.
    void test_theChosenSegmentIsOutlinedOnEverySide()
    {
        chooseTrigger(mpThreePatternRow);
        QRadioButton* pAny = mpEditor->mpRadioButton_matchAny;
        QRadioButton* pAll = mpEditor->mpRadioButton_matchAll;
        QWidget* pPair = mpEditor->mpWidget_matchModeRows;
        const QColor accent = uiDesign::themeTokens().accent;

        pAny->setChecked(true);
        settle();
        remeasure({pAny, pAll});
        const int anyWidth = pAny->width();
        const int allWidth = pAll->width();
        const int pairWidth = pPair->width();
        QVERIFY2(pAny->geometry().right() + 1 == pAll->geometry().left(), "the two segments no longer meet, so there is no shared hairline to measure");

        // Four inks say the whole of it for one choice: the chosen segment's two
        // vertical edges, which are the seam and the outer edge it has to be
        // closed by the same colour on, and the unchosen segment's edge at the
        // seam against its own fill a few pixels further in - equal to it means
        // the seam carries one hairline rather than two
        QImage shot = pPair->grab().toImage();
        const QColor anysSeam = colourInsideTheEdge(shot, pAny, true, 0);
        const QColor anysOuterEdge = colourInsideTheEdge(shot, pAny, false, 0);
        const QColor allsEdgeUnderAny = colourInsideTheEdge(shot, pAll, false, 0);
        const QColor allsFillUnderAny = colourInsideTheEdge(shot, pAll, false, scmFillDepth);

        pAll->setChecked(true);
        settle();
        remeasure({pAny, pAll});
        shot = pPair->grab().toImage();
        const QColor allsSeam = colourInsideTheEdge(shot, pAll, false, 0);
        const QColor allsOuterEdge = colourInsideTheEdge(shot, pAll, true, 0);
        const QColor anysEdgeUnderAll = colourInsideTheEdge(shot, pAny, true, 0);
        const QColor anysFillUnderAll = colourInsideTheEdge(shot, pAny, true, scmFillDepth);

        qInfo().noquote() << qsl("  the accent is %1; Any chosen reads %2 at the seam and %3 outside, with All at %4 against a fill of %5; All chosen reads %6 at the seam and %7 outside, with Any at "
                                 "%8 against a fill of %9")
                                     .arg(accent.name(),
                                          anysSeam.name(),
                                          anysOuterEdge.name(),
                                          allsEdgeUnderAny.name(),
                                          allsFillUnderAny.name(),
                                          allsSeam.name(),
                                          allsOuterEdge.name(),
                                          anysEdgeUnderAll.name(),
                                          anysFillUnderAll.name());

        QVERIFY2(distanceBetween(anysSeam, accent) <= scmInkSlack,
                 qPrintable(qsl("with Any chosen its edge at the seam reads %1 against an accent of %2, so its outline is open there").arg(anysSeam.name(), accent.name())));
        QVERIFY2(distanceBetween(anysOuterEdge, accent) <= scmInkSlack,
                 qPrintable(qsl("with Any chosen its outer edge reads %1 rather than the accent, so the two sides are not one outline").arg(anysOuterEdge.name())));
        QVERIFY2(distanceBetween(allsEdgeUnderAny, allsFillUnderAny) <= scmInkSlack,
                 qPrintable(qsl("with Any chosen, All reads %1 at the seam against its own fill of %2 - it is still drawing a hairline of its own, so the seam is two lines")
                                    .arg(allsEdgeUnderAny.name(), allsFillUnderAny.name())));

        QVERIFY2(distanceBetween(allsSeam, accent) <= scmInkSlack,
                 qPrintable(qsl("with All chosen its edge at the seam reads %1 against an accent of %2, so its outline is open there").arg(allsSeam.name(), accent.name())));
        QVERIFY2(distanceBetween(allsOuterEdge, accent) <= scmInkSlack,
                 qPrintable(qsl("with All chosen its outer edge reads %1 rather than the accent, so the two sides are not one outline").arg(allsOuterEdge.name())));
        QVERIFY2(distanceBetween(anysEdgeUnderAll, anysFillUnderAll) <= scmInkSlack,
                 qPrintable(qsl("with All chosen, Any reads %1 at the seam against its own fill of %2 - it kept a hairline there, so the seam is two lines")
                                    .arg(anysEdgeUnderAll.name(), anysFillUnderAll.name())));

        // ...and the hairline changing hands moved nothing: what one segment
        // gives up in border the other takes back in padding
        qInfo().noquote() << qsl("  the segments are %1px and %2px in a %3px pair with Any chosen, and %4px and %5px in a %6px pair with All chosen")
                                     .arg(anyWidth)
                                     .arg(allWidth)
                                     .arg(pairWidth)
                                     .arg(pAny->width())
                                     .arg(pAll->width())
                                     .arg(pPair->width());
        QCOMPARE(pAny->width(), anyWidth);
        QCOMPARE(pAll->width(), allWidth);
        QCOMPARE(pPair->width(), pairWidth);
    }

    // (l) The mark a choice on the strip is made with is the design's in both
    // appearances, rather than the platform's - a grey Fusion square on the
    // dark theme, a blue rounded macOS box on the light one. The settings
    // dialog is held to the same measurement, so that a check box reads as the
    // same control in both windows.
    void test_theStripsCheckBoxCarriesTheOneMark()
    {
        const auto appearanceBefore = mudlet::self()->mAppearance;
        auto restore = qScopeGuard([this, appearanceBefore]() {
            takeTheEditorTo(appearanceBefore);
        });

        chooseTrigger(mpThreePatternRow);
        QCheckBox* pBox = form()->checkBox_perlSlashGOption;
        QVERIFY2(pBox->isVisible(), "the check box this case measures is not on show");
        const bool wasChecked = pBox->isChecked();
        auto putTheBoxBack = qScopeGuard([pBox, wasChecked]() {
            pBox->setChecked(wasChecked);
        });

        QStringList misses;
        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            takeTheEditorTo(appearance.second);
            // Proves the appearance actually moved: a run where it did not
            // would measure the same theme twice and pass
            QCOMPARE(uiDesign::themeTokens().darkPage, appearance.second == enums::Appearance::dark);

            pBox->setChecked(false);
            settle();
            const QImage empty = mpEditor->grab().toImage();
            const QRect box = markBoxIn(pBox, mpEditor, empty);
            QVERIFY2(box.width() > 4 && box.height() > 4, qPrintable(qsl("%1: the mark has no rectangle to sample: %2x%3").arg(appearance.first).arg(box.width()).arg(box.height())));

            // Well past the mark's own antialiased edge and short of the
            // words after it, which is what the gap named in choiceStyleSheet()
            // leaves room for
            const QColor behind = empty.pixelColor(qMin(box.right() + box.width() / 3, empty.width() - 1), box.center().y());
            const qreal outline = outlineRatioOf(empty, box, behind);
            const QColor fill = empty.pixelColor(box.center());
            const int nothingInIt = markedPixelsIn(empty, box, fill);

            pBox->setChecked(true);
            settle();
            const int aTick = markedPixelsIn(mpEditor->grab().toImage(), box, fill);

            qInfo().noquote() << qsl("  %1: the strip's mark is %2px on a fill of %3, outlined at %4:1 against %5, with %6 pixels in it empty and %7 checked")
                                         .arg(appearance.first)
                                         .arg(box.width())
                                         .arg(fill.name(), QString::number(outline, 'f', 2), behind.name())
                                         .arg(nothingInIt)
                                         .arg(aTick);
            if (outline < uiDesign::scmQuietMinimumRatio) {
                misses << qsl("%1: the mark is outlined at %2:1 against the form behind it").arg(appearance.first, QString::number(outline, 'f', 2));
            }
            if (nothingInIt > 0) {
                misses << qsl("%1: an unchecked box already has %2 pixels in it that are not its fill").arg(appearance.first).arg(nothingInIt);
            }
            if (aTick <= 0) {
                misses << qsl("%1: a checked box paints nothing inside itself, so there is no tick").arg(appearance.first);
            }
        }
        QVERIFY2(misses.isEmpty(), qPrintable(qsl("\n  %1").arg(misses.join(qsl("\n  ")))));
    }

    // (k) A spin box's steppers say they are being used: the chevron under the
    // pointer takes the accent, and gives it back when the pointer leaves
    void test_theStepperChevronTakesTheAccentUnderThePointer()
    {
        chooseTrigger(mpThreePatternRow);
        QSpinBox* pBox = form()->spinBox_stayOpen;
        QVERIFY2(pBox->isVisible(), "the spin box these steppers belong to is not on show");

        QStyleOptionSpinBox option;
        option.initFrom(pBox);
        option.subControls = QStyle::SC_All;
        const QRect upButton = pBox->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, pBox);
        QVERIFY2(!upButton.isEmpty(), "the spin box draws no stepper at all, so there is no chevron to light up");
        const QColor accent = uiDesign::themeTokens().accent;

        // Somewhere on the box that is not a stepper, which is where the
        // pointer is taken to put the chevron back
        const QPoint field(2, pBox->height() / 2);
        QTest::mouseMove(pBox, field);
        settle();
        const int atRest = nearestToTheAccentIn(pBox->grab().toImage(), upButton, accent);

        QTest::mouseMove(pBox, upButton.center());
        settle();
        const int hovered = nearestToTheAccentIn(pBox->grab().toImage(), upButton, accent);

        QTest::mouseMove(pBox, field);
        settle();
        const int afterwards = nearestToTheAccentIn(pBox->grab().toImage(), upButton, accent);

        qInfo().noquote() << qsl("  the up stepper is %1 and its nearest pixel to the accent %2 is %3 away at rest, %4 under the pointer and %5 after it left")
                                     .arg(qsl("%1x%2+%3+%4").arg(upButton.width()).arg(upButton.height()).arg(upButton.x()).arg(upButton.y()), accent.name())
                                     .arg(atRest)
                                     .arg(hovered)
                                     .arg(afterwards);

        QVERIFY2(atRest > scmChevronInkSlack, qPrintable(qsl("the resting stepper already has a pixel %1 from the accent, so nothing here says the pointer did anything").arg(atRest)));
        QVERIFY2(hovered <= scmChevronInkSlack, qPrintable(qsl("the stepper under the pointer comes no nearer the accent than %1, so its chevron did not light up").arg(hovered)));
        QVERIFY2(atRest - hovered >= scmChevronInkShift,
                 qPrintable(qsl("the pointer took the stepper from %1 to %2 of the accent, which is inside what the wash behind the chevron moves on its own").arg(atRest).arg(hovered)));
        QVERIFY2(afterwards > scmChevronInkSlack, qPrintable(qsl("the stepper is still %1 from the accent with the pointer off it, so the chevron never went back").arg(afterwards)));
    }

    // (m) The accent the mark's hairline takes on focus is the keyboard's. A
    // click that turned an option on and off again used to leave that accent
    // sitting on it until something else in the window was clicked, because
    // QAbstractButton asks QStyle::SH_Button_FocusPolicy once, in its
    // constructor, and keeps what that style answered - which is the style's
    // decision rather than the design's. The box is put back into the
    // Qt::StrongFocus a style answering that way leaves it in, and the form
    // restyled over it, so that this measures the shell rather than which
    // platform the run is on.
    void test_clickingAnOptionDoesNotLeaveItHoldingTheFocus()
    {
        const auto appearanceBefore = mudlet::self()->mAppearance;
        auto restore = qScopeGuard([this, appearanceBefore]() {
            takeTheEditorTo(appearanceBefore);
        });

        chooseTrigger(mpThreePatternRow);
        QCheckBox* pBox = form()->checkBox_perlSlashGOption;
        QVERIFY2(pBox->isVisible(), "the check box this case clicks is not on show");

        // An appearance the editor is not already in, because
        // applyEditorShellStyle() has nothing to redo when the four colours it
        // mixes everything from have not moved
        pBox->setFocusPolicy(Qt::StrongFocus);
        takeTheEditorTo(uiDesign::themeTokens().darkPage ? enums::Appearance::light : enums::Appearance::dark);
        QCOMPARE(pBox->focusPolicy(), Qt::TabFocus);

        const bool wasChecked = pBox->isChecked();
        auto putTheBoxBack = qScopeGuard([pBox, wasChecked]() {
            pBox->setChecked(wasChecked);
            // The focus is given to the box below, and no other case here
            // expects to find it there
            pBox->clearFocus();
        });

        QTest::mouseClick(pBox, Qt::LeftButton, Qt::NoModifier, pBox->rect().center());
        settle();
        QVERIFY2(pBox->isChecked() != wasChecked, "clicking the option left it where it was, so nothing here says the click landed");
        // The window's own focus child rather than hasFocus(), which answers
        // false on any run whose window never became the active one - and would
        // then pass whatever the policy said
        QVERIFY2(mpEditor->focusWidget() != pBox, "a click left the option holding the keyboard focus, so the accent stayed on its mark");
        QVERIFY2(!pBox->hasFocus(), "a click left the option focused");

        pBox->setFocus(Qt::TabFocusReason);
        settle();
        QVERIFY2(mpEditor->focusWidget() == pBox, "the Tab key cannot reach the option, so a keyboard user has nothing to see the accent on");
    }
};

#include "EditorTriggerOptionsStripTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorTriggerOptionsStripTest)
