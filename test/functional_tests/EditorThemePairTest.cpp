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
 * Mudlet ships two editor themes now, and they are a light/dark pair only
 * because of what they are called: the preferences already carry a profile from
 * a theme to its counterpart when the application appearance changes, matching
 * on the words "light" and "dark" in the name, and the light theme was renamed
 * from "Mudlet" to "Mudlet Light" so that rule picks it up. No code pairs them.
 *
 * The rename is what has to be paid for. A profile saved by any earlier Mudlet
 * carries the theme as "Mudlet" and the file as "Mudlet.tmTheme", and both forms
 * have to go on resolving - the file because that is what edbee is handed, the
 * name because that is what the theme list is searched for, and a search that
 * misses leaves the preferences showing no theme at all.
 *
 * Run with: ctest -R EditorThemePairTest -V
 */

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TelnetServerStub.h"
#include "dlgProfilePreferences.h"
#include "mudlet.h"

#include "GroupedTest.h"

class EditorThemePairTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    QTemporaryDir mCacheDir;
    QByteArray mSavedXdgCache;
    TelnetServerStub* mpServer = nullptr;
    Host* mpHost = nullptr;
    dlgProfilePreferences* mpPreferences = nullptr;
    const QString mProfileName = qsl("EditorThemePair-Test");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    // A theme that answers to nothing in the counterpart rule, standing in for
    // every downloaded theme that comes on its own
    const QString mLoneThemeName = qsl("PhaseDProbe");
    const QString mLoneThemeFile = qsl("PhaseDProbe.tmTheme");

    void deleteProfileDirectory(const QString& profileName)
    {
        QDir dir(mudlet::getMudletPath(enums::profileHomePath, profileName));
        if (dir.exists()) {
            dir.removeRecursively();
        }
    }

    // The list the downloaded themes are read from. One entry, so that the two
    // bundled themes are joined by a theme with no counterpart to its name.
    void writeEditorThemesFile()
    {
        const QString file = mudlet::getMudletPath(enums::editorWidgetThemeJsonFile);
        QVERIFY(QDir().mkpath(QFileInfo(file).absolutePath()));
        QFile themes(file);
        QVERIFY(themes.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray contents = qsl(R"([{"Title": "%1", "FileName": "%2"}])").arg(mLoneThemeName, mLoneThemeFile).toUtf8();
        QVERIFY(themes.write(contents) == contents.size());
    }

    void openPreferences()
    {
        mpPreferences = new dlgProfilePreferences(mudlet::self(), mpHost);
        mpPreferences->setStyleSheet(mpHost->mProfileStyleSheet);
        mpPreferences->resize(1060, 760);
        mpPreferences->show();
        QVERIFY(QTest::qWaitForWindowExposed(mpPreferences));
    }

    QComboBox* themeBox() const { return mpPreferences->code_editor_theme_selection_combobox; }

    // Through the control, which is what the appearance slot is wired to - the
    // slot itself is private, and the point of these cases is the path a user
    // takes to it
    void setAppearance(const enums::Appearance state)
    {
        mpPreferences->comboBox_appearance->setCurrentIndex(state);
        QCoreApplication::processEvents();
    }

    QStringList themesOffered() const
    {
        QStringList names;
        for (int index = 0; index < themeBox()->count(); ++index) {
            names << themeBox()->itemText(index);
        }
        return names;
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
        QVERIFY(mCacheDir.isValid());
        mSavedXdgCache = qgetenv("XDG_CACHE_HOME");
        qputenv("XDG_CACHE_HOME", mCacheDir.path().toUtf8());

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
        writeEditorThemesFile();

        mpHost = TestProfile::create(mProfileName, mLocalhost, mPort);
        QVERIFY2(mpHost, "No active host after profile creation");
    }

    void cleanupTestCase()
    {
        mpHost = nullptr;
        delete mpServer;
        mpServer = nullptr;
        if (mudlet::self()) {
            deleteProfileDirectory(mProfileName);
            delete mudlet::self();
        }
        mSavedXdg.isNull() ? qunsetenv("XDG_CONFIG_HOME") : qputenv("XDG_CONFIG_HOME", mSavedXdg);
        mSavedXdgCache.isNull() ? qunsetenv("XDG_CACHE_HOME") : qputenv("XDG_CACHE_HOME", mSavedXdgCache);
    }

    void cleanup()
    {
        delete mpPreferences;
        mpPreferences = nullptr;
        mudlet::self()->setAppearance(enums::Appearance::light);
        QCoreApplication::processEvents();
    }

    // Both are carried in the resource file rather than downloaded, and the
    // light one answers to the file name it had before the rename as well - so
    // a profile whose stored file name has not been brought forward still gets
    // a theme rather than a path into a cache directory that has no such file
    void test_bothBundledThemesResolveToTheResourceFile()
    {
        QCOMPARE(mudlet::getMudletPath(enums::editorWidgetThemePathFile, mudlet::scmEditorThemeFileLight), qsl(":/edbee_defaults/Mudlet Light.tmTheme"));
        QCOMPARE(mudlet::getMudletPath(enums::editorWidgetThemePathFile, mudlet::scmEditorThemeFileDark), qsl(":/edbee_defaults/Mudlet Dark.tmTheme"));
        QCOMPARE(mudlet::getMudletPath(enums::editorWidgetThemePathFile, mudlet::scmEditorThemeFileLegacy), qsl(":/edbee_defaults/Mudlet Light.tmTheme"));

        QVERIFY2(QFile::exists(qsl(":/edbee_defaults/Mudlet Light.tmTheme")), "the light theme is not in the resource file");
        QVERIFY2(QFile::exists(qsl(":/edbee_defaults/Mudlet Dark.tmTheme")), "the dark theme is not in the resource file");

        // A downloaded theme still goes to the cache it was downloaded into
        QVERIFY2(!mudlet::getMudletPath(enums::editorWidgetThemePathFile, mLoneThemeFile).startsWith(QLatin1Char(':')), "a downloaded theme is being looked for in the resource file");

        QVERIFY2(mudlet::loadEdbeeTheme(mudlet::scmEditorThemeNameDark, mudlet::scmEditorThemeFileDark), "the dark theme did not load - it is not a theme file edbee can read");
    }

    // What a profile saved by an earlier Mudlet carries. The theme list no
    // longer offers "Mudlet", so without the migration the preferences would
    // open on no theme at all - and writing that back would wipe the profile's
    // choice.
    void test_aProfileSavedBeforeTheRenameOpensOnTheLightTheme()
    {
        QString name = qsl("Mudlet");
        QString file = qsl("Mudlet.tmTheme");
        mudlet::migrateBundledEditorTheme(name, file);
        QCOMPARE(name, QString(mudlet::scmEditorThemeNameLight));
        QCOMPARE(file, QString(mudlet::scmEditorThemeFileLight));

        mpHost->mEditorTheme = qsl("Mudlet");
        mpHost->mEditorThemeFile = qsl("Mudlet.tmTheme");
        mpHost->mEditorThemeDark.clear();
        mpHost->mEditorThemeFileDark.clear();
        mudlet::migrateBundledEditorTheme(mpHost->mEditorTheme, mpHost->mEditorThemeFile);

        openPreferences();
        QVERIFY2(themesOffered().contains(mudlet::scmEditorThemeNameLight),
                 qPrintable(qsl("the theme list does not offer the light theme at all, it offers: %1").arg(themesOffered().join(qsl(", ")))));
        QCOMPARE(themeBox()->currentText(), QString(mudlet::scmEditorThemeNameLight));
        QCOMPARE(themeBox()->currentData().toString(), QString(mudlet::scmEditorThemeFileLight));
        QCOMPARE(mpHost->getEditorTheme(), QString(mudlet::scmEditorThemeNameLight));
        QCOMPARE(mpHost->getEditorThemeFile(), QString(mudlet::scmEditorThemeFileLight));
    }

    // The pairing, in both directions, through the counterpart rule that was
    // already there. Nothing names either theme: they pair because one is
    // called Light and the other Dark.
    void test_theTwoBundledThemesPairOnTheirNames()
    {
        mpHost->mEditorTheme = mudlet::scmEditorThemeNameLight;
        mpHost->mEditorThemeFile = mudlet::scmEditorThemeFileLight;
        mpHost->mEditorThemeDark.clear();
        mpHost->mEditorThemeFileDark.clear();

        openPreferences();
        QCOMPARE(themeBox()->currentText(), QString(mudlet::scmEditorThemeNameLight));

        setAppearance(enums::Appearance::dark);
        QCOMPARE(themeBox()->currentText(), QString(mudlet::scmEditorThemeNameDark));
        QCOMPARE(themeBox()->currentData().toString(), QString(mudlet::scmEditorThemeFileDark));
        // The theme it came from is kept as the light choice, so going back is
        // a return rather than a guess
        QCOMPARE(mpHost->mEditorTheme, QString(mudlet::scmEditorThemeNameLight));
        QCOMPARE(mpHost->mEditorThemeFile, QString(mudlet::scmEditorThemeFileLight));

        setAppearance(enums::Appearance::light);
        QCOMPARE(themeBox()->currentText(), QString(mudlet::scmEditorThemeNameLight));
        QCOMPARE(mpHost->mEditorThemeDark, QString(mudlet::scmEditorThemeNameDark));
        QCOMPARE(mpHost->mEditorThemeFileDark, QString(mudlet::scmEditorThemeFileDark));
    }

    // Nothing is forced. A theme with no counterpart to its name is what the
    // user asked for in both appearances, and an appearance change leaves it
    // exactly where it is - which is also what lets someone on dark mode keep
    // the light theme by choosing it there.
    void test_aThemeWithNoCounterpartIsLeftAlone()
    {
        mpHost->mEditorTheme = mLoneThemeName;
        mpHost->mEditorThemeFile = mLoneThemeFile;
        mpHost->mEditorThemeDark.clear();
        mpHost->mEditorThemeFileDark.clear();

        openPreferences();
        QCOMPARE(themeBox()->currentText(), mLoneThemeName);

        setAppearance(enums::Appearance::dark);
        QVERIFY2(themeBox()->currentText() == mLoneThemeName, qPrintable(qsl("going dark moved a theme with no counterpart from %1 to %2").arg(mLoneThemeName, themeBox()->currentText())));
        QCOMPARE(mpHost->mEditorTheme, mLoneThemeName);

        setAppearance(enums::Appearance::light);
        QCOMPARE(themeBox()->currentText(), mLoneThemeName);
    }
};

#include "EditorThemePairTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorThemePairTest)
