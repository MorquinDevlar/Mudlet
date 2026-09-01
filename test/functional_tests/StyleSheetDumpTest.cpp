/*
 * Throwaway harness: dumps every generated stylesheet in the settings dialog
 * and in the editor window, so two trees can be compared byte for byte.
 * NOT part of the suite - added by hand to both trees for one comparison and
 * removed again.
 */

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <chrono>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TelnetServerStub.h"
#include "dlgProfilePreferences.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class StyleSheetDumpTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QTemporaryDir mCacheDir;
    QByteArray mSavedXdg;
    QByteArray mSavedXdgCache;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    dlgProfilePreferences* mpPreferences = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("StyleSheetDump-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    void deleteProfileDirectory(const QString& profileName)
    {
        QDir dir(mudlet::getMudletPath(enums::profileHomePath, profileName));
        if (dir.exists()) {
            dir.removeRecursively();
        }
    }

    static QStringList sheetsOf(const QString& label, QWidget* pRoot)
    {
        QStringList out;
        QList<QWidget*> all{pRoot};
        all << pRoot->findChildren<QWidget*>();
        for (QWidget* pWidget : all) {
            const QString sheet = pWidget->styleSheet();
            if (sheet.isEmpty()) {
                continue;
            }
            out << qsl("%1\t%2\t%3\t%4").arg(label, pWidget->metaObject()->className(), pWidget->objectName(), sheet);
        }
        out.sort();
        return out;
    }

private slots:
    void initTestCase()
    {
        if (portableMarkerPresent()) {
            QSKIP("portable.txt present");
        }

        QVERIFY(mConfigDir.isValid());
        QVERIFY(QDir().mkpath(qsl("%1/mudlet/profiles").arg(mConfigDir.path())));
        mSavedXdg = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", mConfigDir.path().toUtf8());
        QVERIFY(mCacheDir.isValid());
        mSavedXdgCache = qgetenv("XDG_CACHE_HOME");
        qputenv("XDG_CACHE_HOME", mCacheDir.path().toUtf8());

        mpServer = new TelnetServerStub(qApp);
        mpServer->start(mLocalhost, 0);
        mPort = QString::number(mpServer->serverPort());
        mudlet::start();
        mudlet::self()->setupConfig();
        mudlet::self()->takeOwnershipOfInstanceCoordinator(std::make_unique<MudletInstanceCoordinator>(qsl("MudletInstanceCoordinator")));
        mudlet::self()->init();
        mudlet::self()->setStorePasswordsSecurely(false);
        deleteProfileDirectory(mProfileName);

        mpHost = TestProfile::create(mProfileName, mLocalhost, mPort);
        QVERIFY(mpHost);
        QSignalSpy spy(&(mpHost->mTelnet), &cTelnet::signal_connected);
        QVERIFY(spy.wait(1000));

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY(mpEditor);
        mpEditor->resize(1000, 800);
        mpEditor->slot_showTriggers();
        QTest::qWait(100ms);

        mpPreferences = new dlgProfilePreferences(mudlet::self(), mpHost);
        mpPreferences->resize(1060, 760);
        mpPreferences->show();
        QVERIFY(QTest::qWaitForWindowExposed(mpPreferences));
        QTest::qWait(100ms);
    }

    void cleanupTestCase()
    {
        mSavedXdg.isNull() ? qunsetenv("XDG_CONFIG_HOME") : qputenv("XDG_CONFIG_HOME", mSavedXdg);
        mSavedXdgCache.isNull() ? qunsetenv("XDG_CACHE_HOME") : qputenv("XDG_CACHE_HOME", mSavedXdgCache);
    }

    void test_dump()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        QStringList out;
        out << qsl("TOKENS\tpage=%1 card=%2 field=%3 border=%4 text=%5 muted=%6 disabled=%7 accent=%8 accentText=%9")
                       .arg(tokens.page.name(), tokens.card.name(), tokens.field.name(), tokens.border.name(), tokens.text.name(), tokens.mutedText.name(), tokens.disabledText.name(),
                            tokens.accent.name(), tokens.accentText.name())
            << qsl("TOKENS\thoverSoft=%1 accentSoft=%2 marker=%3 darkPage=%4").arg(tokens.hoverSoft, tokens.accentSoft, tokens.marker.name()).arg(tokens.darkPage);
        out << sheetsOf(qsl("SETTINGS"), mpPreferences);
        out << sheetsOf(qsl("EDITOR"), mpEditor);

        const QString path = QString::fromLocal8Bit(qgetenv("MUDLET_STYLESHEET_DUMP"));
        QVERIFY2(!path.isEmpty(), "MUDLET_STYLESHEET_DUMP is not set");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        // One rule per line, so a diff points at the rule that moved
        QString text = out.join(QChar('\n'));
        text.replace(qsl("; "), qsl(";\n    "));
        text.replace(qsl("}"), qsl("}\n"));
        file.write(text.toUtf8());
        file.write("\n");
        file.close();
    }
};

#include "StyleSheetDumpTest.moc"
MUDLET_GROUPED_TEST_MAIN(StyleSheetDumpTest)
