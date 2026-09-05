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
 * The picture beside a notice in the editor.
 *
 * It is tinted through its alpha channel, which keeps the antialiased edges a
 * recolouring of the pixels would harden - but also keeps only the shape that
 * channel carries. The old dialog-*.png bitmaps drew their picture in colour
 * over a solid alpha, so tinting one gave back a filled disc with nothing in
 * it: the information notice was led by a plain blob.
 *
 * The three sources are line glyphs now, and what tells the two apart is a row
 * of pixels across the middle: a ring has transparent pixels between its two
 * sides, a disc has none.
 *
 * Run with: ctest -R EditorNoticeGlyphTest -V
 */

#include <QImage>
#include <QLabel>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest/QtTest>
#include <chrono>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgSystemMessageArea.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorNoticeGlyphTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorNoticeGlyph-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    // Anything above this is ink rather than the antialiased edge of it
    static constexpr int scmInkAlpha = 128;

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

    // The row of pixels across the middle of a glyph, as the ink it is drawn in
    // rather than as colours
    static QString describeMiddleRow(const QImage& glyph)
    {
        QString marks;
        const int middle = glyph.height() / 2;
        for (int x = 0; x < glyph.width(); ++x) {
            marks.append(glyph.pixelColor(x, middle).alpha() > scmInkAlpha ? QLatin1Char('#') : QLatin1Char('.'));
        }
        return marks;
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
        mpEditor->resize(1000, 800);
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

    void test_theInformationNoticeIsLedByARingRatherThanADisc()
    {
        mpEditor->slot_showTriggers();
        //: Placeholder notice a test puts up to read the picture beside it
        mpEditor->showInfo(qsl("A notice with a picture beside it"));
        QCoreApplication::processEvents();
        QTest::qWait(50ms);

        QLabel* pGlyphLabel = mpEditor->mpSystemMessageArea->notificationAreaIconLabelInformation;
        QVERIFY2(pGlyphLabel->isVisible(), "the information notice is not showing its picture");
        const QImage glyph = pGlyphLabel->pixmap().toImage();
        QVERIFY2(glyph.width() > 8 && glyph.height() > 8, "the picture beside the notice is too small to read a row out of");

        const QString row = describeMiddleRow(glyph);
        qInfo().noquote() << qsl("  the middle row of the notice's picture reads %1").arg(row);

        const qsizetype firstInk = row.indexOf(QLatin1Char('#'));
        const qsizetype lastInk = row.lastIndexOf(QLatin1Char('#'));
        QVERIFY2(firstInk >= 0 && lastInk > firstInk, qPrintable(qsl("the picture beside the notice has no shape in it at all: %1").arg(row)));
        QVERIFY2(row.mid(firstInk, lastInk - firstInk + 1).contains(QLatin1Char('.')),
                 qPrintable(qsl("the picture beside the information notice is a solid disc rather than a ring - tinting kept only its alpha, and that source's alpha is a filled shape: %1").arg(row)));
    }

    // The other two are drawn from the same three-file set, so a source that
    // went back to a filled bitmap is caught wherever it happened
    void test_theWarningAndErrorNoticesAreLineGlyphsToo()
    {
        mpEditor->slot_showTriggers();
        mpEditor->showWarning(qsl("A warning with a picture beside it"), false);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        const QString warningRow = describeMiddleRow(mpEditor->mpSystemMessageArea->notificationAreaIconLabelWarning->pixmap().toImage());

        mpEditor->showError(qsl("An error with a picture beside it"));
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        const QString errorRow = describeMiddleRow(mpEditor->mpSystemMessageArea->notificationAreaIconLabelError->pixmap().toImage());

        qInfo().noquote() << qsl("  the warning's picture reads %1 and the error's %2").arg(warningRow, errorRow);

        for (const QString& row : {warningRow, errorRow}) {
            const qsizetype firstInk = row.indexOf(QLatin1Char('#'));
            const qsizetype lastInk = row.lastIndexOf(QLatin1Char('#'));
            QVERIFY2(firstInk >= 0 && lastInk > firstInk, qPrintable(qsl("a notice's picture has no shape in it at all: %1").arg(row)));
            QVERIFY2(row.mid(firstInk, lastInk - firstInk + 1).contains(QLatin1Char('.')), qPrintable(qsl("a notice's picture is a filled shape rather than a line glyph: %1").arg(row)));
        }
    }

    // The banner's other picture. It shipped as application-exit.png, a
    // full-colour bitmap of a red cross - which said "error" on a banner that is
    // usually a tip, and was the one thing on it not inked from the palette.
    void test_theDismissCrossIsInkedFromThePaletteLikeTheNoticeBesideIt()
    {
        mpEditor->slot_showTriggers();
        mpEditor->showInfo(qsl("A notice the reader can dismiss"));
        QCoreApplication::processEvents();
        QTest::qWait(50ms);

        QToolButton* pClose = mpEditor->mpSystemMessageArea->messageAreaCloseButton;
        QVERIFY2(pClose, "the banner has no close button");
        const QImage cross = pClose->icon().pixmap(pClose->iconSize()).toImage();
        QVERIFY2(!cross.isNull(), "the banner's close button carries no picture at all");

        // Tinting keeps only the alpha, so every pixel with ink in it is the one
        // colour. The red cross was full-colour, and would fail on both counts.
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        QSet<QRgb> inks;
        for (int y = 0; y < cross.height(); ++y) {
            for (int x = 0; x < cross.width(); ++x) {
                const QColor pixel = cross.pixelColor(x, y);
                if (pixel.alpha() >= scmInkAlpha) {
                    inks.insert(qRgb(pixel.red(), pixel.green(), pixel.blue()));
                }
            }
        }
        QVERIFY2(!inks.isEmpty(), "the banner's close button has no shape in it");
        QCOMPARE(inks.size(), 1);
        QCOMPARE(QColor(*inks.cbegin()).name(), tokens.mutedText.name());
    }
};

#include "EditorNoticeGlyphTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorNoticeGlyphTest)
