
#define	LOG_TAG		"STT:process"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <errno.h>

//#include <cutils/log.h>

#include "libcommon.h"
#include "process.h"
#include "common.h"

//#define gettid() syscall(__NR_gettid)

int find_all_pids_of_bin (const char *bin_name, pid_t *pid_array, int array_count)
{
	char cmdkey [PATH_MAX];
	char buffer [PATH_MAX];
	struct dirent *entry;
	DIR *dir;
	int fd;
	pid_t pid;

	int count, idx, i;

	snprintf (cmdkey, sizeof (cmdkey), " (%s) ", bin_name);
	cmdkey [sizeof (cmdkey) - 1] = 0;

	if ((dir = opendir ("/proc")) == NULL)
		return -1;

	count = idx = 0;

	while ((entry = readdir (dir)) != NULL)
	{
		if (entry->d_type != DT_DIR)
			continue;

		for (i = 0; entry->d_name [i] != 0; i ++)
			if (! isdigit (entry->d_name [i]))
				break;

		if (entry->d_name [i] != 0)
			continue;

		pid = (pid_t) atoi (entry->d_name);

		snprintf (buffer, sizeof (buffer) - 1, "/proc/%d/stat", pid);
		buffer [sizeof (buffer) - 1] = 0;

		if ((fd = open (buffer, O_RDONLY)) < 0)
			continue;

		if (read (fd, buffer, sizeof (buffer)) < 0)
			buffer [0] = 0;

		buffer [sizeof (buffer) - 1] = 0;

		close (fd);

		if (strstr (buffer, cmdkey))
		{
			count ++;

			if (pid_array && (idx < array_count))
			{
				pid_array [idx ++] = pid;
			}
		}
	}

	closedir (dir);
	return count;
}

/*
 * return a PID_INFO list of current processes.
 */
GLIST *find_all_pids (void)
{
	char buffer [PATH_MAX];
	char *bin, *ppid;
	struct dirent *entry;
	DIR *dir;
	int i, fd;
	pid_t pid;

	PID_INFO *pi;

	GLIST_NEW (head);

	if ((dir = opendir ("/proc")) == NULL)
		return NULL;

	while ((entry = readdir (dir)) != NULL)
	{
		if (entry->d_type != DT_DIR)
			continue;

		for (i = 0; entry->d_name [i] != 0; i ++)
			if (! isdigit (entry->d_name [i]))
				break;

		if (entry->d_name [i] != 0)
			continue;

		pid = (pid_t) atoi (entry->d_name);

		snprintf (buffer, sizeof (buffer) - 1, "/proc/%d/stat", pid);
		buffer [sizeof (buffer) - 1] = 0;

		if ((fd = open (buffer, O_RDONLY)) < 0)
			continue;

		if (read (fd, buffer, sizeof (buffer)) < 0)
			buffer [0] = 0;

		buffer [sizeof (buffer) - 1] = 0;

		close (fd);

		ppid = NULL;

		bin = strchr (buffer, ')');

		if (bin)
		{
			*bin = 0;

			ppid = bin + 4;

			for (bin = ppid; isdigit (*bin); bin ++);

			*bin = 0;
		}

		bin = strchr (buffer, '(');

		if (bin) bin ++; else bin = buffer;

		pi = (PID_INFO *)malloc (sizeof (PID_INFO));

		if (pi)
		{
			pi->pid = pid;

			pi->ppid = ppid ? atoi (ppid) : 0;

			strncpy (pi->bin, bin, sizeof (pi->bin));
			pi->bin [sizeof (pi->bin) - 1] = 0;

			glist_add (& head, pi);
		}
	}

	closedir (dir);
	return head;
}

/*
 * return a FD_INFO list of current opened file handlers.
 */
GLIST *find_all_fds (void)
{
	char buffer [PATH_MAX];
	struct dirent *entry;
	DIR *dir;
	FD_INFO *pi;
	pid_t pid;

	GLIST_NEW (head);

	pid = getpid ();

	snprintf (buffer, sizeof (buffer), "/proc/%d/fd", pid);
	buffer [sizeof (buffer) - 1] = 0;

	if ((dir = opendir (buffer)) != NULL)
	{
		while ((entry = readdir (dir)) != NULL)
		{
			if (entry->d_type == DT_DIR)
				continue;

			pi = (FD_INFO *)malloc (sizeof (FD_INFO));

			if (pi)
			{
				pi->fd = atoi (entry->d_name);

				snprintf (buffer, sizeof (buffer), "/proc/%d/fd/%s", pid, entry->d_name);
				buffer [sizeof (buffer) - 1] = 0;

				if (stat (buffer, & pi->st) == -1)
				{
					bzero (& pi->st, sizeof (pi->st));
				}

				memset (pi->link, 0, sizeof (pi->link));
				readlink (buffer, pi->link, sizeof (pi->link));
				pi->link [sizeof (pi->link) - 1] = 0;

				glist_add (& head, pi);
			}
		}

		closedir (dir);
	}

	return head;
}

/*
 * close all opened fds, do not do memory alloc/free in this function to make it safe for forked child process.
 */
void close_all_fds (GLIST *fds)
{
	GLIST *p;
	FD_INFO *pi;

	for (p = fds; p; p = GLIST_NEXT (p))
	{
		pi = (FD_INFO *)GLIST_DATA (p);

		/*
		 * close all except properties
		 */
		if (strstr (pi->link, "__properties__")) /* "/dev/__properties__ (deleted)" */
		{
			//fLOGD ("close_all_fds(): ignore [%d][%s]\n", pi->fd, pi->link);
			continue;
		}

		/*
		 * close all except log devices
		 */
		if (strstr (pi->link, "/dev/log/")) /* "/dev/log/main" */
		{
			//fLOGD ("close_all_fds(): ignore [%d][%s]\n", pi->fd, pi->link);
			continue;
		}

	#if 0
		/*
		 * close all except cpuctl
		 */
		if (strstr (pi->link, "cpuctl")) /* "/dev/cpuctl/tasks" and "/dev/cpuctl/bg_non_interactive/tasks" */
		{
			//fLOGD ("close_all_fds(): ignore [%d][%s]\n", pi->fd, pi->link);
			continue;
		}

		/*
		 * close all except inotify
		 */
		if (strstr (pi->link, "inotify")) /* "anon_inode:inotify" */
		{
			//fLOGD ("close_all_fds(): ignore [%d][%s]\n", pi->fd, pi->link);
			continue;
		}

		/*
		 * close all except /proc/ entries
		 */
		if (strstr (pi->link, "/proc/")) /* "/proc/xxx/fd" */
		{
			//fLOGD ("close_all_fds(): ignore [%d][%s]\n", pi->fd, pi->link);
			continue;
		}
	#endif

	#if 1	/*
		 * need to close stdin/out/error on some build lines or any output would cause SIGPIPE
		 */
		/*
		 * 20130530: closing 0, 1, 2 will cause execv() failed on Android 4.3, we still need to keep them opened but add a checking on the link name
		 */
		/*
		 * close all except 0, 1, 2
		 */
		if ((pi->fd <= 2) && (strcmp (pi->link, "/dev/null") == 0))
		{
			//fLOGD ("close_all_fds(): ignore [%d][%s]\n", pi->fd, pi->link);
			continue;
		}
	#endif

		//fLOGD ("close_all_fds(): close [%d][%s]\n", pi->fd, pi->link);

		close (pi->fd);

		//fLOGD ("close_all_fds(): closed\n");
	}
}

int is_process_alive (int pid)
{
	char p [32];
	snprintf (p, sizeof (p) - 1, "/proc/%d/maps", pid);
	p [sizeof (p) - 1] = 0;
	return (! access (p, F_OK));
}

char get_pid_stat (pid_t pid)
{
	char p [PATH_MAX], *c;
	int fd;
	snprintf (p, sizeof (p) - 1, "/proc/%d/stat", pid);
	p [sizeof (p) - 1] = 0;
	if ((fd = open (p, O_RDONLY)) < 0)
		return PROCESS_STATE_UNKNOWN;
	memset (p, 0, sizeof (p));
	read (fd, p, sizeof (p));
	p [sizeof (p) - 1] = 0;
	close (fd);
	for (c = p; *c; c ++) if ((*c == ')') && (*(c + 1) == ' ')) break;
	return *(c + 2);
}

/*
 * return -1:no such process, 0:not a zombi, 1:is a zombi
 */
int is_process_zombi (int pid)
{
	char s = get_pid_stat (pid);
	return IS_PROCESS_STATE_ZOMBIE (s) ? 1 : (IS_PROCESS_STATE_UNKNOWN (s) ? -1 : 0);
}

int is_thread_alive (pthread_t tid)
{
	if (tid == (pthread_t) -1)
	{
		return 0;
	}
	if (pthread_kill (tid, 0) != 0)
	{
		return 0;
	}
	return 1;
}

int get_pid_name (pid_t pid, char *buffer, int buflen)
{
	char path [256];
	int fd;

	if ((! buffer) || (buflen <= 0))
		return -1;

	buffer [0] = 0;

	snprintf (path, sizeof (path), "/proc/%d/stat", pid);
	path [sizeof (path) - 1] = 0;

	if ((fd = open (path, O_RDONLY)) < 0)
		return -1;

	bzero (path, 0);

	if (read (fd, path, sizeof (path)) >= 0)
	{
		char *ph, *pe;

		for (ph = path; *ph && (*ph != '('); ph ++);

		if (*ph == '(')
		{
			for (pe = ph + 1; *pe && (*pe != ')'); pe ++);

			if (*pe == ')')
			{
				*pe = 0;
				strncpy (buffer, ph + 1, buflen);
				close (fd);
				return 0;
			}
		}
	}

	close (fd);
	return -1;
}

int get_pid_cmdline (pid_t pid, char *buffer, int buflen)
{
	char path [256];
	int fd, count;

	if ((! buffer) || (buflen <= 0))
		return -1;

	buffer [0] = 0;

	snprintf (path, sizeof (path), "/proc/%d/cmdline", pid);
	path [sizeof (path) - 1] = 0;

	if ((fd = open (path, O_RDONLY)) < 0)
		return -1;

	bzero (path, 0);

	if ((count = read (fd, path, sizeof (path))) > 0)
	{
		int i;

		for (i = 0; i < count; i ++)
			if (path [i] == 0)
				path [i] = ' ';

		path [count - 1] = 0;

		strncpy (buffer, path, buflen);
		close (fd);
		return 0;
	}

	close (fd);
	return -1;
}

pid_t getpppid (void)
{
	char path [256];
	int fd;

	snprintf (path, sizeof (path), "/proc/%d/stat", getppid ());
	path [sizeof (path) - 1] = 0;

	if ((fd = open (path, O_RDONLY)) < 0)
		return -1;

	bzero (path, 0);

	if (read (fd, path, sizeof (path)) >= 0)
	{
		char *ph, *pe;

		for (ph = path; *ph && (*ph != ')'); ph ++);

		if (*ph == ')')
		{
			for (ph += 2; *ph && (! isdigit (*ph)); ph ++);

			if (isdigit (*ph))
			{
				for (pe = ph + 1; *pe && isdigit (*pe); pe ++);
				*pe = 0;
				close (fd);
				return atoi (ph);
			}
		}
	}

	close (fd);
	return -1;
}

static void *thread_system (void *arg)
{
	char name [16] = "system:";
	int res;

	pthread_detach (pthread_self ());

	strncat (name, (const char *) arg, sizeof (name) - strlen (name) - 1);
	name [sizeof (name) - 1] = 0;

	prctl (PR_SET_NAME, (unsigned long) name, 0, 0, 0);

	DM ("system_in_thread LD_LIBRARY_PATH=[%s]\n", getenv ("LD_LIBRARY_PATH"));

	res = system ((const char *) arg);

	DM ("system_in_thread returned %d\n", res);
	return NULL;
}

void system_in_thread (const char *command)
{
	pthread_t thid = -1;

	DM ("system_in_thread [%s]\n", command);

	if (pthread_create (& thid, NULL, thread_system, (void *) command) != 0)
	{
		DM ("system_in_thread %s\n", strerror (errno));
	}
}

char *alloc_waitpid_status_text (int status)
{
	char buf [32];

	if (WIFEXITED (status))
	{
		snprintf (buf, sizeof (buf), "exit status=%d", WEXITSTATUS (status));
	}
	else if (WIFSIGNALED (status))
	{
		snprintf (buf, sizeof (buf), "exit signal=%d, coredump=%d", WTERMSIG (status), WCOREDUMP (status));
	}
	else if (WIFSTOPPED (status))
	{
		snprintf (buf, sizeof (buf), "stopped signal=%d", WSTOPSIG (status));
	}
	/*
	else if (WIFCONTINUED (status))
	{
		snprintf (buf, sizeof (buf), "continued by SIGCONT");
	}
	*/
	else
	{
		snprintf (buf, sizeof (buf), "unknown status=0x%08X", status);
	}

	buf [sizeof (buf) - 1] = 0;
	return strdup (buf);
}

void dump_environ (void)
{
	char buffer [4096];
	int tid;
	int fd, count;
	int h, t;

	tid = 0;//gettid ();

	sprintf (buffer, "/proc/%d/environ", tid);

	if ((fd = open (buffer, O_RDONLY)) < 0)
	{
		DM ("dump_environ: failed to open [%s]: %s\n", buffer, strerror (errno));
		return;
	}

	count = read (fd, buffer, sizeof (buffer));

	close (fd);

	if (count < 0)
	{
		DM ("dump_environ: failed to read the environ of tid [%d]: %s\n", tid, strerror (errno));
		return;
	}

	DM ("dump_environ: begin, count=%d\n", count);

	if (count == sizeof (buffer)) count --;
	buffer [count] = 0;

	for (h = 0, t = 1; h < count; t ++)
	{
		if (buffer [t] == 0)
		{
			DM ("dump_environ: [%s]\n", & buffer [h]);

			h = ++ t;
		}
	}

	DM ("dump_environ: end\n");
}

#ifdef __cplusplus
}
#endif

