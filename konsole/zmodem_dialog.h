/*  This file is part of the KDE libraries
 *  Copyright (C) 2002 Waldo Bastian <bastian@kde.org>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License version 2 as published by the Free Software Foundation;
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA 02111-1307, USA.
 **/

#ifndef __zmodem_dialog_h__
#define __zmodem_dialog_h__ 

#include <kdialogbase.h>

class QTextEdit;

class ZModemDialog : public KDialogBase
{
  Q_OBJECT
public:
  ZModemDialog(QWidget *parent, bool modal, const QString &caption);
  
  /**
   * Adds a line of text to the progress window
   */
  void addProgressText(const QString &);

  /**
   * To indicate the process is finished.
   */
  void done();

public slots:
  void slotClose();
  
private:
  QTextEdit *textEdit;
};

#endif
