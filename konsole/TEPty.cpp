/* -------------------------------------------------------------------------- */
/*                                                                            */
/* [TEPty.C]               Pseudo Terminal Device                             */
/*                                                                            */
/* -------------------------------------------------------------------------- */
/*                                                                            */
/* Copyright (c) 1997,1998 by Lars Doelle <lars.doelle@on-line.de>            */
/*                                                                            */
/* This file is part of Konsole - an X terminal for KDE                       */
/* -------------------------------------------------------------------------- */

/* If you're compiling konsole on non-Linux platforms and find
   problems that you can track down to this file, please have
   a look into ../README.ports, too.
*/

/*! \file
*/


/*! \class TEPty

    \brief Ptys provide a pseudo terminal connection to a program.

    Although closely related to pipes, these pseudo terminal connections have
    some ability, that makes it nessesary to uses them. Most importent, they
    know about changing screen sizes and UNIX job control.

    Within the terminal emulation framework, this class represents the
    host side of the terminal together with the connecting serial line.

    One can create many instances of this class within a program.
    As a side effect of using this class, a signal(2) handler is
    installed on SIGCHLD.

    \par FIXME

    [NOTE: much of the technical stuff below will be replaced by forkpty.]

    publish the SIGCHLD signal if not related to an instance.

    clearify TEPty::done vs. TEPty::~TEPty semantics.
    check if pty is restartable via run after done.

    \par Pseudo terminals

    Pseudo terminals are a unique feature of UNIX, and always come in form of
    pairs of devices (/dev/ptyXX and /dev/ttyXX), which are connected to each
    other by the operating system. One may think of them as two serial devices
    linked by a null-modem cable. Being based on devices the number of
    simultanous instances of this class is (globally) limited by the number of
    those device pairs, which is 256.

    Another technic are UNIX 98 PTY's. These are supported also, and prefered
    over the (obsolete) predecessor.

    There's a sinister ioctl(2), signal(2) and job control stuff
    nessesary to make everything work as it should.

    Much of the stuff can be simplified by using openpty from glibc2.
    Compatibility issues with obsolete installations and other unixes
    my prevent this.
*/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef __sgi
#define __svr4__
#endif

#if defined(HAVE_GRANTPT) && defined(HAVE_PTSNAME) && defined(HAVE_UNLOCKPT) && !defined(_XOPEN_SOURCE) && !defined(__svr4__)
#define _XOPEN_SOURCE // make stdlib.h offer the above fcts
#endif

/* for NSIG */
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif

#ifdef __osf__
#define _OSF_SOURCE
#include <float.h>
#endif

#ifdef _AIX
#define _ALL_SOURCE
#include <sys/types.h>
#endif

#include <stdlib.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#ifdef TIME_WITH_SYS_TIME
  #include <sys/time.h>
#endif
#include <sys/resource.h>
#ifdef HAVE_SYS_STROPTS_H
#include <sys/stropts.h>
#define _NEW_TTY_CTRL
#endif
#include <sys/wait.h>

#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <grp.h>

#ifdef HAVE_LIBUTIL_H
        #include <libutil.h>
        #define USE_LOGIN
#elif defined(HAVE_UTIL_H)
        #include <util.h>
        #define USE_LOGIN
#endif

#ifdef USE_LOGIN
        #include <errno.h>
        #include <utmp.h>
#endif

#include <signal.h>

#ifdef HAVE_TERMIOS_H
/* for HP-UX (some versions) the extern C is needed, and for other
   platforms it doesn't hurt */
extern "C" {
#include <termios.h>
}
#endif
#if !defined(__osf__)
#ifdef HAVE_TERMIO_H
/* needed at least on AIX */
#include <termio.h>
#endif
#endif
//#include <time.h>
#include <unistd.h>

#if defined (_HPUX_SOURCE)
#define _TERMIOS_INCLUDED
#include <bsdtty.h>
#endif

#if defined(HAVE_PTY_H)
#include <pty.h>
#endif

#include <qintdict.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "TEPty.h"
#include "TEPty.moc"

#define TTY_GROUP "tty"

#include <kstandarddirs.h>
#include <klocale.h>
#include <kdebug.h>

#ifndef HERE
#define HERE fprintf(stdout,"%s(%d): here\n",__FILE__,__LINE__)
#endif

// Small hack to keep it still compiling with KDE 3.0
#include "kdeversion.h"
#if KDE_VERSION <305
  #undef HAVE_OPENPTY
#endif

template class QIntDict<TEPty>;

class KUtmpProcess : public KProcess
{
public:
   int commSetupDoneC()
   {
     dup2(cmdFd, 0);
     dup2(cmdFd, 1);
     dup2(cmdFd, 3);
     return 1;
   }
   int cmdFd;
};

FILE* syslog_file = NULL; //stdout;

#define PTY_FILENO 3
#define BASE_CHOWN "konsole_grantpty"

int chownpty(int fd, bool grant)
// param fd: the fd of a master pty.
// param grant: true to grant, false to revoke
// returns 1 on success 0 on fail
{
  struct sigaction newsa, oldsa;
  newsa.sa_handler = SIG_DFL;
  newsa.sa_mask = sigset_t();
  newsa.sa_flags = 0;
  sigaction(SIGCHLD, &newsa, &oldsa);

  pid_t pid = fork();
  if (pid < 0)
  {
    // restore previous SIGCHLD handler
    sigaction(SIGCHLD, &oldsa, NULL);

    return 0;
  }
  if (pid == 0)
  {
    /* We pass the master pseudo terminal as file descriptor PTY_FILENO. */
    if (fd != PTY_FILENO && dup2(fd, PTY_FILENO) < 0) exit(1);
    QString path = locate("exe", BASE_CHOWN);
    execle(path.ascii(), BASE_CHOWN, grant?"--grant":"--revoke", NULL, NULL);
    exit(1); // should not be reached
  }

  if (pid > 0) {
    int w;

retry:
    int rc = waitpid(pid, &w, 0);
    if ((rc == -1) && (errno == EINTR))
      goto retry;

    // restore previous SIGCHLD handler
    sigaction(SIGCHLD, &oldsa, NULL);

    return (rc != -1 && WIFEXITED(w) && WEXITSTATUS(w) == 0);
  }

  return 0; //dummy.
}

/* -------------------------------------------------------------------------- */

/*!
   Informs the client program about the
   actual size of the window.
*/

void TEPty::setSize(int lines, int columns)
{
  //kdDebug(1211)<<"TEPty::setSize()"<<endl;
  m_WSize.ws_row = (unsigned short)lines;
  m_WSize.ws_col = (unsigned short)columns;
  if(m_MasterFd < 0) return;
  ioctl(m_MasterFd, TIOCSWINSZ,(char *)&m_WSize);
  //kdDebug(1211)<<"TEPty::setSize() done"<<endl;
}

void TEPty::commClose()
{
#ifdef HAVE_UTEMPTER
  {
     KUtmpProcess utmp;
     utmp.cmdFd = m_MasterFd;
     utmp << "/usr/sbin/utempter" << "-d" << ttynam;
     utmp.start(KProcess::Block);
  }
#elif defined(USE_LOGIN)
  char *tty_name=ttyname(0);
  if (tty_name)
  {
  	if (strncmp(tty_name, "/dev/", 5) == 0)
	    tty_name += 5;
        logout(tty_name);
  }
#endif
  if (m_bNeedGrantPty) chownpty(m_MasterFd, false);
 
  KProcess::commClose();
}

void TEPty::donePty()
{
  int status = exitStatus();
  emit done(status);
}


const char* TEPty::deviceName()
{
  return ttynam;
}

/*!
    start the client program.
*/
int TEPty::run(const char* _pgm, QStrList & _args, const char* _term, bool _addutmp,
               const char* _konsole_dcop, const char* _konsole_dcop_session)
{
  clearArguments();

  QStrListIterator it( _args );
  for (; it.current(); ++it )
    arguments << it.current();
  arguments.prepend(_pgm);
//  kdDebug() << "pgm = " << _pgm << endl;
  term = _term;
  konsole_dcop = _konsole_dcop;
  konsole_dcop_session = _konsole_dcop_session;
  m_bAddUtmp = _addutmp;
  if (m_MasterFd < 0)
     return -1;

  if (!start(NotifyOnExit, (Communication) (Stdout | NoRead)))
     return -1;

  if (m_SlaveFd >= 0)
  {
     close(m_SlaveFd);
     m_SlaveFd = -1;
  }

  resume(); // Start...
  return 0;

}

void TEPty::setWriteable(bool writeable)
{
  struct stat sbuf;
  stat(deviceName(), &sbuf);
  if (writeable)
    chmod(deviceName(), sbuf.st_mode | S_IWGRP);
  else
    chmod(deviceName(), sbuf.st_mode & ~(S_IWGRP|S_IWOTH));
}

void TEPty::openPty()
{ 
  m_bNeedGrantPty = true;

#include <kdeversion.h>
#if KDE_VERSION <305
  #undef HAVE_OPENPTY  // Hack to make it compile for binner
#endif

  // Find a master pty that we can open ////////////////////////////////

  // Because not all the pty animals are created equal, they want to
  // be opened by several different methods.

  // We try, as we know them, one by one.
#if defined(HAVE_OPENPTY) //FIXME: some work needed.
  if (m_MasterFd < 0) {
    int master_fd, slave_fd;
    if (openpty(&master_fd, &slave_fd, NULL, NULL, NULL) == 0) {
      m_MasterFd = master_fd;
      m_SlaveFd = slave_fd;
#ifdef HAVE_PTSNAME
      strncpy(ptynam, ptsname(master_fd), 50);
#else
      // Just a guess, maybe ttyname with return nothing.
      strncpy(ptynam, ttyname(master_fd),50);
#endif
      strncpy(ttynam, ttyname(slave_fd), 50);

      m_bNeedGrantPty = false;
       
      /* Get the group ID of the special `tty' group.  */
      struct group* p = getgrnam(TTY_GROUP);    /* posix */
      gid_t gid = p ? p->gr_gid : getgid ();    /* posix */

      if (fchown(slave_fd, (uid_t) -1, gid) < 0)
      {
         m_bNeedGrantPty = true;
         fprintf(stderr,"konsole: cannot chown %s.\n",ttynam); 
         perror("Reason");
      }
      else if (chmod(ttynam, S_IRUSR|S_IWUSR|S_IWGRP) < 0)
      {
         m_bNeedGrantPty = true;
         fprintf(stderr,"konsole: cannot chmod %s.\n",ttynam); 
         perror("Reason");
      }
    }
  }
#endif

//#if defined(__sgi__) || defined(__osf__) || defined(__svr4__)
#if defined(HAVE_GRANTPT) && defined(HAVE_PTSNAME)
  if (m_MasterFd < 0)
  {
#ifdef _AIX
    m_MasterFd = open("/dev/ptc",O_RDWR);
#else
    m_MasterFd = open("/dev/ptmx",O_RDWR);
#endif
    if (m_MasterFd >= 0)
    {
      char *ptsn = ptsname(m_MasterFd);
      if (ptsn) {
          strncpy(ttynam, ptsname(m_MasterFd), 50);
          grantpt(m_MasterFd);
          m_bNeedGrantPty = false;
      } else {
      	  perror("ptsname");
	  close(m_MasterFd);
	  m_MasterFd = -1;
      }
    }
  }
#endif

#if defined(_SCO_DS) || defined(__USLC__) // SCO OSr5 and UnixWare, might be obsolete
  if (m_MasterFd < 0)
  { for (int idx = 0; idx < 256; idx++)
    { sprintf(ptynam, "/dev/ptyp%d", idx);
      sprintf(ttynam, "/dev/ttyp%d", idx);
      if (access(ttynam, F_OK) < 0) { idx = 256; break; }
      if ((m_MasterFd = open (ptynam, O_RDWR)) >= 0)
      { if (access (ttynam, R_OK|W_OK) == 0) break;
        close(m_MasterFd); m_MasterFd = -1;
      }
    }
  }
#endif

  if (m_MasterFd < 0) // Linux device names, FIXME: Trouble on other systems?
  { for (const char* s3 = "pqrstuvwxyzabcdefghijklmno"; *s3 != 0; s3++)
    { for (const char* s4 = "0123456789abcdefghijklmnopqrstuvwxyz"; *s4 != 0; s4++)
      { sprintf(ptynam,"/dev/pty%c%c",*s3,*s4);
        sprintf(ttynam,"/dev/tty%c%c",*s3,*s4);
        if ((m_MasterFd = open(ptynam,O_RDWR)) >= 0)
        { if (geteuid() == 0 || access(ttynam,R_OK|W_OK) == 0) break;
          close(m_MasterFd); m_MasterFd = -1;
        }
      }
      if (m_MasterFd >= 0) break;
    }
  }

  if (m_MasterFd < 0)
  {
    fprintf(stderr,"Can't open a pseudo teletype\n"); 
    m_strError = i18n("Unable to open a suitable terminal device.");
    return -1;
  }

  if (m_bNeedGrantPty && !chownpty(m_MasterFd, true))
  {
    fprintf(stderr,"konsole: chownpty failed for device %s::%s.\n",ptynam,ttynam);
    fprintf(stderr,"       : This means the session can be eavesdroped.\n");
    fprintf(stderr,"       : Make sure konsole_grantpty is installed in\n");
    fprintf(stderr,"       : %s and setuid root.\n",
            KGlobal::dirs()->findResourceDir("exe",
                                             "konsole").local8Bit().data());
  }

  fcntl(m_MasterFd,F_SETFL,O_NDELAY);
}

int TEPty::makePty(bool _addutmp)
{
  if (m_MasterFd < 0) // no master pty could be opened
  {
  //FIXME:
    fprintf(stderr,"opening master pty failed.\n");
    exit(1);
  }

#ifdef HAVE_UNLOCKPT
  unlockpt(m_MasterFd);
#endif

#if defined(TIOCSPTLCK) && 0 //FIXME: obsolete, to removed if no one complains
  int flag = 0; ioctl(m_MasterFd,TIOCSPTLCK,&flag); // unlock pty
#endif

  // open and set all standard files to slave tty
  int tt = m_SlaveFd; // Already opened?

  if (tt < 0)
    tt = open(ttynam, O_RDWR); 

  if (tt < 0) // the slave pty could be opened
  {
  //FIXME:
  //fprintf(stderr,"opening slave pty (%s) failed.\n",ttynam);
  //exit(1);
  }

#if (defined(__svr4__) || defined(__sgi__))
  // Solaris
  ioctl(tt, I_PUSH, "ptem");
  ioctl(tt, I_PUSH, "ldterm");
#endif

  // Stamp utmp/wtmp if we have and want them
#ifdef HAVE_UTEMPTER
  if (_addutmp)
  {
     KUtmpProcess utmp;
     utmp.cmdFd = m_MasterFd;
     utmp << "/usr/sbin/utempter" << "-a" << ttynam << "";
     utmp.start(KProcess::Block);
  }
#endif
#ifdef USE_LOGIN
  char *str_ptr;
  struct utmp l_struct;
  memset(&l_struct, 0, sizeof(struct utmp));

  if (! (str_ptr=getlogin()) ) {
    if ( ! (str_ptr=getenv("LOGNAME"))) {
      abort();
    }
  }
  strncpy(l_struct.ut_name, str_ptr, UT_NAMESIZE);

  if (gethostname(l_struct.ut_host, UT_HOSTSIZE) == -1) {
     if (errno != ENOMEM)
        abort();
     l_struct.ut_host[UT_HOSTSIZE]=0;
  }

  if (! (str_ptr=ttyname(tt)) ) {
    abort();
  }
  if (strncmp(str_ptr, "/dev/", 5) == 0)
       str_ptr += 5;
  strncpy(l_struct.ut_line, str_ptr, UT_LINESIZE);
  time(&l_struct.ut_time); 

  login(&l_struct);
#endif
  return tt;
}

//! only used internally. See `run' for interface
void TEPty::startPgm(const char* pgm, QValueList<QCString> & args, const char* term)
{ 
  int sig;
  int tt = makePty(m_bAddUtmp);

  //reset signal handlers for child process
  for (sig = 1; sig < NSIG; sig++)
      signal(sig,SIG_DFL);

  // Don't know why, but his is vital for SIGHUP to find the child.
  // Could be, we get rid of the controling terminal by this.
  // getrlimit is a getdtablesize() equivalent, more portable (David Faure)
  struct rlimit rlp;
  getrlimit(RLIMIT_NOFILE, &rlp);
  // We need to close all remaining fd's.
  // Especially the one used by KProcess::start to see if we are running ok.
  for (int i = 0; i < (int)rlp.rlim_cur; i++)
    if (i != tt && i != m_MasterFd) close(i); //FIXME: (result of merge) Check if not closing fd is OK)

  dup2(tt,fileno(stdin));
  dup2(tt,fileno(stdout));
  dup2(tt,fileno(stderr));

  if (tt > 2) close(tt);

  // Setup job control //////////////////////////////////

  // This is pretty obscure stuff which makes the session
  // to be the controlling terminal of a process group.

  if (setsid() < 0) perror("failed to set process group"); // (vital for bash)

#if defined(TIOCSCTTY)
  ioctl(0, TIOCSCTTY, 0);
#endif

  int pgrp = getpid();                 // This sequence is necessary for
#ifdef _AIX
  tcsetpgrp (0, pgrp);
#else
  ioctl(0, TIOCSPGRP, (char *)&pgrp);  // event propagation. Omitting this
#endif
  setpgid(0,0);                        // is not noticeable with all
  close(open(ttynam, O_WRONLY, 0));       // clients (bash,vi). Because bash
  setpgid(0,0);                        // heals this, use '-e' to test it.

  /* without the '::' some version of HP-UX thinks, this declares
     the struct in this class, in this method, and fails to find the correct
     t[gc]etattr */
  static struct ::termios ttmode;
#undef CTRL
#define CTRL(c) ((c) - '@')

// #ifdef __svr4__
// #define CINTR 0177
// #define CQUIT CTRL('U')
// #define CERASE CTRL('H')
// // #else
// #define CINTR CTRL('C')
// #define CQUIT CTRL('\\')
// #define CERASE 0177
// #endif

#if defined (__FreeBSD__) || defined (__NetBSD__) || defined (__OpenBSD__) || defined (__bsdi__) || defined(__APPLE__)
      ioctl(0,TIOCGETA,(char *)&ttmode);
#else
#   if defined (_HPUX_SOURCE) || defined(__Lynx__)
      tcgetattr(0, &ttmode);
#   else
      ioctl(0,TCGETS,(char *)&ttmode);
#   endif
#endif
      if (!m_bXonXoff)
         ttmode.c_iflag &= ~(IXOFF | IXON);
      ttmode.c_cc[VINTR] = CTRL('C');
      ttmode.c_cc[VQUIT] = CTRL('\\');
      ttmode.c_cc[VERASE] = 0177;
#if defined (__FreeBSD__) || defined (__NetBSD__) || defined (__OpenBSD__) || defined (__bsdi__) || defined(__APPLE__)
      ioctl(0,TIOCSETA,(char *)&ttmode);
#else
#   ifdef _HPUX_SOURCE
      tcsetattr(0, TCSANOW, &ttmode);
#   else
      ioctl(0,TCSETS,(char *)&ttmode);
#   endif
#endif

  close(m_MasterFd);

  // drop privileges
  setgid(getgid()); setuid(getuid()); 

  // propagate emulation
  if (term && term[0]) setenv("TERM",term,1);
  if (konsole_dcop && konsole_dcop[0]) setenv("KONSOLE_DCOP",konsole_dcop,1);
  if (konsole_dcop_session && konsole_dcop_session[0]) setenv("KONSOLE_DCOP_SESSION",konsole_dcop_session,1);

  // convert QStrList into char*[]
  unsigned int i;
  char **argv = (char**)malloc(sizeof(char*)*(args.count()+1));
  for (i = 0; i<args.count(); i++) {
     argv[i] = strdup(args[i]);
     }

  argv[i] = 0L;

  ioctl(0,TIOCSWINSZ,(char *)&m_WSize);  // set screen size

  // finally, pass to the new program
  //  kdDebug(1211) << "We are ready to run the program " << pgm << endl;
  execvp(pgm, argv);
  perror("exec failed");
  exit(1);                             // control should never come here.
}

/*!
    Create an instance.
*/
TEPty::TEPty() : pSendJobTimer(NULL)
{ 
  m_bXonXoff = false;
  memset(&m_WSize, 0, sizeof(struct winsize));
  m_SlaveFd = -1;
  m_MasterFd = -1;
  openPty();
  connect(this, SIGNAL(receivedStdout(int, int &)), 
	  this, SLOT(DataReceived(int, int&)));
  connect(this, SIGNAL(processExited(KProcess *)),
          this, SLOT(donePty()));
}

/*!
    Destructor.
    Note that the related client program is not killed
    (yet) when a instance is deleted.
*/
TEPty::~TEPty()
{
}

int TEPty::setupCommunication(Communication comm)
{
   if (m_MasterFd <= 0) return 0;
   out[0] = m_MasterFd;
   out[1] = dup(2); // Dummy
   communication = comm;
   return 1;
}

int TEPty::commSetupDoneC()
{
   QCString pgm = arguments.first();
   arguments.pop_front();
   startPgm(pgm,arguments,term);
   return 0; // Never reached.
}

/*! sends a character through the line */
void TEPty::send_byte(char c)
{
  send_bytes(&c,1);
}

/*! sends a 0 terminated string through the line */
void TEPty::send_string(const char* s)
{
  send_bytes(s,strlen(s));
}

void TEPty::doSendJobs() {
  int written;
  while(!pendingSendJobs.isEmpty()) {
    SendJob& job = pendingSendJobs.first();
    written = write(m_MasterFd, job.buffer.data() + job.start, job.length);
    if (written==-1) {
      if ( errno!=EAGAIN && errno!=EINTR )
        pendingSendJobs.remove(pendingSendJobs.begin());
      return;
    }else {
      job.start += written;
      job.length -= written;
      if ( job.length == 0 )
        pendingSendJobs.remove(pendingSendJobs.begin());
    }
  }
  if (pSendJobTimer)
    pSendJobTimer->stop();
}

void TEPty::appendSendJob(const char* s, int len)
{
  pendingSendJobs.append(SendJob(s,len));
  if (!pSendJobTimer) {
    pSendJobTimer = new QTimer(this);
    connect(pSendJobTimer, SIGNAL(timeout()), this, SLOT(doSendJobs()) );
  }
  pSendJobTimer->start(0);
}  

/*! sends len bytes through the line */
void TEPty::send_bytes(const char* s, int len)
{
  if (m_MasterFd < 0)
    return;
  if (!pendingSendJobs.isEmpty()) {
    appendSendJob(s,len);
  }else {
    int written;
    do {
      written = write(m_MasterFd,s,len);
      if (written==-1) {
        if ( errno==EAGAIN || errno==EINTR )
          appendSendJob(s,len);
        return;
      }
      s += written;
      len -= written;
    } while(len>0);
  }
}

/*! indicates that a block of data is received */
void TEPty::DataReceived(int,int &len)
{ 
  char buf[4096];
  len = read(m_MasterFd, buf, 4096);
  if (len < 0)
     return;

  emit block_in(buf,len);
  if (syslog_file) // if (debugging) ...
  {
    int i;
    for (i = 0; i < len; i++) fputc(buf[i],syslog_file);
    fflush(syslog_file);
  }
}

void TEPty::lockPty(bool lock)
{
  if (lock)
    suspend();
  else
    resume();
}

