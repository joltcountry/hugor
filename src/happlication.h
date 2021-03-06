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
#ifndef HAPPLICATION_H
#define HAPPLICATION_H

#include <QApplication>


extern class HApplication*  hApp;


class HApplication: public QApplication {
    Q_OBJECT

  private:
    // Preferences (fonts, colors, etc.)
    class Settings* fSettings;

    // Main application window.
    class HMainWindow* fMainWin;

    // Frame widget containing all subwindows.
    class HFrame* fFrameWin;

    // Parent of fFrameWin, provides margins.
    class HMarginWidget* fMarginWidget;

    const int fBottomMarginSize;

    // Are we currently executing a game?
    bool fGameRunning;

    // Filename of the game we're currently executing.
    QString fGameFile;

    // The game we should try to run after the current one ends.
    QString fNextGame;

    // Text codec used by the Hugo engine.
    QTextCodec* fHugoCodec;

    // Are we running in Gnome?
    bool fDesktopIsGnome;

    // Hugo engine runner and thread.
    class EngineRunner* fEngineRunner;
    class EngineThread* fHugoThread;

    // Run the game file contained in fNextGame.
    void
    fRunGame();

    void
    fUpdateMarginColor( int color );

#ifdef Q_OS_MAC
  protected:
    // On the Mac, dropping a file on our application icon will generate a
    // FileOpen event, so we override this to be able to handle it.
    virtual bool
    event( QEvent* );
#endif

  signals:
    // Emitted prior to quitting a game.  The game has not quit yet when this
    // is emitted.
    void gameQuitting();

    // Emitted after quiting a game.  The game has already quit when this is
    // emitted.
    void gameHasQuit();

  public slots:
    // Replacement for main().  We need this so that we can start the
    // Hugo engine after the QApplication main event loop has started.
    void
    entryPoint( QString gameFileName );

    void
    handleEngineFinished();

  public:
    HApplication( int& argc, char* argv[], const char* appName, const char* appVersion,
                  const char* orgName, const char* orgDomain );

    virtual
    ~HApplication();

    /* Passing a negative value as 'color' will keep the current margin color.
     */
    void
    updateMargins( int color );

    class Settings*
    settings()
    { return this->fSettings; }

    class HFrame*
    frameWindow()
    { return this->fFrameWin; }

    class HMarginWidget*
    marginWidget()
    { return this->fMarginWidget; }

    bool
    gameRunning()
    { return this->fGameRunning; }

    const QString&
    gameFile()
    { return this->fGameFile; }

    void
    setGameRunning( bool f )
    {
        this->fGameRunning = f;
        if (f == false) {
            emit gameQuitting();
        }
    }

    // Notify the application that preferences have changed.
    void
    notifyPreferencesChange( const class Settings* sett );

    // Advance the event loop.
    void
    advanceEventLoop();

    // Text codec used by Hugo.
    QTextCodec*
    hugoCodec()
    { return this->fHugoCodec; }

    bool
    desktopIsGnome()
    { return this->fDesktopIsGnome; }

    void
    terminateEngineThread();
};


#endif
