/* $Id: cleanup.c,v 2.18.2.3 1997-10-17 07:06:35 ghudson Exp $
 *
 * Cleanup program for stray processes
 *
 * by Mark Rosenstein <mar@mit.edu>, based on a program by 
 * John Carr <jfc@athena.mit.edu>
 *
 * 1. Create /etc/nologin, then snapshot (1) who is in the password file,
 *    (2) who is logged in, and (3) what process are running, then 
 *    re-enable logins.
 *
 * 2. Kill process owned by users who are not logged in, or are
 *    not in the password file (depending on command line options)
 *
 * 3. If the options indicate it, rebuild the password file from 
 *    logged in user list.
 */

#ifdef SOLARIS
#define volatile
#endif
#include <stdio.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/time.h>
#ifndef sgi
#include <sys/proc.h>
#endif
#include <signal.h>
#include <string.h>
#ifdef SYSV
#include <utmpx.h>
#define CLEANUP_UTMP_FILE "/etc/utmpx"
#else
#include <utmp.h>
#define CLEANUP_UTMP_FILE "/etc/utmp"
#endif
#include <pwd.h>
#ifdef _IBMR2
#include <usersec.h>
#endif
#ifdef _BSD
#undef _BSD
#endif
#include <nlist.h>
#ifdef SOLARIS
#include <kvm.h>
#endif
#include "cleanup.h"

char *version = "$Id: cleanup.c,v 2.18.2.3 1997-10-17 07:06:35 ghudson Exp $";




#ifdef _AIX
extern char     *sys_errlist[];
extern int      sys_nerr;
#endif

#ifdef ultrix
extern char *sys_errlist[];
#endif

#ifdef sun
extern char *sys_errlist[];
#endif

struct nlist nl[] =
{
#ifdef _AIX
#define PROC 0
  { "proc" },
#define MAX_PROC 1
  { "max_proc" },
  { ""}
#else
#ifdef SOLARIS
#define PROC 0
  { "nproc" },
#define MAX_PROC 1
  { "max_nproc" },
  { ""}
#else
#define PROC 0
  { "_proc" },
#define NPROC 1
  { "_nproc" },
  { ""}
#endif
#endif
};

static char *nologin_msg =
  "This machine is down for cleanup; try again in a few seconds.\n";
static char *nologin_fn  = "/etc/nologin";


int main(argc,argv)
int argc;
char *argv[];
{
    int fd, mode = LOGGED_IN, i, j, found, fail = 0;
    int *users, *pword, *get_password_entries(), *get_logged_in();
    struct cl_proc *procs, *get_processes();

    if (argc == 1)
      mode = LOGGED_IN;
    else if (argc == 2 && !strcmp(argv[1], "-loggedin"))
      mode = LOGGED_IN;
    else if (argc == 2 && !strcmp(argv[1], "-passwd"))
      mode = PASSWD;
    else if (argc == 2 && !strcmp(argv[1], "-debug"))
      mode = MDEBUG;
    else {
	fprintf(stderr, "usage: %s [-loggedin | -passwd | -debug]\n", argv[0]);
	exit(1);
    }

    /* First disable logins */
    fd = open(nologin_fn ,O_RDWR | O_EXCL | O_CREAT, 0644);
    if (fd < 0) {
	if (errno == EEXIST) {
	    fprintf(stderr, "%s: %s already exists, not performing cleanup.\n",
		    argv[0], nologin_fn);
	    exit(2);
	} else {
	    fprintf(stderr, "%s: Can't create %s, %s.\n", argv[0], nologin_fn,
		    sys_errlist[errno]);
	    exit(3);
	}
    }
    (void)write(fd, nologin_msg, strlen(nologin_msg));
    (void)close(fd);

    /* wait a moment so that any login in progress when we disabled
     * logins is likely to complete
     */
    sleep(2);

    /* snapshot info */

#ifdef SYSV
    if (lckpwdf() != 0) {
	unlink(nologin_fn);
	exit(1);
    }
#endif
    lock_file("/etc/ptmp");
    lock_file("/etc/gtmp");
    pword = get_password_entries();
    users = get_logged_in();
    /* rewrite password file if necessary */
    if (users && mode == LOGGED_IN) {
	fail = rewrite_passwd(users);
    }
    unlock_file("/etc/ptmp");
    unlock_file("/etc/gtmp");
#ifdef SYSV
    ulckpwdf();
#endif
    procs = get_processes();
    if (fail || pword == NULL || users == NULL || procs == NULL) {
	unlink(nologin_fn);
	exit(1);
    }

    if (unlink(nologin_fn) < 0)
      fprintf(stderr, "%s: Warning: unable to unlink %s.\n",
	      argv[0], nologin_fn);

    /* First round of kill signals: HUP */
    found = 0;
    for (i = 0; procs[i].pid != -1; i++) {
	switch (mode) {
	case LOGGED_IN:
	    for (j = 0; users[j] != -1; j++)
	      if (users[j] == procs[i].uid)
		break;
	    if (users[j] == -1) {
#ifdef DEBUG
		fprintf(stderr,"kill(%d, SIGHUP)\n", procs[i].pid);
#endif
		if (kill(procs[i].pid, SIGHUP) == 0)
		  found = 1;
	    }
	    break;
	case PASSWD:
	    for (j = 0; pword[j] != -1; j++)
	      if (pword[j] == procs[i].uid)
		break;
	    if (pword[j] == -1) {
#ifdef DEBUG
		fprintf(stderr,"kill(%d, SIGHUP)\n", procs[i].pid);
#endif
		if (kill(procs[i].pid, SIGHUP) == 0)
		  found = 1;
	    }
	    break;
	default:
	    ;
	}
    }

    /* only do second round if found any */
    if (found) {
	sleep(5);
#ifdef DEBUG
	fprintf(stderr,"Starting second pass\n");
#endif

	for (i = 0; procs[i].pid != -1; i++) {
	    switch (mode) {
	    case LOGGED_IN:
		for (j = 0; users[j] != -1; j++)
		  if (users[j] == procs[i].uid)
		    break;
		if (users[j] == -1) {
#ifdef DEBUG
		    fprintf(stderr,"kill(%d, SIGKILL)\n", procs[i].pid);
#endif
		    if (kill(procs[i].pid, SIGKILL) == 0)
		      found = 1;
		}
		break;
	    case PASSWD:
		for (j = 0; pword[j] != -1; j++)
		  if (pword[j] == procs[i].uid)
		    break;
		if (pword[j] == -1) {
#ifdef DEBUG
		    fprintf(stderr,"kill(%d, SIGKILL)\n", procs[i].pid);
#endif
		    if (kill(procs[i].pid, SIGKILL) == 0)
		      found = 1;
		}
		break;
	    default:
		;
	    }
	}
    }
    return(0);
}


/* Get list of uids of logged in users */

int *get_logged_in()
{
    int fd, i, j;
    char login[9];
#ifdef _IBMR2
    int pwuid;
    struct utmp *u;
#else
#ifdef SYSV
    struct utmpx u;
#else
    struct utmp u;
#endif
    struct passwd *p;
#endif
    static int uids[MAXUSERS];

#ifndef _IBMR2
    fd = open(CLEANUP_UTMP_FILE, O_RDONLY);
    if (fd < 0) {
	fprintf(stderr, "cleanup: can't open %s, %s\n", CLEANUP_UTMP_FILE,
		sys_errlist[errno]);
	return(NULL);
    }
#endif

    /* Always include root & daemon */
    i=0;
    uids[i++] = ROOTUID;
    uids[i++] = DAEMONUID;
#ifdef DEBUG
    fprintf(stderr,"Logged in: 0, 1");
#endif

    /* make sure that this string will always end in NULL */
    login[8] = 0;

#ifdef _IBMR2
    if (setuserdb(S_READ) < 0) {
	fprintf(stderr, "cleanup: can't open user database\n");
	/* this should probably completely blow out if this call fails */
	return(NULL);
    }
    while((u = getutent()) != 0 && i < MAXUSERS) {
	if (u->ut_name[0] == 0)
	    continue;
        if (u->ut_type != USER_PROCESS)
	    continue;

	strncpy(login, u->ut_name, 8);
	if (getuserattr(login, S_ID, &pwuid, SEC_INT) < 0)
	    fprintf(stderr, "cleanup: Warning, could not get uid for user %s\n",
		    login);
	else {
	    /* first check for duplicates */
	    for (j = 0; j < i; j++)
		if (pwuid == uids[j])
		    break;
	    if (i == j) {
		uids[i++] = pwuid;
#ifdef DEBUG
		fprintf(stderr,", %d", pwuid);
#endif
            }
        }
    }
    uids[i] = -1;
    endutent();
    enduserdb();
#else /* !RIOS */
    while (read(fd, &u, sizeof(u)) > 0 && i < MAXUSERS) {
	if (u.ut_name[0] == 0)
	    continue;
#if defined(SOLARIS) || defined(sgi)
        if (u.ut_type != USER_PROCESS)
            continue;
#endif
	strncpy(login, u.ut_name, 8);
	p = getpwnam(login);
	if (p == 0)
	    fprintf(stderr, "cleanup: Warning, could not get uid for user %s\n",
		    login);
	else {
	    /* first check for duplicates */
	    for (j = 0; j < i; j++)
		if (p->pw_uid == uids[j])
		    break;
	    if (i == j) {
		uids[i++] = p->pw_uid;
#ifdef DEBUG
		fprintf(stderr,", %d", p->pw_uid);
#endif
	    }
	}
    }
    uids[i] = -1;
    close(fd);
#endif /* _IBMR2 */

#ifdef DEBUG
    fprintf(stderr,"\n");
#endif
    return(uids);
}


/* Get list of uids in /etc/passwd */

int *get_password_entries()
{
    int i = 0;
    static int uids[MAXUSERS];
    struct passwd *p;

    setpwent();
#ifdef DEBUG
    fprintf(stderr,"In passwd:");
#endif

    while ((p = getpwent()) != NULL && i < MAXUSERS) {
	uids[i++] = p->pw_uid;
#ifdef DEBUG
	fprintf(stderr,", %d", p->pw_uid);
#endif
    }
    if (i >= MAXUSERS) {
	fprintf(stderr, "cleanup: Too many users in /etc/passwd\n");
	return(NULL);
    }

    endpwent();
    uids[i] = -1;
#ifdef DEBUG
    fprintf(stderr,"\n");
#endif

    return(uids);
}


/* snapshot running processes */
#ifndef sgi /* For sgi, we link another object for this routine. */
struct cl_proc *get_processes()
{
#ifndef SOLARIS
    int kmem, nproc, i;
    caddr_t procp;
    struct proc p;
#else
     int i=0;
    struct proc  *p;
    struct pid pid_buf;
    struct cred cred_buf;
#endif
    static struct cl_proc procs[MAXPROCS];
#ifdef _AIX
    char *kernel = "/unix";
#else
#ifdef SOLARIS
    kvm_t *kv;
    int j,pid,tst=0;
#else
    char *kernel = "/vmunix";
#endif
#endif
#ifndef SOLARIS
    char *memory = "/dev/kmem";
#endif

#ifndef SOLARIS
    if (nlist(kernel, nl) != 0) {
	fprintf(stderr, "cleanup: can't get kernel namelist\n");
	return(NULL);
    }
    kmem = open(memory, O_RDONLY);
    if (kmem < 0) {
	fprintf(stderr, "cleanup: can't open %s: %s\n", memory,
		sys_errlist[errno]);
	return(NULL);
    }
#ifdef _AIX
    nproc = (nl[MAX_PROC].n_value - nl[PROC].n_value) /
      sizeof(nl[MAX_PROC].n_value);
#else
    lseek(kmem, nl[NPROC].n_value, L_SET);
    read(kmem, &nproc, sizeof(nproc));
#endif

    lseek(kmem, nl[PROC].n_value, L_SET);
    read(kmem, &procp, sizeof(procp));
#ifdef _AIX
    lseek(kmem, ((int)procp & 0x7fffffff), L_SET);
#else
    lseek(kmem, procp, L_SET);
#endif

    for (i = 0; i < nproc; i++) {
#ifdef _AIX
        readx(kmem, &p, sizeof(p), 1);
#else
	read(kmem, &p, sizeof(p));
#endif
	if (p.p_pid == 0) continue;
#ifdef _AIX
	if (p.p_stat == SNONE) continue;
#endif
	procs[i].pid = p.p_pid;
	procs[i].uid = p.p_uid;
    }
    close(kmem);
    procs[i].pid = procs[i].uid = -1;
    return(procs);
#else /* SOLARIS */
      kv = kvm_open(NULL,NULL,NULL,O_RDONLY,NULL);
      if (kv == NULL ) {
        fprintf(stderr,"%s: can't open kvm\n","Cleanup");
        exit(2);
      }
      tst =  kvm_setproc(kv);
      if ( tst  == -1 ) {
              fprintf(stderr,"kvm_setproc failed\n");
      }
      i=0;
      while ( (p = kvm_nextproc(kv)) != NULL ) {
              if ( p != NULL ) {
                     kvm_read(kv, (unsigned long)p ->p_pidp, 
			      (char *)&pid_buf, sizeof(struct pid));
		     kvm_read(kv, (unsigned long)p ->p_cred,
			      (char *)&cred_buf, sizeof(struct cred));
		      if (pid_buf.pid_id == 0)
                              continue;
                      else {
                              procs[i].pid = pid_buf.pid_id;
			      procs[i].uid = cred_buf.cr_uid;
                              i++;
                      }
              }
     }
    kvm_close(kv);
    procs[i].pid = procs[i].uid = -1;
    return(procs);
#endif
}
#endif


/* lock protocol for /etc/passwd & /etc/group using /etc/ptmp & /etc/gtmp */

int
lock_file(fn)
char *fn;
{
    int i, fd;

    for (i = 0; i < 10; i++)
      if ((fd = open(fn, O_RDWR | O_CREAT | O_EXCL, 0644)) == -1 &&
	  errno == EEXIST)
	sleep(1);
      else
	break;
    if (fd == -1) {
	if (i < 10)
	  fprintf(stderr, "cleanup: unable to lock passwd file: %s\n",
		  sys_errlist[errno]);
	else
	  (void) unlink(fn);
    }
    close(fd);
}


unlock_file(fn)
char *fn;
{
    (void) unlink(fn);
}


/* Update the passwd and group files.  The passed set of uids specifies
 * who should be in the passwd file.
 */

#ifdef _IBMR2
rewrite_passwd(users)
int *users;
{
    int i, uid, count;
    char *usr, *grp, *ulist;
    static char *empty = "\0";
    
    setuserdb(S_READ|S_WRITE);

    /* Note: This routine cannot properly track reference counts for
     * groups because there is no reference count on permanent users.
     */

    /* Remove any inactive temporary users */
    usr = nextuser(S_LOCAL, NULL);
    while (usr) {
	if (getuserattr(usr, S_ID, &uid, SEC_INT) == -1)
	    uid = -1;
#ifdef DEBUG
	fprintf(stderr,"Checking user %s (uid %d)\n", usr, uid);
#endif
	if (getuserattr(usr, "athena_temp", &count, SEC_INT) == 0) {
	    for (i = 0; users[i] >= 0; i++)
		if (users[i] == uid)
		    break;
	    if (users[i] < 0 || users[i] != uid) {
#ifdef DEBUG
		fprintf(stderr,"Deleting user %s from passwd file\n", usr);
#endif
		putuserattr(usr, S_GROUPS, (void *)empty, SEC_LIST);
		putuserattr(usr, (char *)0, (void *)0, SEC_COMMIT);
		rmufile(usr, 1, USER_TABLE);
	    }
	}
	usr=nextuser(NULL, NULL);
    }

    /* Check all temporary groups for inactive users. */
    /* Remove any empty temporary groups. */
    grp = nextgroup(S_LOCAL, NULL);
    while (grp) {
	if (getgroupattr(grp, "athena_temp", (void *)&count, SEC_INT) == 0 ||
	    getgroupattr(grp, "athena_temp", (void *)&count, SEC_BOOL) == 0) {
	    if (getgroupattr(grp, S_USERS, (void *)&usr, SEC_LIST)) {
		rmufile(grp, 0, GROUP_TABLE);
	    } else {
		count = 0;
		ulist = 0;
		while (*usr) {
		    if (getuserattr(usr, S_ID, (void *)&uid, SEC_INT) == 0) {
			for (i=0; users[i]>=0; i++)
			    if (uid==users[i]) {
				if (ulist)
				    ulist = (char *)realloc(ulist,strlen(ulist)+strlen(usr)+1);
				else {
				    ulist = (char *)malloc(strlen(usr)+2);
				    *ulist = 0;
				}
				strcat(ulist,usr);
				strcat(ulist,",");
				count++;
				break;
			    }
		    }
		    while (*usr) usr++;
		    usr++;
		}
		if (count) {
#ifdef DEBUG
		    printf("Group %s reduced to %s\n", grp, ulist);
#endif
		    for (usr=ulist; *usr; usr++)
			if (*usr==',') *usr=0;
		    putgroupattr(grp, S_USERS, (void *)ulist, SEC_LIST);
		    putgroupattr(grp, (char *)0, (void *)0, SEC_COMMIT);
		    free(ulist);
		} else {
#ifdef DEBUG
		    printf("Deleting group %s\n", grp);
#endif
		    putgroupattr(grp, S_USERS, (void *)empty, SEC_LIST);
		    putgroupattr(grp, (char *)0, (void *)0, SEC_COMMIT);
		    rmufile(grp, 0, GROUP_TABLE);
		}
	    }
	}
	grp = nextgroup(NULL, NULL);
    }
    enduserdb();
}

#else	/* !_IBMR2 */

rewrite_passwd(users)
int *users;
{
#ifdef SOLARIS
    FILE *in, *in1, *out, *out1;
    char username[10];
    struct passwd *pw;
    char buffer1[512];
#else
    FILE *in, *out;
#endif
    char buffer[512], *p;
    int in_passwd[MAXUSERS], user = 0, i, uid;

    in = fopen("/etc/passwd.local", "r");
    if (in == NULL) {
        fprintf(stderr, "cleanup: unable to open /etc/passwd.local: %s\n",
	        sys_errlist[errno]);
#ifndef SOLARIS 
        return(0);				/* non fatal error */
#endif
    }

    out = fopen("/etc/passwd.new", "w");
    if (out == NULL) {
	fprintf(stderr, "cleanup: unable to open /etc/passwd.new: %s\n",
		sys_errlist[errno]);
	fclose(in);
	return(-1);
    }
    if (chmod("/etc/passwd.new", 0644)) {
	fprintf(stderr, "cleanup: unable to change mode of /etc/passwd.new: %s\n",
		sys_errlist[errno]);
	fclose(in);
	fclose(out);
	return(-1);
    }
#ifdef SOLARIS
    out1 = fopen("/etc/shadow.new", "w");
    if (out1 == NULL) {
	fprintf(stderr, "cleanup: unable to open /etc/shadow.new: %s\n",
		sys_errlist[errno]);
	fclose(in);
        fclose(out);
	return(-1);
    }
    if (chmod("/etc/shadow.new", 0600)) {
	fprintf(stderr, "cleanup: unable to change mode of /etc/shadow.new: %s\n",
		sys_errlist[errno]);
	fclose(in);
	fclose(out);
        fclose(out1);
	return(-1);
    }
#endif

    /* copy /etc/passwd.local, keeping track of who is in it */
    while (in && fgets(buffer, sizeof(buffer), in)) {
	fputs(buffer, out);
	p = strchr(buffer, ':');
	if (p) {
	    p = strchr(p + 1, ':');
	    if (p) {
		p++;
		in_passwd[user++] = atoi(p);
	    }
	}
    }

    fclose(in);
    in = fopen("/etc/passwd", "r");
    if (in == NULL)
	fprintf(stderr, "cleanup: unable to open /etc/passwd: %s\n",
		sys_errlist[errno]);

#ifdef SOLARIS
    in1 = fopen("/etc/shadow", "r");
    if (in1 == NULL)
	fprintf(stderr, "cleanup: unable to open /etc/shadow: %s\n",
		sys_errlist[errno]);
#endif
    /* now process /etc/passwd, avoiding duplicates */
    while (in && fgets(buffer, sizeof(buffer), in)) {
	uid = -1;
	p = strchr(buffer, ':');
	if (p) {
	    p = strchr(p + 1, ':');
	    if (p) {
		p++;
		uid = atoi(p);
	    }
	}
	/* if we can't find the uid in the entry, give up */
	if (uid == -1) {
#ifdef DEBUG
	    printf("Skipping malformed passwd entry: %s\n", buffer);
#endif
	    continue;
	}

	for (i = 0; i < user; i++)
	    if (in_passwd[i] == uid)
		break;
	/* if already in passwd file, skip */
	if (i < user && in_passwd[i] == uid) {
#ifdef DEBUG
	    printf("Skipping entry %d already in passwd file: %s", uid, buffer);
#endif
	    continue;
	}

        for (i = 0; users[i] >= 0; i++)
	    if (users[i] == uid)
		break;
	/* if not supposed to be in passwd file, skip */
	if (users[i] < 0 || users[i] != uid) {
#ifdef DEBUG
	    printf("Skipping entry shouldn't be in passwd file: %s", buffer);
#endif
	    continue;
	}

	fputs(buffer, out);
	in_passwd[user++] = uid;
    }
    fclose(in);
    fclose(out);

    if (unlink("/etc/passwd"))
	fprintf(stderr, "cleanup: unable to remove /etc/passwd: %s\n",
		sys_errlist[errno]);
    if (rename("/etc/passwd.new", "/etc/passwd"))
	fprintf(stderr, "cleanup: failed to rename /etc/passwd.new to /etc/passwd: %s\n",
		sys_errlist[errno]);
#ifdef SOLARIS
    /* now process /etc/shadow, avoiding duplicates */
    memset(buffer,0,sizeof(buffer));
    while (in1 && fgets(buffer, sizeof(buffer), in1)) {
       uid = -1;
       strcpy(buffer1, buffer);
       p = strchr(buffer1, ':');
       if (p) {
	    *p = 0;
            strncpy(username, buffer1, sizeof(username) - 1);
	    username[sizeof(username) - 1] = 0;
            pw = getpwnam(username);
            if (pw)
               uid = pw-> pw_uid;
	  }
       if (uid !=-1)
	       fputs(buffer, out1);
    memset(buffer,0,sizeof(buffer));
    }
    fclose(in1);
    fclose(out1);
    if (unlink("/etc/shadow"))
	fprintf(stderr, "cleanup: unable to remove /etc/shadow: %s\n",
		sys_errlist[errno]);
    if (rename("/etc/shadow.new", "/etc/shadow"))
	fprintf(stderr, "cleanup: failed to rename /etc/shadow.new to /etc/shadow: %s\n",
		sys_errlist[errno]);
#endif


    in_passwd[user] = -1;
    make_group(in_passwd);
    return(0);
}

/* Rebuild the group file, keeping any group which has a member who is 
 * in the passwd file.
 */

make_group(uids)
int *uids;
{
    int i, n, match, nuid, len;
    char buf[10240], *p, *p1;
    char llist[MAXUSERS][9];
    struct passwd *pw;
    FILE *new, *old;

    if ((old = fopen("/etc/group", "r")) == NULL) {
	fprintf(stderr,"cleanup: Couldn't open \"/etc/group\", %s.\n",
		sys_errlist[errno]);
	return;
    }
    if ((new = fopen("/etc/group.new", "w")) == NULL) {
	fprintf(stderr,"cleanup: Couldn't open \"/etc/group.new\", %s.\n",
		sys_errlist[errno]);
	fclose(old);
	return;
    }
    if (chmod("/etc/group.new", 0644)) {
	fprintf(stderr, "cleanup: Couldn't change mode of \"/etc/group.new\", %s\n",
		sys_errlist[errno]);
	fclose(old);
	fclose(new);
	return;
    }

#ifdef DEBUG
    printf("UIDs for /etc/group:");
#endif
    for (nuid = 0; uids[nuid] >= 0; nuid++) {
#ifdef DEBUG
	printf(", %d", uids[nuid]);
#endif
    }
#ifdef DEBUG
    printf("\n");
#endif

    /* build list of users in the passwd file */
#ifdef DEBUG
    printf("logins for /etc/group:");
#endif
    for (i = 0; i < nuid; i++) {
	pw = getpwuid(uids[i]);
	if (pw != NULL)
	    strcpy(llist[i], pw->pw_name);
#ifdef DEBUG
	printf(", %s", llist[i]);
#endif
    }
#ifdef DEBUG
    printf("\n");
#endif

    /* loop over each line in the group file */
    while (fgets(buf, sizeof(buf), old)) {
	/* take out tailing \n */
	n = strlen(buf);
	if (n) {
	    if (buf[n - 1] == '\n')
		buf[n - 1] = 0;
	    else
		fprintf(stderr, "cleanup: warning, too long group entry truncated\n");
	}
	if ((p = strchr(buf, ':')) == 0) {
	    fprintf(stderr, "cleanup: Corrupt group entry \"%s\".\n", buf);
	    continue;
        }
	if ((p = strchr(p+1, ':')) == 0 ||
	    (p = strchr(p+1, ':')) == 0) {
	    fprintf(stderr, "cleanup: Corrupt group entry \"%s\".\n", buf);
	    continue;
	}
	match = 0;
	p++;

	/* if existing group has no members, keep it */
	if (!*p)
	    fprintf(new, "%s\n", buf);
	else {
	    len = p - buf;

	    /* loop over each member of the group */
	    while (p != NULL) {
		p1 = strchr(p, ',');
		if (p1)
		    n = p1 - p;
		else
		    n = strlen(p);
		/* compare against each user in the passwd file */
		for (i = 0; i < nuid; i++) {
		    if (!strncmp(p, llist[i], n)) {
			if (!match) {
			    fprintf(new, "%.*s%s", len, buf, llist[i]);
			    match = 1;
			} else
			    fprintf(new, ",%s", llist[i]);
			break;
		    }
		}
		if (p1)
		    p = p1 + 1;
		else
		    p = NULL;
	    }
	}
	if (match)
	    fprintf(new, "\n");
#ifdef DEBUG
	else
	    printf("<<<%s\n", buf);
#endif
    }

    if (fclose(new) || fclose(old)) {
	fprintf(stderr, "cleannup: Error closing group file: sys_errlist[errno]\n");
	return;
    }
    if (unlink("/etc/group") == -1)
	perror("unlink");
    if (rename("/etc/group.new", "/etc/group") == -1)
	perror("rename");
}
#endif	/* !_IBMR2 */
