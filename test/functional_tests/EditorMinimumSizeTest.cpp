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
 * The script editor has to open at the size the user last left it at, and to
 * be dragged small again afterwards. Both are decided by the minimum its
 * layout reports: the size restore floors a stored size with it, and Qt gives
 * the same figure to the window as its minimum size.
 *
 * That minimum used to be measured before it meant anything. The editor's
 * seven main areas are hidden in the constructor, but hiding a widget only
 * propagates upwards while it is still visible, so their containers went on
 * reporting a minimum measured with all seven stacked on screen - around
 * 1600px. No event loop runs before the size restore reads it, so the editor
 * opened at screen height however small the stored geometry was.
 */

#include <QTemporaryDir>
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

class EditorMinimumSizeTest : public QObject {
  Q_OBJECT

private:
  QTemporaryDir mConfigDir;
  QByteArray mSavedXdg;
  TelnetServerStub *mpServer = nullptr;
  dlgTriggerEditor *mpEditor = nullptr;
  Host *mpHost = nullptr;
  const QString mProfileName = qsl("EditorMinimumSize-Test-Profile");
  QString mPort; // assigned the stub's actual ephemeral port in initTestCase()
  const QString mLocalhost = qsl("localhost");
  QSize mStoredSize; // seeded in initTestCase() before the editor is built

  // Kept in step with the bound the editor is allowed to claim, so a failure
  // reports the offending widgets rather than only the total
  static constexpr int scmMaxMinimumHeight = 600;
  static constexpr int scmMaxMinimumWidth = 900;

  void deleteProfileDirectory(const QString &profileName) {
    const QString path =
        mudlet::getMudletPath(enums::profileHomePath, profileName);
    QDir dir(path);
    if (dir.exists()) {
      dir.removeRecursively();
    }
  }

  void startProfile(const QString &profileName, const QString &address,
                    const QString &port) {
    mpHost = TestProfile::create(profileName, address, port);
    if (!mpHost) {
      QFAIL("No active host available for the test.");
    }

    QSignalSpy spy(&(mpHost->mTelnet), &cTelnet::signal_connected);
    if (!spy.wait(1000)) {
      QFAIL("Could not connect with the host.");
    }
  }

  // Every widget whose own minimum height is at least threshold, deepest
  // first, so the report names the widget that actually carries the number
  // rather than only the ancestors that inherit it
  QString tallWidgetReport(const int threshold) const {
    QString report;
    const QList<QWidget *> children = mpEditor->findChildren<QWidget *>();
    for (const QWidget *pWidget : children) {
      const int hint = pWidget->minimumSizeHint().height();
      const int floor = pWidget->minimumHeight();
      if (qMax(hint, floor) < threshold) {
        continue;
      }
      report.append(qsl("\n  %1 (%2) hint=%3 min=%4 max=%5 vis=%6")
                        .arg(pWidget->objectName().isEmpty()
                                 ? qsl("<unnamed>")
                                 : pWidget->objectName(),
                             QString::fromLatin1(pWidget->metaObject()->className()),
                             QString::number(hint), QString::number(floor),
                             QString::number(pWidget->maximumHeight()),
                             pWidget->isVisible() ? qsl("y") : qsl("n")));
    }
    return report;
  }

private slots:
  void initTestCase() {
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
    QVERIFY2(mpServer->isListening(),
             qPrintable(qsl("TelnetServerStub failed to start: %1")
                            .arg(mpServer->errorString())));
    mPort = QString::number(mpServer->serverPort());
    mudlet::start();
    mudlet::self()->setupConfig();
    QCOMPARE(mudlet::getMudletPath(enums::mainPath),
             qsl("%1/mudlet").arg(mConfigDir.path()));
    mudlet::self()->takeOwnershipOfInstanceCoordinator(
        std::make_unique<MudletInstanceCoordinator>(
            "MudletInstanceCoordinator"));
    mudlet::self()->init();
    mudlet::self()->setStorePasswordsSecurely(false);
    deleteProfileDirectory(mProfileName);

    // Seeded before the profile is up, because the editor is built with it and
    // what went wrong was decided in that constructor: the size restore reads
    // this, and the layout pass right after it used to overrule what it read
    const QScreen *pScreen = QGuiApplication::primaryScreen();
    QVERIFY(pScreen);
    // The size restore deliberately keeps a stored size inside the desktop it
    // opens on, and the offscreen platform's screen is smaller than a real
    // one, so ask for a size this screen can actually hold
    mStoredSize = QSize(989, 643).boundedTo(pScreen->availableGeometry().size());
    QVERIFY2(mStoredSize.height() >= 400,
             "The screen is too small for this test to say anything");
    mudlet::getQSettings()->setValue(qsl("script_editor_size"), mStoredSize);
    mudlet::getQSettings()->remove(qsl("script_editor_pos"));

    startProfile(mProfileName, mLocalhost, mPort);

    mudlet::self()->slot_showScriptDialog();
    QTest::qWait(100ms);

    mpEditor = mpHost->mpEditorDialog;
    QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
  }

  void cleanupTestCase() {
    mpEditor = nullptr;
    mpHost = nullptr;
    delete mpServer;
    mpServer = nullptr;
    if (mudlet::self()) {
      deleteProfileDirectory(mProfileName);
      delete mudlet::self();
    }
    mSavedXdg.isNull() ? qunsetenv("XDG_CONFIG_HOME")
                       : qputenv("XDG_CONFIG_HOME", mSavedXdg);
  }

  // The floor restoreWindowGeometry() applies to a stored size, and the floor
  // a window manager applies to a drag, are both this hint
  void test_editorMinimumSizeHintStaysSmall() {
    const QSize hint = mpEditor->minimumSizeHint();

    QVERIFY2(hint.height() <= scmMaxMinimumHeight,
             qPrintable(qsl("The editor's minimum height is %1, over the %2 it "
                            "is allowed - it will override a smaller stored "
                            "size and cannot be dragged smaller. Widgets at "
                            "least %2 tall:%3")
                            .arg(QString::number(hint.height()),
                                 QString::number(scmMaxMinimumHeight),
                                 tallWidgetReport(scmMaxMinimumHeight))));
    QVERIFY2(hint.width() <= scmMaxMinimumWidth,
             qPrintable(qsl("The editor's minimum width is %1, over the %2 it "
                            "is allowed")
                            .arg(QString::number(hint.width()),
                                 QString::number(scmMaxMinimumWidth))));
  }

  // The editor has to open at the size it was left at. It used to open at
  // screen height instead: the seven main areas are hidden in the constructor,
  // but their containers kept a minimum measured while all seven were on
  // screen, and the first layout pass gave the window that as its minimum
  // height - overruling the stored size before anyone could see it.
  void test_editorOpensAtStoredSize() {
    QVERIFY2(mpEditor->size() == mStoredSize,
             qPrintable(qsl("The editor was left at %1x%2 but opened at %3x%4. "
                            "Its minimum is %5x%6.")
                            .arg(QString::number(mStoredSize.width()),
                                 QString::number(mStoredSize.height()),
                                 QString::number(mpEditor->size().width()),
                                 QString::number(mpEditor->size().height()),
                                 QString::number(mpEditor->minimumSize().width()),
                                 QString::number(mpEditor->minimumSize().height()))));
  }

  // Whatever the hint says, the window has to actually go there: the size a
  // user can drag it to is the one Qt derived from the layout, and a resize
  // that comes back larger than it was asked for is a minimum standing in
  // the way. Restores the opening size afterwards, for cases that follow.
  void test_editorResizesDownToASmallWindow() {
    const QSize small(700, 500);
    mpEditor->resize(small);
    QCoreApplication::processEvents();
    const QSize reached = mpEditor->size();
    mpEditor->resize(mStoredSize);
    QCoreApplication::processEvents();

    QVERIFY2(reached == small,
             qPrintable(qsl("Asked for %1x%2, got %3x%4 - the editor cannot be "
                            "made that small. Its minimum is %5x%6.")
                            .arg(QString::number(small.width()),
                                 QString::number(small.height()),
                                 QString::number(reached.width()),
                                 QString::number(reached.height()),
                                 QString::number(mpEditor->minimumSize().width()),
                                 QString::number(mpEditor->minimumSize().height()))));
  }
};

#include "EditorMinimumSizeTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorMinimumSizeTest)
