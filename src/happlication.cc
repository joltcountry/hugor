/* Copyright 2015 Nikos Chantziaras
 *
 * This file is part of Hugor.
 *
 * Hugor is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * Hugor is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * Hugor.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Additional permission under GNU GPL version 3 section 7
 *
 * If you modify this Program, or any covered work, by linking or combining it
 * with the Hugo Engine (or a modified version of the Hugo Engine), containing
 * parts covered by the terms of the Hugo License, the licensors of this
 * Program grant you additional permission to convey the resulting work.
 * Corresponding Source for a non-source form of such a combination shall
 * include the source code for the parts of the Hugo Engine used as well as
 * that of the covered work.
 */
#include <QDebug>
#include <QLayout>
#include <QLabel>
#include <QIcon>
#include <QStatusBar>
#include <QDir>
#include <QTextCodec>
#include <QMessageBox>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QStyle>
#include <QMenuBar>
#include <QDesktopWidget>

extern "C" {
#include "heheader.h"
}
#include "happlication.h"
#include "hmainwindow.h"
#include "hmarginwidget.h"
#include "hframe.h"
#include "settings.h"
#include "settingsoverrides.h"
#include "hugodefs.h"
#include "enginerunner.h"
#include "hugohandlers.h"
#include "videoplayer.h"


HApplication* hApp = 0;


HApplication::HApplication( int& argc, char* argv[], const char* appName,
                            const char* appVersion, const char* orgName,
                            const char* orgDomain )
    : QApplication(argc, argv),
      fBottomMarginSize(0),
      fGameRunning(false),
      fHugoCodec(QTextCodec::codecForName("Windows-1252")),
      fDesktopIsGnome(false)
{
    //qDebug() << Q_FUNC_INFO;
    Q_ASSERT(hApp == 0);

    // Check if a config file with the same basename as ours exists in our
    // directory.  If yes, we will override default settings from it.
    QString cfgFname = QApplication::applicationDirPath();
    if (not cfgFname.endsWith('/')) {
        cfgFname += '/';
    }
    cfgFname += QFileInfo(QApplication::applicationFilePath()).baseName();
    cfgFname += ".cfg";
    if (not QFileInfo(cfgFname).exists()) {
        cfgFname.clear();
    }
    SettingsOverrides* settOvr;
    if (cfgFname.isEmpty()) {
        settOvr = 0;
    } else {
        settOvr = new SettingsOverrides(cfgFname);
    }

    this->setApplicationName(QString::fromLatin1(appName));
    this->setApplicationVersion(QString::fromLatin1(appVersion));
    this->setOrganizationName(QString::fromLatin1(orgName));
    this->setOrganizationDomain(QString::fromLatin1(orgDomain));

    // Possibly override application and organization names.
    if (settOvr) {
        // Make sure that appName and authorName are either both set or unset.
        // This avoids mixing up the system file/registry paths for the settings
        // with our default ones.
        if ((settOvr->appName.isEmpty() and not settOvr->authorName.isEmpty())
            or (settOvr->authorName.isEmpty() and not settOvr->appName.isEmpty()))
        {
            settOvr->appName.clear();
            settOvr->authorName.clear();
        }

        if (not settOvr->appName.isEmpty()) {
            this->setApplicationName(settOvr->appName);
        }
        if (not settOvr->authorName.isEmpty()) {
            this->setOrganizationName(settOvr->authorName);
        }
    }

#ifdef Q_OS_UNIX
    // Detect whether we're running in Gnome.
    QDialogButtonBox::ButtonLayout layoutPolicy
        = QDialogButtonBox::ButtonLayout(QApplication::style()->styleHint(QStyle::SH_DialogButtonLayout));
    if (layoutPolicy == QDialogButtonBox::GnomeLayout) {
        this->fDesktopIsGnome = true;
    } else {
        this->fDesktopIsGnome = false;
    }
#endif

    // Load our persistent settings.
    this->fSettings = new Settings;
    this->fSettings->loadFromDisk(settOvr);

    // Apply the smart formatting setting.
    smartformatting = this->fSettings->smartFormatting;

    // Set our global pointer.
    hApp = this;

    // Create our main application window.
    this->fMainWin = new HMainWindow(0);
    this->fMainWin->setWindowTitle(this->applicationName());
    // Disable screen updates until we're actually ready to run a game. This
    // prevents screen flicker due to the resizing and background color changes
    // if we're starting in fullscreen mode.
    this->fMainWin->setUpdatesEnabled(false);

    // This widget provides margins for fFrameWin.
    this->fMarginWidget = new HMarginWidget(this->fMainWin);

    this->fFrameWin = new HFrame(this->fMarginWidget);
    this->fMarginWidget->addWidget(this->fFrameWin);
    this->updateMargins(-1);
    this->fMainWin->setCentralWidget(this->fMarginWidget);

    if (settOvr and settOvr->hideMenuBar) {
        this->fMainWin->hideMenuBar();
    }

    // Restore the application's size.
    this->fMainWin->resize(this->fSettings->appSize);

    // Set application window icon, unless we're on OS X where the bundle
    // icon is used.
#ifndef Q_OS_MAC
    this->setWindowIcon(QIcon(":/he_32-bit_48x48.png"));
#endif
    delete settOvr;

    hHandlers = new HugoHandlers(this);
}


HApplication::~HApplication()
{
    //qDebug() << Q_FUNC_INFO;
    Q_ASSERT(hHandlers != 0);
    delete hHandlers;
    hHandlers = 0;
    Q_ASSERT(hApp != 0);
    this->fSettings->saveToDisk();
    delete this->fSettings;
    delete this->fMainWin;
    // We're being destroyed, so our global pointer is no longer valid.
    hApp = 0;
}


void
HApplication::fRunGame()
{
    if (this->fNextGame.isEmpty()) {
        // Nothing to run.
        return;
    }

    while (not this->fNextGame.isEmpty()) {
        QFileInfo finfo(QFileInfo(this->fNextGame).absoluteFilePath());
        this->fNextGame.clear();

        // Remember the directory of the game.
        this->fSettings->lastFileOpenDir = finfo.absolutePath();

        // Change to the game file's directory.
        QDir::setCurrent(finfo.absolutePath());

        // Set the application's window title to contain the filename of
        // the game we're running. The game is free to change that later on.
#ifdef Q_OS_MAC
        // Just use the filename on OS X.  Seems to be the norm there.
        //qWinGroup->setWindowTitle(finfo.fileName());
#else
        // On all other systems, also append the application name.
        //qWinGroup->setWindowTitle(finfo.fileName() + QString::fromLatin1(" - ") + qFrame->applicationName());
#endif

        // Add the game file to our "recent games" list.
        QStringList& gamesList = this->fSettings->recentGamesList;
        int recentIdx = gamesList.indexOf(finfo.absoluteFilePath());
        if (recentIdx > 0) {
            // It's already in the list and it's not the first item.  Make
            // it the first item so that it becomes the most recent entry.
            gamesList.move(recentIdx, 0);
        } else if (recentIdx < 0) {
            // We didn't find it in the list by absoluteFilePath(). Try to
            // find it by canonicalFilePath() instead. This way, we avoid
            // listing the same game twice if the user opened it through a
            // different path (through a symlink that leads to the same
            // file, for instance.)
            bool found = false;
            const QString& canonPath = finfo.canonicalFilePath();
            for (recentIdx = 0; recentIdx < gamesList.size() and not found; ++recentIdx) {
                if (QFileInfo(gamesList.at(recentIdx)).canonicalFilePath() == canonPath) {
                    found = true;
                }
            }
            if (found) {
                gamesList.move(recentIdx - 1, 0);
            } else {
                // It's not in the list.  Prepend it as the most recent item
                // and, if the list is full, delete the oldest one.
                if (gamesList.size() >= this->fSettings->recentGamesCapacity) {
                    gamesList.removeLast();
                }
                gamesList.prepend(finfo.absoluteFilePath());
            }
        }
        this->fSettings->saveToDisk();

        // Run the Hugo engine.
        this->fGameRunning = true;
        this->fGameFile = finfo.absoluteFilePath();
        this->fMainWin->setUpdatesEnabled(true);
        this->fMainWin->raise();
        this->fMainWin->activateWindow();
        fHugoThread = new EngineThread(this);
        fHugoThread->setObjectName("engine");
        fEngineRunner = new EngineRunner(fGameFile, fHugoThread);
        fEngineRunner->moveToThread(fHugoThread);
        connect(fEngineRunner, SIGNAL(finished()), fHugoThread, SLOT(quit()));
        connect(fHugoThread, SIGNAL(started()), fEngineRunner, SLOT(runEngine()));
        connect(fHugoThread, SIGNAL(finished()), fEngineRunner, SLOT(deleteLater()));
        connect(fHugoThread, SIGNAL(finished()), fHugoThread, SLOT(deleteLater()));
        connect(fHugoThread, SIGNAL(finished()), SLOT(handleEngineFinished()));
        fHugoThread->start();
    }
}


void
HApplication::fUpdateMarginColor( int color )
{
    if (color < 0)
        return;

    const Settings* sett = hApp->settings();
    const QColor& qColor = (hMainWin->isFullScreen() and sett->customFsMarginColor)
                           ? sett->fsMarginColor
                           : hugoColorToQt(color);
    fMarginWidget->setColor(qColor);
}


void
HApplication::updateMargins( int color )
{
    int scrWidth = QApplication::desktop()->screenGeometry().width();
    int margin;

    // In fullscreen mode, respect the aspect ratio and max width settings.
    if (hMainWin->isFullScreen()) {
        int maxWidth = fSettings->fullscreenWidth * scrWidth / 100;

        // Calculate how big the margin should be to get the specified
        // width.
        int targetWidth = qMin(maxWidth, this->fMarginWidget->width());
        margin = (fMarginWidget->width() - targetWidth) / 2;
    } else {
        // In windowed mode, do not update the margins if we're currently
        // displaying scrollback as an overlay.
        if (fMarginWidget->layout()->indexOf(fFrameWin) < 0) {
            return;
        }
        margin = fSettings->marginSize;
    }
    fMarginWidget->setContentsMargins(margin, 0, margin, fBottomMarginSize);
    fUpdateMarginColor(color);
}


#ifdef Q_OS_MAC
#include <QFileOpenEvent>
bool
HApplication::event( QEvent* e )
{
    // We only handle the FileOpen event and only when no game
    // is currently running.
    if (e->type() != QEvent::FileOpen or this->fGameRunning) {
        return QApplication::event(e);
    }
    QFileOpenEvent* fOpenEv = static_cast<QFileOpenEvent*>(e);
    if (fOpenEv->file().isEmpty()) {
        return QApplication::event(e);
    }
    this->fNextGame = fOpenEv->file();
    e->accept();
    return true;
}
#endif


void
HApplication::entryPoint( QString gameFileName )
{
    // Process pending events in case we have a FileOpen event. Freeze user
    // input while doing so; we don't want to leave a way to mess with the
    // GUI when we don't have a game running yet.  We do this a bunch of
    // times to make sure the FileOpen event can propagate properly.
    for (int i = 0; i < 100; ++i) {
        this->advanceEventLoop();
    }

    if (this->fNextGame.isEmpty()) {
        this->fNextGame = gameFileName;
    }

    // If we still don't have a filename, prompt for one.
    if (this->fNextGame.isEmpty() and this->fSettings->askForGameFile) {
        this->fNextGame = QFileDialog::getOpenFileName(0, QObject::tr("Choose the story file you wish to play"),
                                                       this->fSettings->lastFileOpenDir,
                                                       QObject::tr("Hugo Games")
                                                       + QString::fromLatin1("(*.hex *.Hex *.HEX)"));
    }

    // Switch to fullscreen, if needed.
    if (fSettings->isFullscreen) {
        this->fMainWin->toggleFullscreen();
        // If we don't let the event loop run for a while, for some reason
        // the screen will be flashing when the window first becomes visible.
        // The reason is unknown, but this seems to work around the issue.
        for (int i = 0; i < 5000; ++i) {
            this->advanceEventLoop();
        }
    }

    // Automatically quit the application when the last window has closed.
    connect(this, SIGNAL(lastWindowClosed()), this, SLOT(quit()));

    // If we have a filename, load it.
    if (not this->fNextGame.isEmpty()) {
        if (this->fSettings->isMaximized) {
            this->fMainWin->showMaximized();
        } else {
            this->fMainWin->show();
        }
        this->fRunGame();
    } else {
        // File dialog was canceled.
        quit();
    }
}


void HApplication::handleEngineFinished()
{
    this->fGameRunning = false;
    this->fGameFile.clear();
    emit gameHasQuit();
    fMainWin->close();
}


void
HApplication::notifyPreferencesChange( const Settings* sett )
{
    smartformatting = sett->smartFormatting;

    // 'bgcolor' is a Hugo engine global.
    this->updateMargins(::bgcolor);

    // Recalculate font dimensions, in case font settings have changed.
    hHandlers->calcFontDimensions();

    // The fonts might have changed.
    hFrame->setFontType(currentfont);
    hMainWin->setScrollbackFont(sett->scrollbackFont);

    display_needs_repaint = true;
#ifndef DISABLE_AUDIO
    if (not sett->enableMusic) {
        hHandlers->stopmusic();
    }
    if (not sett->enableSoundEffects) {
        hHandlers->stopsample();
    }
    updateMusicVolume();
    updateSoundVolume();
    updateVideoVolume();
#endif
#ifndef DISABLE_VIDEO
    if (not sett->enableVideo) {
        hHandlers->stopvideo();
    }
#endif
    fFrameWin->updateGameScreen(true);
}


void
HApplication::advanceEventLoop()
{
    // Guard against re-entrancy.
    static volatile bool working = false;
    if (working) {
        return;
    }
    working = true;
    this->processEvents(QEventLoop::ExcludeUserInputEvents);
    working = false;
}


void
HApplication::terminateEngineThread()
{
    fHugoThread->terminate();
    fHugoThread->wait(2000);
}
