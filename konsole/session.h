/* -------------------------------------------------------------------------- */
/*                                                                            */
/* [session.h]              Testbed for TE framework                          */
/*                                                                            */
/* -------------------------------------------------------------------------- */
/*                                                                            */
/* Copyright (c) 1997,1998 by Lars Doelle <lars.doelle@on-line.de>            */
/*                                                                            */
/* This file is part of Konsole, an X terminal.                               */
/*                                                                            */
/* -------------------------------------------------------------------------- */

#ifndef SESSION_H
#define SESSION_H

#include <kapplication.h>
#include <kmainwindow.h>
#include <qstrlist.h>

#include "TEPty.h"
#include "TEWidget.h"
#include "TEmuVt102.h"

class TESession : public QObject
{ Q_OBJECT

public:

  TESession(KMainWindow* main, TEWidget* w,
            const QString &pgm, QStrList & _args,
	    const QString &term);
  ~TESession();

public:

  void        setConnect(bool r);  // calls setListenToKeyPress(r)
  void        setListenToKeyPress(bool l);
  TEmulation* getEmulation();      // to control emulation
  bool        isSecure();
  bool        isMonitorActivity();
  bool        isMonitorSilence();
  bool        isMasterMode();
  int schemaNo();
  int fontNo();
  const QString& Term();
  const QString& Title();
  const QString& IconName();
  const QString& IconText();
  QString fullTitle() const;
  int keymapNo();
  QString keymap();
  QStrList getArgs();
  QString getPgm();

  void setHistory(const HistoryType&);
  const HistoryType& history();

  void setMonitorActivity(bool);
  void setMonitorSilence(bool);
  void setMasterMode(bool);
  void setSchemaNo(int sn);
  void setKeymapNo(int kn);
  void setKeymap(const QString& _id);
  void setFontNo(int fn);
  void setTitle(const QString& _title);
  void setIconName(const QString& _iconName);
  void setIconText(const QString& _iconText);
  void kill(int signal);

public slots:

  void run();
  void done(int status);
  void terminate();

signals:

  void done(TESession*, int);
  void updateTitle();
  void notifySessionState(TESession* session, int state);

private slots:
  void setUserTitle( int, const QString &caption );
  void monitorTimerDone();
  void notifySessionState(int state);

private:

  TEPty*         sh;
  TEWidget*      te;
  TEmulation*    em;

  bool           monitorActivity;
  bool           monitorSilence;
  bool           masterMode;
  QTimer*        monitorTimer;

  //FIXME: using the indices here
  // is propably very bad. We should
  // use a persistent reference instead.
  int            schema_no;
  int            font_no;
  QString        title;
  QString        userTitle;
  QString        iconName;
  QString        iconText; // as set by: echo -en '\033]1;IconText\007

  QString        pgm;
  QStrList       args;

  QString        term;
};

#endif
