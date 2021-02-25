

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>


#include <getopt.h>

#include <string.h>
#include <mntent.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>
#include <pwd.h>
#include <grp.h>
#include <sys/ioctl.h>
#include <sys/quota.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/xattr.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <ctype.h>
#include <zlib.h>
#include <libgen.h>
#include <asm/byteorder.h>
#include "lfs_project.h"
#include "lfs_project.c"

// #include <libcfs/util/string.h>
// #include <libcfs/util/ioctl.h>
// #include <libcfs/util/parser.h>
// #include <libcfs/util/string.h>
// #include "../../include/libcfs/util/string.h"
// #include "/usr/src/lustre-client-2.12.6/libcfs/include/libcfs/util/ioctl.h"
// #include "../../include/libcfs/util/parser.h"
// #include "../../include/libcfs/util/string.h"
#include <lustre/lustreapi.h>
#include <linux/lustre/lustre_ver.h>
#include <linux/lustre/lustre_param.h>
#include <linux/lnet/nidstr.h>
// #include <lnetconfig/cyaml.h>

#define LUSTRE_Q_GETQUOTAPOOL 0x80000f /* get user pool quota */
#define LUSTRE_Q_SETQUOTAPOOL 0x800010 /* set user pool quota */
#define LUSTRE_Q_GETINFOPOOL 0x800011  /* get pool quota info */
#define LUSTRE_Q_SETINFOPOOL 0x800012  /* set pool quota info */

#define NOTIFY_GRACE "notify"
#define NOTIFY_GRACE_TIME LQUOTA_GRACE_MASK

#define CMD_COMPLETE 0
#define CMD_INCOMPLETE 1
#define CMD_NONE 2
#define CMD_AMBIG 3
#define CMD_HELP 4

// const char	*progname;

struct project_quota_resp
{
	char block_used[32];
	char block_soft_limit[32];
	char block_hard_limit[32];
	char inode_used[32];
	char inode_soft_limit[32];
	char inode_hard_limit[32];
};

// ________                __
// \_____  \  __ __  _____/  |______
//  /  / \  \|  |  \/  _ \   __\__  \  
// /   \_/.  \  |  (  <_> )  |  / __ \_
// \_____\ \_/____/ \____/|__| (____  /
//        \__>                      \/

static int name2uid(unsigned int *id, const char *name)
{
	struct passwd *passwd;

	passwd = getpwnam(name);
	if (passwd == NULL)
		return -ENOENT;
	*id = passwd->pw_uid;

	return 0;
}

static int name2gid(unsigned int *id, const char *name)
{
	struct group *group;

	group = getgrnam(name);
	if (group == NULL)
		return -ENOENT;
	*id = group->gr_gid;

	return 0;
}

static inline int name2projid(unsigned int *id, const char *name)
{
	return -ENOTSUP;
}

static int uid2name(char **name, unsigned int id)
{
	struct passwd *passwd;

	passwd = getpwuid(id);
	if (passwd == NULL)
		return -ENOENT;
	*name = passwd->pw_name;

	return 0;
}

static inline int gid2name(char **name, unsigned int id)
{
	struct group *group;

	group = getgrgid(id);
	if (group == NULL)
		return -ENOENT;
	*name = group->gr_name;

	return 0;
}

static inline int vscnprintf(char *buf, size_t bufsz, const char *format, va_list args)
{
	int ret;

	if (!bufsz)
		return 0;

	ret = vsnprintf(buf, bufsz, format, args);
	return (bufsz > ret) ? ret : bufsz - 1;
}

static inline int scnprintf(char *buf, size_t bufsz, const char *format, ...)
{
	int ret;
	va_list args;

	va_start(args, format);
	ret = vscnprintf(buf, bufsz, format, args);
	va_end(args);

	return ret;
}

static char *__sec2str(time_t seconds, char *buf)
{
	const char spec[] = "smhdw";
	const unsigned long mult[] = {1, 60, 60 * 60, 24 * 60 * 60, 7 * 24 * 60 * 60};
	unsigned long c;
	char *tail = buf;
	int i;

	for (i = ARRAY_SIZE(mult) - 1; i >= 0; i--)
	{
		c = seconds / mult[i];

		if (c > 0 || (i == 0 && buf == tail))
			tail += scnprintf(tail, 40 - (tail - buf), "%lu%c", c,
							  spec[i]);

		seconds %= mult[i];
	}

	return tail;
}

static void sec2str(time_t seconds, char *buf, int rc)
{
	char *tail = buf;

	if (rc)
		*tail++ = '[';

	tail = __sec2str(seconds, tail);

	if (rc && tail - buf < 39)
	{
		*tail++ = ']';
		*tail++ = 0;
	}
}

static void diff2str(time_t seconds, char *buf, time_t now)
{

	buf[0] = 0;
	if (!seconds)
		return;
	if (seconds <= now)
	{
		strcpy(buf, "none");
		return;
	}
	__sec2str(seconds - now, buf);
}

static void print_quota_title(char *name, struct if_quotactl *qctl,
							  bool human_readable, bool show_default)
{
	if (show_default)
	{
		printf("Disk default %s quota:\n", qtype_name(qctl->qc_type));
		printf("%15s %8s%8s%8s %8s%8s%8s\n",
			   "Filesystem", "bquota", "blimit", "bgrace",
			   "iquota", "ilimit", "igrace");
	}
	else
	{
		printf("Disk quotas for %s %s (%cid %u):\n",
			   qtype_name(qctl->qc_type), name,
			   *qtype_name(qctl->qc_type), qctl->qc_id);
		printf("%15s%8s %7s%8s%8s%8s %7s%8s%8s\n",
			   "Filesystem", human_readable ? "used" : "kbytes",
			   "quota", "limit", "grace",
			   "files", "quota", "limit", "grace");
	}
}

static void kbytes2str(__u64 num, char *buf, int buflen, bool h)
{
	if (!h)
	{
		snprintf(buf, buflen, "%ju", (uintmax_t)num);
	}
	else
	{
		if (num >> 40)
			snprintf(buf, buflen, "%5.4gP",
					 (double)num / ((__u64)1 << 40));
		else if (num >> 30)
			snprintf(buf, buflen, "%5.4gT",
					 (double)num / (1 << 30));
		else if (num >> 20)
			snprintf(buf, buflen, "%5.4gG",
					 (double)num / (1 << 20));
		else if (num >> 10)
			snprintf(buf, buflen, "%5.4gM",
					 (double)num / (1 << 10));
		else
			snprintf(buf, buflen, "%ju%s", (uintmax_t)num, "k");
	}
}

#define STRBUF_LEN 32
static void print_quota(char *mnt, struct if_quotactl *qctl, int type,
						int rc, bool h, bool show_default)
{
	time_t now;

	time(&now);

	if (qctl->qc_cmd == LUSTRE_Q_GETQUOTA || qctl->qc_cmd == Q_GETOQUOTA ||
		qctl->qc_cmd == LUSTRE_Q_GETQUOTAPOOL ||
		qctl->qc_cmd == LUSTRE_Q_GETDEFAULT)
	{
		int bover = 0, iover = 0;
		struct obd_dqblk *dqb = &qctl->qc_dqblk;
		char numbuf[3][STRBUF_LEN];
		char timebuf[40];
		char strbuf[STRBUF_LEN];

		if (dqb->dqb_bhardlimit &&
			lustre_stoqb(dqb->dqb_curspace) >= dqb->dqb_bhardlimit)
		{
			bover = 1;
		}
		else if (dqb->dqb_bsoftlimit && dqb->dqb_btime)
		{
			if (dqb->dqb_btime > now)
			{
				bover = 2;
			}
			else
			{
				bover = 3;
			}
		}

		if (dqb->dqb_ihardlimit &&
			dqb->dqb_curinodes >= dqb->dqb_ihardlimit)
		{
			iover = 1;
		}
		else if (dqb->dqb_isoftlimit && dqb->dqb_itime)
		{
			if (dqb->dqb_itime > now)
			{
				iover = 2;
			}
			else
			{
				iover = 3;
			}
		}

		if (strlen(mnt) > 15)
			printf("%s\n%15s", mnt, "");
		else
			printf("%15s", mnt);

		if (bover)
			diff2str(dqb->dqb_btime, timebuf, now);
		else if (show_default)
			snprintf(timebuf, sizeof(timebuf), "%llu",
					 (unsigned long long)dqb->dqb_btime);

		kbytes2str(lustre_stoqb(dqb->dqb_curspace),
				   strbuf, sizeof(strbuf), h);
		if (rc == -EREMOTEIO)
			sprintf(numbuf[0], "%s*", strbuf);
		else
			sprintf(numbuf[0], (dqb->dqb_valid & QIF_SPACE) ? "%s" : "[%s]", strbuf);

		kbytes2str(dqb->dqb_bsoftlimit, strbuf, sizeof(strbuf), h);
		if (type == QC_GENERAL)
			sprintf(numbuf[1], (dqb->dqb_valid & QIF_BLIMITS) ? "%s" : "[%s]", strbuf);
		else
			sprintf(numbuf[1], "%s", "-");

		kbytes2str(dqb->dqb_bhardlimit, strbuf, sizeof(strbuf), h);
		sprintf(numbuf[2], (dqb->dqb_valid & QIF_BLIMITS) ? "%s" : "[%s]", strbuf);

		if (show_default)
			printf(" %6s %7s %7s", numbuf[1], numbuf[2], timebuf);
		else
			printf(" %7s%c %6s %7s %7s",
				   numbuf[0], bover ? '*' : ' ', numbuf[1],
				   numbuf[2], bover > 1 ? timebuf : "-");

		if (iover)
			diff2str(dqb->dqb_itime, timebuf, now);
		else if (show_default)
			snprintf(timebuf, sizeof(timebuf), "%llu",
					 (unsigned long long)dqb->dqb_itime);

		snprintf(numbuf[0], sizeof(numbuf),
				 (dqb->dqb_valid & QIF_INODES) ? "%ju" : "[%ju]",
				 (uintmax_t)dqb->dqb_curinodes);

		if (type == QC_GENERAL)
			sprintf(numbuf[1], (dqb->dqb_valid & QIF_ILIMITS) ? "%ju" : "[%ju]",
					(uintmax_t)dqb->dqb_isoftlimit);
		else
			sprintf(numbuf[1], "%s", "-");

		sprintf(numbuf[2], (dqb->dqb_valid & QIF_ILIMITS) ? "%ju" : "[%ju]", (uintmax_t)dqb->dqb_ihardlimit);

		if (show_default)
			printf(" %6s %7s %7s", numbuf[1], numbuf[2], timebuf);
		else if (type != QC_OSTIDX)
			printf(" %7s%c %6s %7s %7s",
				   numbuf[0], iover ? '*' : ' ', numbuf[1],
				   numbuf[2], iover > 1 ? timebuf : "-");
		else
			printf(" %7s %7s %7s %7s", "-", "-", "-", "-");
		printf("\n");
	}
	else if (qctl->qc_cmd == LUSTRE_Q_GETINFO || LUSTRE_Q_GETINFOPOOL ||
			 qctl->qc_cmd == Q_GETOINFO)
	{
		char bgtimebuf[40];
		char igtimebuf[40];

		if (qctl->qc_dqinfo.dqi_bgrace == NOTIFY_GRACE_TIME)
			strncpy(bgtimebuf, NOTIFY_GRACE, 40);
		else
			sec2str(qctl->qc_dqinfo.dqi_bgrace, bgtimebuf, rc);
		if (qctl->qc_dqinfo.dqi_igrace == NOTIFY_GRACE_TIME)
			strncpy(igtimebuf, NOTIFY_GRACE, 40);
		else
			sec2str(qctl->qc_dqinfo.dqi_igrace, igtimebuf, rc);

		printf("Block grace time: %s; Inode grace time: %s\n",
			   bgtimebuf, igtimebuf);
	}
}

static struct project_quota_resp arena_print_quota(char *mnt, struct if_quotactl *qctl, int type,
												   int rc, bool h, bool show_default)
{
	struct project_quota_resp pqr;
	time_t now;

	time(&now);

	if (qctl->qc_cmd == LUSTRE_Q_GETQUOTA || qctl->qc_cmd == Q_GETOQUOTA ||
		qctl->qc_cmd == LUSTRE_Q_GETQUOTAPOOL ||
		qctl->qc_cmd == LUSTRE_Q_GETDEFAULT)
	{
		int bover = 0, iover = 0;
		struct obd_dqblk *dqb = &qctl->qc_dqblk;
		char numbuf[3][STRBUF_LEN];
		char timebuf[40];
		char strbuf[STRBUF_LEN];

		if (dqb->dqb_bhardlimit &&
			lustre_stoqb(dqb->dqb_curspace) >= dqb->dqb_bhardlimit)
		{
			bover = 1;
		}
		else if (dqb->dqb_bsoftlimit && dqb->dqb_btime)
		{
			if (dqb->dqb_btime > now)
			{
				bover = 2;
			}
			else
			{
				bover = 3;
			}
		}

		if (dqb->dqb_ihardlimit &&
			dqb->dqb_curinodes >= dqb->dqb_ihardlimit)
		{
			iover = 1;
		}
		else if (dqb->dqb_isoftlimit && dqb->dqb_itime)
		{
			if (dqb->dqb_itime > now)
			{
				iover = 2;
			}
			else
			{
				iover = 3;
			}
		}

		if (strlen(mnt) > 15)
			printf("%s\n%15s", mnt, "");
		else
			printf("%15s", mnt);

		if (bover)
			diff2str(dqb->dqb_btime, timebuf, now);
		else if (show_default)
			snprintf(timebuf, sizeof(timebuf), "%llu",
					 (unsigned long long)dqb->dqb_btime);

		kbytes2str(lustre_stoqb(dqb->dqb_curspace),
				   strbuf, sizeof(strbuf), h);
		if (rc == -EREMOTEIO)
			sprintf(numbuf[0], "%s*", strbuf);
		else
			sprintf(numbuf[0], (dqb->dqb_valid & QIF_SPACE) ? "%s" : "[%s]", strbuf);

		kbytes2str(dqb->dqb_bsoftlimit, strbuf, sizeof(strbuf), h);
		if (type == QC_GENERAL)
			sprintf(numbuf[1], (dqb->dqb_valid & QIF_BLIMITS) ? "%s" : "[%s]", strbuf);
		else
			sprintf(numbuf[1], "%s", "-");

		kbytes2str(dqb->dqb_bhardlimit, strbuf, sizeof(strbuf), h);
		sprintf(numbuf[2], (dqb->dqb_valid & QIF_BLIMITS) ? "%s" : "[%s]", strbuf);

		if (show_default)
		{
			printf(" %6s %7s %7s", numbuf[1], numbuf[2], timebuf);
		}
		else
		{
			printf(" %7s%c %6s %7s %7s",
				   numbuf[0], bover ? '*' : ' ', numbuf[1],
				   numbuf[2], bover > 1 ? timebuf : "-");
			// pqr.block_used = numbuf[0];
			// pqr.block_soft_limit = numbuf[1];
			// pqr.block_hard_limit = numbuf[2];
			strcpy(pqr.block_used, numbuf[0]);
			strcpy(pqr.block_soft_limit, numbuf[1]);
			strcpy(pqr.block_hard_limit, numbuf[2]);
		}

		if (iover)
			diff2str(dqb->dqb_itime, timebuf, now);
		else if (show_default)
			snprintf(timebuf, sizeof(timebuf), "%llu",
					 (unsigned long long)dqb->dqb_itime);

		snprintf(numbuf[0], sizeof(numbuf),
				 (dqb->dqb_valid & QIF_INODES) ? "%ju" : "[%ju]",
				 (uintmax_t)dqb->dqb_curinodes);

		if (type == QC_GENERAL)
			sprintf(numbuf[1], (dqb->dqb_valid & QIF_ILIMITS) ? "%ju" : "[%ju]",
					(uintmax_t)dqb->dqb_isoftlimit);
		else
			sprintf(numbuf[1], "%s", "-");

		sprintf(numbuf[2], (dqb->dqb_valid & QIF_ILIMITS) ? "%ju" : "[%ju]", (uintmax_t)dqb->dqb_ihardlimit);

		if (show_default)
		{
			printf(" %6s %7s %7s", numbuf[1], numbuf[2], timebuf);
		}
		else if (type != QC_OSTIDX)
		{
			printf(" %7s%c %6s %7s %7s",
				   numbuf[0], iover ? '*' : ' ', numbuf[1],
				   numbuf[2], iover > 1 ? timebuf : "-");
			strcpy(pqr.inode_used, numbuf[0]);
			strcpy(pqr.inode_soft_limit, numbuf[1]);
			strcpy(pqr.inode_hard_limit, numbuf[2]);
		}
		else
			printf(" %7s %7s %7s %7s", "-", "-", "-", "-");
		printf("\n");
	}
	else if (qctl->qc_cmd == LUSTRE_Q_GETINFO || LUSTRE_Q_GETINFOPOOL ||
			 qctl->qc_cmd == Q_GETOINFO)
	{
		char bgtimebuf[40];
		char igtimebuf[40];

		if (qctl->qc_dqinfo.dqi_bgrace == NOTIFY_GRACE_TIME)
			strncpy(bgtimebuf, NOTIFY_GRACE, 40);
		else
			sec2str(qctl->qc_dqinfo.dqi_bgrace, bgtimebuf, rc);
		if (qctl->qc_dqinfo.dqi_igrace == NOTIFY_GRACE_TIME)
			strncpy(igtimebuf, NOTIFY_GRACE, 40);
		else
			sec2str(qctl->qc_dqinfo.dqi_igrace, igtimebuf, rc);

		printf("Block grace time: %s; Inode grace time: %s\n",
			   bgtimebuf, igtimebuf);
	}

	return pqr;
}

static int print_obd_quota(char *mnt, struct if_quotactl *qctl, int is_mdt,
						   bool h, __u64 *total)
{
	int rc = 0, rc1 = 0, count = 0;
	__u32 valid = qctl->qc_valid;

	/* TODO: for commands LUSTRE_Q_"S\|G"ETQUOTAPOOL we need
	 * to go only through OSTs that belong to requested pool. */
	rc = llapi_get_obd_count(mnt, &count, is_mdt);
	if (rc)
	{
		fprintf(stderr, "can not get %s count: %s\n",
				is_mdt ? "mdt" : "ost", strerror(-rc));
		return rc;
	}

	for (qctl->qc_idx = 0; qctl->qc_idx < count; qctl->qc_idx++)
	{
		qctl->qc_valid = is_mdt ? QC_MDTIDX : QC_OSTIDX;
		rc = llapi_quotactl(mnt, qctl);
		if (rc)
		{
			/* It is remote client case. */
			if (rc == -EOPNOTSUPP)
			{
				rc = 0;
				goto out;
			}

			if (!rc1)
				rc1 = rc;
			fprintf(stderr, "quotactl %s%d failed.\n",
					is_mdt ? "mdt" : "ost", qctl->qc_idx);
			continue;
		}

		print_quota(obd_uuid2str(&qctl->obd_uuid), qctl,
					qctl->qc_valid, 0, h, false);
		*total += is_mdt ? qctl->qc_dqblk.dqb_ihardlimit : qctl->qc_dqblk.dqb_bhardlimit;
	}
out:
	qctl->qc_valid = valid;
	return rc ?: rc1;
}

static int get_print_quota(char *mnt, char *name, struct if_quotactl *qctl,
						   int verbose, int quiet, bool human_readable,
						   bool show_default)
{
	int rc1 = 0, rc2 = 0, rc3 = 0;
	char *obd_type = (char *)qctl->obd_type;
	char *obd_uuid = (char *)qctl->obd_uuid.uuid;
	__u64 total_ialloc = 0, total_balloc = 0;
	bool use_default_for_blk = false;
	bool use_default_for_file = false;
	int inacc;

	rc1 = llapi_quotactl(mnt, qctl);
	if (rc1 < 0)
	{
		switch (rc1)
		{
		case -ESRCH:
			fprintf(stderr, "%s quotas are not enabled.\n",
					qtype_name(qctl->qc_type));
			goto out;
		case -EPERM:
			fprintf(stderr, "Permission denied.\n");
		case -ENODEV:
		case -ENOENT:
			/* We already got error message. */
			goto out;
		default:
			fprintf(stderr, "Unexpected quotactl error: %s\n",
					strerror(-rc1));
		}
	}

	if (!show_default && qctl->qc_id == 0)
	{
		qctl->qc_dqblk.dqb_bhardlimit = 0;
		qctl->qc_dqblk.dqb_bsoftlimit = 0;
		qctl->qc_dqblk.dqb_ihardlimit = 0;
		qctl->qc_dqblk.dqb_isoftlimit = 0;
		qctl->qc_dqblk.dqb_btime = 0;
		qctl->qc_dqblk.dqb_itime = 0;
		qctl->qc_dqblk.dqb_valid |= QIF_LIMITS | QIF_TIMES;
	}

	if (qctl->qc_dqblk.dqb_valid & QIF_BTIME &&
		LQUOTA_FLAG(qctl->qc_dqblk.dqb_btime) & LQUOTA_FLAG_DEFAULT)
	{
		use_default_for_blk = true;
		qctl->qc_dqblk.dqb_btime &= LQUOTA_GRACE_MASK;
	}

	if (qctl->qc_dqblk.dqb_valid & QIF_ITIME &&
		LQUOTA_FLAG(qctl->qc_dqblk.dqb_itime) & LQUOTA_FLAG_DEFAULT)
	{
		use_default_for_file = true;
		qctl->qc_dqblk.dqb_itime &= LQUOTA_GRACE_MASK;
	}

	if ((qctl->qc_cmd == LUSTRE_Q_GETQUOTA ||
		 qctl->qc_cmd == LUSTRE_Q_GETQUOTAPOOL ||
		 qctl->qc_cmd == LUSTRE_Q_GETDEFAULT) &&
		!quiet)
		print_quota_title(name, qctl, human_readable, show_default);

	if (rc1 && *obd_type)
		fprintf(stderr, "%s %s ", obd_type, obd_uuid);

	if (qctl->qc_valid != QC_GENERAL)
		mnt = "";

	inacc = (qctl->qc_cmd == LUSTRE_Q_GETQUOTA ||
			 qctl->qc_cmd == LUSTRE_Q_GETQUOTAPOOL) &&
			((qctl->qc_dqblk.dqb_valid & (QIF_LIMITS | QIF_USAGE)) !=
			 (QIF_LIMITS | QIF_USAGE));

	print_quota(mnt, qctl, QC_GENERAL, rc1, human_readable, show_default);

	if (!show_default && verbose &&
		qctl->qc_valid == QC_GENERAL && qctl->qc_cmd != LUSTRE_Q_GETINFO &&
		qctl->qc_cmd != LUSTRE_Q_GETINFOPOOL)
	{
		char strbuf[STRBUF_LEN];

		rc2 = print_obd_quota(mnt, qctl, 1, human_readable,
							  &total_ialloc);
		rc3 = print_obd_quota(mnt, qctl, 0, human_readable,
							  &total_balloc);
		kbytes2str(total_balloc, strbuf, sizeof(strbuf),
				   human_readable);
		printf("Total allocated inode limit: %ju, total "
			   "allocated block limit: %s\n",
			   (uintmax_t)total_ialloc,
			   strbuf);
	}

	if (use_default_for_blk)
		printf("%cid %u is using default block quota setting\n",
			   *qtype_name(qctl->qc_type), qctl->qc_id);

	if (use_default_for_file)
		printf("%cid %u is using default file quota setting\n",
			   *qtype_name(qctl->qc_type), qctl->qc_id);

	if (rc1 || rc2 || rc3 || inacc)
		printf("Some errors happened when getting quota info. "
			   "Some devices may be not working or deactivated. "
			   "The data in \"[]\" is inaccurate.\n");
out:
	if (rc1)
		return rc1;
	if (rc2)
		return rc2;
	if (rc3)
		return rc3;
	if (inacc)
		return -EIO;

	return 0;
}

static int arena_get_print_quota(char *mnt, char *name, struct if_quotactl *qctl,
								 int verbose, int quiet, bool human_readable,
								 bool show_default, struct project_quota_resp *pqrp)
{
	int rc1 = 0, rc2 = 0, rc3 = 0;
	char *obd_type = (char *)qctl->obd_type;
	char *obd_uuid = (char *)qctl->obd_uuid.uuid;
	__u64 total_ialloc = 0, total_balloc = 0;
	bool use_default_for_blk = false;
	bool use_default_for_file = false;
	int inacc;

	struct project_quota_resp pqr;

	rc1 = llapi_quotactl(mnt, qctl);
	if (rc1 < 0)
	{
		switch (rc1)
		{
		case -ESRCH:
			fprintf(stderr, "%s quotas are not enabled.\n",
					qtype_name(qctl->qc_type));
			goto out;
		case -EPERM:
			fprintf(stderr, "Permission denied.\n");
		case -ENODEV:
		case -ENOENT:
			/* We already got error message. */
			goto out;
		default:
			fprintf(stderr, "Unexpected quotactl error: %s\n",
					strerror(-rc1));
		}
	}

	if (!show_default && qctl->qc_id == 0)
	{
		qctl->qc_dqblk.dqb_bhardlimit = 0;
		qctl->qc_dqblk.dqb_bsoftlimit = 0;
		qctl->qc_dqblk.dqb_ihardlimit = 0;
		qctl->qc_dqblk.dqb_isoftlimit = 0;
		qctl->qc_dqblk.dqb_btime = 0;
		qctl->qc_dqblk.dqb_itime = 0;
		qctl->qc_dqblk.dqb_valid |= QIF_LIMITS | QIF_TIMES;
	}

	if (qctl->qc_dqblk.dqb_valid & QIF_BTIME &&
		LQUOTA_FLAG(qctl->qc_dqblk.dqb_btime) & LQUOTA_FLAG_DEFAULT)
	{
		use_default_for_blk = true;
		qctl->qc_dqblk.dqb_btime &= LQUOTA_GRACE_MASK;
	}

	if (qctl->qc_dqblk.dqb_valid & QIF_ITIME &&
		LQUOTA_FLAG(qctl->qc_dqblk.dqb_itime) & LQUOTA_FLAG_DEFAULT)
	{
		use_default_for_file = true;
		qctl->qc_dqblk.dqb_itime &= LQUOTA_GRACE_MASK;
	}

	if ((qctl->qc_cmd == LUSTRE_Q_GETQUOTA ||
		 qctl->qc_cmd == LUSTRE_Q_GETQUOTAPOOL ||
		 qctl->qc_cmd == LUSTRE_Q_GETDEFAULT) &&
		!quiet)
		print_quota_title(name, qctl, human_readable, show_default);

	if (rc1 && *obd_type)
		fprintf(stderr, "%s %s ", obd_type, obd_uuid);

	if (qctl->qc_valid != QC_GENERAL)
		mnt = "";

	inacc = (qctl->qc_cmd == LUSTRE_Q_GETQUOTA ||
			 qctl->qc_cmd == LUSTRE_Q_GETQUOTAPOOL) &&
			((qctl->qc_dqblk.dqb_valid & (QIF_LIMITS | QIF_USAGE)) !=
			 (QIF_LIMITS | QIF_USAGE));

	// print_quota(mnt, qctl, QC_GENERAL, rc1, human_readable, show_default);
	pqr = arena_print_quota(mnt, qctl, QC_GENERAL, rc1, human_readable, show_default);
	printf("block_used: %s \n", pqr.block_used);
	printf("block_soft_limit: %s \n", pqr.block_soft_limit);
	printf("block_hard_limit: %s \n", pqr.block_hard_limit);
	printf("inode_used: %s \n", pqr.inode_used);
	printf("inode_soft_limit: %s \n", pqr.inode_soft_limit);
	printf("inode_hard_limit: %s \n", pqr.inode_hard_limit);

	// pqrp = &pqr;
	*pqrp = pqr;

	if (!show_default && verbose &&
		qctl->qc_valid == QC_GENERAL && qctl->qc_cmd != LUSTRE_Q_GETINFO &&
		qctl->qc_cmd != LUSTRE_Q_GETINFOPOOL)
	{
		char strbuf[STRBUF_LEN];

		rc2 = print_obd_quota(mnt, qctl, 1, human_readable,
							  &total_ialloc);
		rc3 = print_obd_quota(mnt, qctl, 0, human_readable,
							  &total_balloc);
		kbytes2str(total_balloc, strbuf, sizeof(strbuf),
				   human_readable);
		printf("Total allocated inode limit: %ju, total "
			   "allocated block limit: %s\n",
			   (uintmax_t)total_ialloc,
			   strbuf);
	}

	if (use_default_for_blk)
		printf("%cid %u is using default block quota setting\n",
			   *qtype_name(qctl->qc_type), qctl->qc_id);

	if (use_default_for_file)
		printf("%cid %u is using default file quota setting\n",
			   *qtype_name(qctl->qc_type), qctl->qc_id);

	if (rc1 || rc2 || rc3 || inacc)
		printf("Some errors happened when getting quota info. "
			   "Some devices may be not working or deactivated. "
			   "The data in \"[]\" is inaccurate.\n");
out:
	if (rc1)
		return rc1;
	if (rc2)
		return rc2;
	if (rc3)
		return rc3;
	if (inacc)
		return -EIO;

	return 0;
}

static int lfs_quota(int argc, char **argv)
{
	struct project_quota_resp pqr;
	int c;
	char *mnt, *name = NULL;
	struct if_quotactl *qctl;
	char *obd_uuid;
	int rc = 0, rc1 = 0, verbose = 0, quiet = 0;
	char *endptr;
	__u32 valid = QC_GENERAL, idx = 0;
	bool human_readable = false;
	bool show_default = false;
	int qtype;
	struct option long_opts[] = {
		{.name = "pool", .has_arg = required_argument, .val = 1},
		{.name = NULL}};

	qctl = calloc(1, sizeof(*qctl) + LOV_MAXPOOLNAME + 1);
	if (!qctl)
		return -ENOMEM;

	qctl->qc_cmd = LUSTRE_Q_GETQUOTA;
	qctl->qc_type = ALLQUOTA;
	obd_uuid = (char *)qctl->obd_uuid.uuid;

	while ((c = getopt_long(argc, argv, "gGi:I:o:pPqtuUvh",
							long_opts, NULL)) != -1)
	{
		switch (c)
		{
		case 'U':
			show_default = true;
		case 'u':
			qtype = USRQUOTA;
			goto quota_type;
		case 'G':
			show_default = true;
		case 'g':
			qtype = GRPQUOTA;
			goto quota_type;
		case 'P':
			show_default = true;
		case 'p':
			qtype = PRJQUOTA;
		quota_type:
			if (qctl->qc_type != ALLQUOTA)
			{
				fprintf(stderr,
						"%s quota: only one of -u, -g, or -p may be specified\n",
						progname);
				rc = CMD_HELP;
				goto out;
			}
			qctl->qc_type = qtype;
			break;
		case 't':
			qctl->qc_cmd = LUSTRE_Q_GETINFO;
			break;
		case 'o':
			valid = qctl->qc_valid = QC_UUID;
			snprintf(obd_uuid, sizeof(*obd_uuid), "%s", optarg);
			break;
		case 'i':
			valid = qctl->qc_valid = QC_MDTIDX;
			idx = qctl->qc_idx = atoi(optarg);
			if (idx == 0 && *optarg != '0')
			{
				fprintf(stderr,
						"%s quota: invalid MDT index '%s'\n",
						progname, optarg);
				rc = CMD_HELP;
				goto out;
			}
			break;
		case 'I':
			valid = qctl->qc_valid = QC_OSTIDX;
			idx = qctl->qc_idx = atoi(optarg);
			if (idx == 0 && *optarg != '0')
			{
				fprintf(stderr,
						"%s quota: invalid OST index '%s'\n",
						progname, optarg);
				rc = CMD_HELP;
				goto out;
			}
			break;
		case 'v':
			verbose = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'h':
			human_readable = true;
			break;
		// case 1:
		// 	if (lfs_verify_poolarg(optarg)) {
		// 		rc = -1;
		// 		goto out;
		// 	}
		// 	strncpy(qctl->qc_poolname, optarg, LOV_MAXPOOLNAME);
		// 	qctl->qc_cmd = qctl->qc_cmd == LUSTRE_Q_GETINFO ?
		// 				LUSTRE_Q_GETINFOPOOL :
		// 				LUSTRE_Q_GETQUOTAPOOL;
		// 	break;
		default:
			fprintf(stderr, "%s quota: unrecognized option '%s'\n",
					progname, argv[optind - 1]);
			rc = CMD_HELP;
			goto out;
		}
	}

	/* current uid/gid info for "lfs quota /path/to/lustre/mount" */
	if ((qctl->qc_cmd == LUSTRE_Q_GETQUOTA ||
		 qctl->qc_cmd == LUSTRE_Q_GETQUOTAPOOL) &&
		qctl->qc_type == ALLQUOTA &&
		optind == argc - 1 && !show_default)
	{

		qctl->qc_idx = idx;

		for (qtype = USRQUOTA; qtype <= GRPQUOTA; qtype++)
		{
			qctl->qc_type = qtype;
			qctl->qc_valid = valid;
			if (qtype == USRQUOTA)
			{
				qctl->qc_id = geteuid();
				rc = uid2name(&name, qctl->qc_id);
			}
			else
			{
				qctl->qc_id = getegid();
				rc = gid2name(&name, qctl->qc_id);
				memset(&qctl->qc_dqblk, 0,
					   sizeof(qctl->qc_dqblk));
			}
			if (rc)
				name = "<unknown>";
			mnt = argv[optind];
			rc1 = get_print_quota(mnt, name, qctl, verbose, quiet,
								  human_readable, show_default);
			if (rc1 && !rc)
				rc = rc1;
		}
		goto out;
		/* lfs quota -u username /path/to/lustre/mount */
	}
	else if (qctl->qc_cmd == LUSTRE_Q_GETQUOTA ||
			 qctl->qc_cmd == LUSTRE_Q_GETQUOTAPOOL)
	{
		/* options should be followed by u/g-name and mntpoint */
		if ((!show_default && optind + 2 != argc) ||
			(show_default && optind + 1 != argc) ||
			qctl->qc_type == ALLQUOTA)
		{
			fprintf(stderr,
					"%s quota: name and mount point must be specified\n",
					progname);
			rc = CMD_HELP;
			goto out;
		}

		if (!show_default)
		{
			name = argv[optind++];
			switch (qctl->qc_type)
			{
			case USRQUOTA:
				rc = name2uid(&qctl->qc_id, name);
				break;
			case GRPQUOTA:
				rc = name2gid(&qctl->qc_id, name);
				break;
			case PRJQUOTA:
				rc = name2projid(&qctl->qc_id, name);
				break;
			default:
				rc = -ENOTSUP;
				break;
			}
		}
		else
		{
			qctl->qc_valid = QC_GENERAL;
			qctl->qc_cmd = LUSTRE_Q_GETDEFAULT;
			qctl->qc_id = 0;
		}

		if (rc)
		{
			qctl->qc_id = strtoul(name, &endptr, 10);
			if (*endptr != '\0')
			{
				fprintf(stderr, "%s quota: invalid id '%s'\n",
						progname, name);
				rc = CMD_HELP;
				goto out;
			}
		}
	}
	else if (optind + 1 != argc || qctl->qc_type == ALLQUOTA)
	{
		fprintf(stderr, "%s quota: missing quota info argument(s)\n",
				progname);
		rc = CMD_HELP;
		goto out;
	}

	mnt = argv[optind];
	// rc = get_print_quota(mnt, name, qctl, verbose, quiet,human_readable, show_default);
	rc = arena_get_print_quota(mnt, name, qctl, verbose, quiet, human_readable, show_default, &pqr);
out:
	free(qctl);
	return rc;
}

static int arena_lfs_quota(int argc, char **argv, struct project_quota_resp *pqr)
{
	int c;
	char *mnt, *name = NULL;
	struct if_quotactl *qctl;
	char *obd_uuid;
	int rc = 0, rc1 = 0, verbose = 0, quiet = 0;
	char *endptr;
	__u32 valid = QC_GENERAL, idx = 0;
	bool human_readable = false;
	bool show_default = false;
	int qtype;
	struct option long_opts[] = {
		{.name = "pool", .has_arg = required_argument, .flag = NULL, .val = 1},
		{.name = NULL, .has_arg = 0, .flag = NULL, .val = 0}};

	qctl = calloc(1, sizeof(*qctl) + LOV_MAXPOOLNAME + 1);
	if (!qctl)
		return -ENOMEM;

	qctl->qc_cmd = LUSTRE_Q_GETQUOTA;
	qctl->qc_type = ALLQUOTA;
	obd_uuid = (char *)qctl->obd_uuid.uuid;
	optind = 0;

	while ((c = getopt_long(argc, argv, "gGi:I:o:p:PqtuUvh",
							long_opts, NULL)) != -1)
	{
		switch (c)
		{
		case 'U':
			show_default = true;
		case 'u':
			qtype = USRQUOTA;
			goto quota_type;
		case 'G':
			show_default = true;
		case 'g':
			qtype = GRPQUOTA;
			goto quota_type;
		case 'P':
			show_default = true;
		case 'p':
			qtype = PRJQUOTA;
		quota_type:
			if (qctl->qc_type != ALLQUOTA)
			{
				fprintf(stderr,
						"%s quota: only one of -u, -g, or -p may be specified\n",
						progname);
				rc = CMD_HELP;
				goto out;
			}
			qctl->qc_type = qtype;
			break;
		case 't':
			qctl->qc_cmd = LUSTRE_Q_GETINFO;
			break;
		case 'o':
			valid = qctl->qc_valid = QC_UUID;
			snprintf(obd_uuid, sizeof(*obd_uuid), "%s", optarg);
			break;
		case 'i':
			valid = qctl->qc_valid = QC_MDTIDX;
			idx = qctl->qc_idx = atoi(optarg);
			if (idx == 0 && *optarg != '0')
			{
				fprintf(stderr,
						"%s quota: invalid MDT index '%s'\n",
						progname, optarg);
				rc = CMD_HELP;
				goto out;
			}
			break;
		case 'I':
			valid = qctl->qc_valid = QC_OSTIDX;
			idx = qctl->qc_idx = atoi(optarg);
			if (idx == 0 && *optarg != '0')
			{
				fprintf(stderr,
						"%s quota: invalid OST index '%s'\n",
						progname, optarg);
				rc = CMD_HELP;
				goto out;
			}
			break;
		case 'v':
			verbose = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'h':
			human_readable = true;
			break;
		// case 1:
		// 	if (lfs_verify_poolarg(optarg)) {
		// 		rc = -1;
		// 		goto out;
		// 	}
		// 	strncpy(qctl->qc_poolname, optarg, LOV_MAXPOOLNAME);
		// 	qctl->qc_cmd = qctl->qc_cmd == LUSTRE_Q_GETINFO ?
		// 				LUSTRE_Q_GETINFOPOOL :
		// 				LUSTRE_Q_GETQUOTAPOOL;
		// 	break;
		default:
			fprintf(stderr, "%s quota: unrecognized option '%s'\n",
					progname, argv[optind - 1]);
			rc = CMD_HELP;
			goto out;
		}
	}

	/* current uid/gid info for "lfs quota /path/to/lustre/mount" */
	if ((qctl->qc_cmd == LUSTRE_Q_GETQUOTA ||
		 qctl->qc_cmd == LUSTRE_Q_GETQUOTAPOOL) &&
		qctl->qc_type == ALLQUOTA &&
		optind == argc - 1 && !show_default)
	{

		qctl->qc_idx = idx;

		for (qtype = USRQUOTA; qtype <= GRPQUOTA; qtype++)
		{
			qctl->qc_type = qtype;
			qctl->qc_valid = valid;
			if (qtype == USRQUOTA)
			{
				qctl->qc_id = geteuid();
				rc = uid2name(&name, qctl->qc_id);
			}
			else
			{
				qctl->qc_id = getegid();
				rc = gid2name(&name, qctl->qc_id);
				memset(&qctl->qc_dqblk, 0,
					   sizeof(qctl->qc_dqblk));
			}
			if (rc)
				name = "<unknown>";
			mnt = argv[optind];
			rc1 = get_print_quota(mnt, name, qctl, verbose, quiet,
								  human_readable, show_default);
			if (rc1 && !rc)
				rc = rc1;
		}
		goto out;
		/* lfs quota -u username /path/to/lustre/mount */
	}
	else if (qctl->qc_cmd == LUSTRE_Q_GETQUOTA ||
			 qctl->qc_cmd == LUSTRE_Q_GETQUOTAPOOL)
	{
		/* options should be followed by u/g-name and mntpoint */
		if ((!show_default && optind + 2 != argc) ||
			(show_default && optind + 1 != argc) ||
			qctl->qc_type == ALLQUOTA)
		{
			fprintf(stderr,
					"%s quota: name and mount point must be specified\n",
					progname);
			rc = CMD_HELP;
			goto out;
		}

		if (!show_default)
		{
			name = argv[optind++];
			switch (qctl->qc_type)
			{
			case USRQUOTA:
				rc = name2uid(&qctl->qc_id, name);
				break;
			case GRPQUOTA:
				rc = name2gid(&qctl->qc_id, name);
				break;
			case PRJQUOTA:
				rc = name2projid(&qctl->qc_id, name);
				break;
			default:
				rc = -ENOTSUP;
				break;
			}
		}
		else
		{
			qctl->qc_valid = QC_GENERAL;
			qctl->qc_cmd = LUSTRE_Q_GETDEFAULT;
			qctl->qc_id = 0;
		}

		if (rc)
		{
			qctl->qc_id = strtoul(name, &endptr, 10);
			if (*endptr != '\0')
			{
				fprintf(stderr, "%s quota: invalid id '%s'\n",
						progname, name);
				rc = CMD_HELP;
				goto out;
			}
		}
	}
	else if (optind + 1 != argc || qctl->qc_type == ALLQUOTA)
	{
		fprintf(stderr, "%s quota: missing quota info argument(s)\n",
				progname);
		rc = CMD_HELP;
		goto out;
	}

	mnt = argv[optind];
	// rc = get_print_quota(mnt, name, qctl, verbose, quiet,human_readable, show_default);
	struct project_quota_resp xxx;
	rc = arena_get_print_quota(mnt, name, qctl, verbose, quiet, human_readable, show_default, &xxx);

	// printf("xxblock_used: %s \n", xxx.block_used);
	// printf("block_soft_limit: %s \n", xxx.block_soft_limit);
	// printf("block_hard_limit: %s \n", xxx.block_hard_limit);
	// printf("inode_used: %s \n", xxx.inode_used);
	// printf("inode_soft_limit: %s \n", xxx.inode_soft_limit);
	// printf("inode_hard_limit: %s \n", xxx.inode_hard_limit);

	*pqr = xxx;

out:
	free(qctl);
	return rc;
}

//   _________       __  ________                __
//  /   _____/ _____/  |_\_____  \  __ __  _____/  |______
//  \_____  \_/ __ \   __\/  / \  \|  |  \/  _ \   __\__  \  
//  /        \  ___/|  | /   \_/.  \  |  (  <_> )  |  / __ \_
// /_______  /\___  >__| \_____\ \_/____/ \____/|__| (____  /
//         \/     \/            \__>                      \/

#define ARG2INT(nr, str, msg)                    \
	do                                           \
	{                                            \
		char *endp;                              \
		nr = strtol(str, &endp, 0);              \
		if (*endp != '\0')                       \
		{                                        \
			fprintf(stderr, "%s: bad %s '%s'\n", \
					progname, msg, str);         \
			return CMD_HELP;                     \
		}                                        \
	} while (0)

#define ADD_OVERFLOW(a, b) ((a + b) < a) ? (a = ULONG_MAX) : (a = a + b)

/* Convert format time string "XXwXXdXXhXXmXXs" into seconds value
 * returns the value or ULONG_MAX on integer overflow or incorrect format
 * Notes:
 *        1. the order of specifiers is arbitrary (may be: 5w3s or 3s5w)
 *        2. specifiers may be encountered multiple times (2s3s is 5 seconds)
 *        3. empty integer value is interpreted as 0
 */
static unsigned long str2sec(const char *timestr)
{
	const char spec[] = "smhdw";
	const unsigned long mult[] = {1, 60, 60 * 60, 24 * 60 * 60, 7 * 24 * 60 * 60};
	unsigned long val = 0;
	char *tail;

	if (strpbrk(timestr, spec) == NULL)
	{
		/* no specifiers inside the time string,
                   should treat it as an integer value */
		val = strtoul(timestr, &tail, 10);
		return *tail ? ULONG_MAX : val;
	}

	/* format string is XXwXXdXXhXXmXXs */
	while (*timestr)
	{
		unsigned long v;
		int ind;
		char *ptr;

		v = strtoul(timestr, &tail, 10);
		if (v == ULONG_MAX || *tail == '\0')
			/* value too large (ULONG_MAX or more)
                           or missing specifier */
			goto error;

		ptr = strchr(spec, *tail);
		if (ptr == NULL)
			/* unknown specifier */
			goto error;

		ind = ptr - spec;

		/* check if product will overflow the type */
		if (!(v < ULONG_MAX / mult[ind]))
			goto error;

		ADD_OVERFLOW(val, mult[ind] * v);
		if (val == ULONG_MAX)
			goto error;

		timestr = tail + 1;
	}

	return val;

error:
	return ULONG_MAX;
}

#define ARG2ULL(nr, str, def_units)                     \
	do                                                  \
	{                                                   \
		unsigned long long limit, units = def_units;    \
		int rc;                                         \
                                                        \
		rc = llapi_parse_size(str, &limit, &units, 1);  \
		if (rc < 0)                                     \
		{                                               \
			fprintf(stderr, "%s: invalid limit '%s'\n", \
					progname, str);                     \
			return CMD_HELP;                            \
		}                                               \
		nr = limit;                                     \
	} while (0)

static inline int has_times_option(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++)
		if (!strcmp(argv[i], "-t"))
			return 1;

	return 0;
}

static inline int lfs_verify_poolarg(char *pool)
{
	if (strnlen(optarg, LOV_MAXPOOLNAME + 1) > LOV_MAXPOOLNAME)
	{
		fprintf(stderr,
				"Pool name '%.*s' is longer than %d\n",
				LOV_MAXPOOLNAME, pool, LOV_MAXPOOLNAME);
		return 1;
	}
	return 0;
}

static int lfs_setquota_times(int argc, char **argv, struct if_quotactl *qctl)
{
	int c, rc;
	char *mnt, *obd_type = (char *)qctl->obd_type;
	struct obd_dqblk *dqb = &qctl->qc_dqblk;
	struct obd_dqinfo *dqi = &qctl->qc_dqinfo;
	struct option long_opts[] = {
		{.name = "block-grace", .has_arg = required_argument, .val = 'b'},
		{.name = "group", .has_arg = no_argument, .val = 'g'},
		{.name = "inode-grace", .has_arg = required_argument, .val = 'i'},
		{.name = "projid", .has_arg = no_argument, .val = 'p'},
		{.name = "times", .has_arg = no_argument, .val = 't'},
		{.name = "user", .has_arg = no_argument, .val = 'u'},
		{.name = "pool", .has_arg = required_argument, .val = 'o'},
		{.name = NULL}};
	int qtype;

	qctl->qc_cmd = LUSTRE_Q_SETINFO;
	qctl->qc_type = ALLQUOTA;

	while ((c = getopt_long(argc, argv, "b:gi:ptuo:",
							long_opts, NULL)) != -1)
	{
		switch (c)
		{
		case 'u':
			qtype = USRQUOTA;
			goto quota_type;
		case 'g':
			qtype = GRPQUOTA;
			goto quota_type;
		case 'p':
			qtype = PRJQUOTA;
		quota_type:
			if (qctl->qc_type != ALLQUOTA)
			{
				fprintf(stderr, "error: -u/g/p can't be used "
								"more than once\n");
				return CMD_HELP;
			}
			qctl->qc_type = qtype;
			break;
		case 'b':
			if (strncmp(optarg, NOTIFY_GRACE,
						strlen(NOTIFY_GRACE)) == 0)
			{
				dqi->dqi_bgrace = NOTIFY_GRACE_TIME;
			}
			else
			{
				dqi->dqi_bgrace = str2sec(optarg);
				if (dqi->dqi_bgrace >= NOTIFY_GRACE_TIME)
				{
					fprintf(stderr, "error: bad "
									"block-grace: %s\n",
							optarg);
					return CMD_HELP;
				}
			}
			dqb->dqb_valid |= QIF_BTIME;
			break;
		case 'i':
			if (strncmp(optarg, NOTIFY_GRACE,
						strlen(NOTIFY_GRACE)) == 0)
			{
				dqi->dqi_igrace = NOTIFY_GRACE_TIME;
			}
			else
			{
				dqi->dqi_igrace = str2sec(optarg);
				if (dqi->dqi_igrace >= NOTIFY_GRACE_TIME)
				{
					fprintf(stderr, "error: bad "
									"inode-grace: %s\n",
							optarg);
					return CMD_HELP;
				}
			}
			dqb->dqb_valid |= QIF_ITIME;
			break;
		case 't': /* Yes, of course! */
			break;
		// case 'o':
		// 	if (lfs_verify_poolarg(optarg))
		// 		return -1;
		// 	fprintf(stdout,
		// 		"Trying to set grace for pool %s\n", optarg);
		// 	strncpy(qctl->qc_poolname, optarg, LOV_MAXPOOLNAME);
		// 	qctl->qc_cmd  = LUSTRE_Q_SETINFOPOOL;
		// 	break;
		/* getopt prints error message for us when opterr != 0 */
		default:
			return CMD_HELP;
		}
	}

	if (qctl->qc_type == ALLQUOTA)
	{
		fprintf(stderr, "error: neither -u, -g nor -p specified\n");
		return CMD_HELP;
	}

	if (optind != argc - 1)
	{
		fprintf(stderr, "error: unexpected parameters encountered\n");
		return CMD_HELP;
	}

	mnt = argv[optind];
	rc = llapi_quotactl(mnt, qctl);
	if (rc)
	{
		if (*obd_type)
			fprintf(stderr, "%s %s ", obd_type,
					obd_uuid2str(&qctl->obd_uuid));
		fprintf(stderr, "setquota failed: %s\n", strerror(-rc));
		return rc;
	}

	return 0;
}

#define BSLIMIT (1 << 0)
#define BHLIMIT (1 << 1)
#define ISLIMIT (1 << 2)
#define IHLIMIT (1 << 3)

static int lfs_setquota(int argc, char **argv)
{
	int c, rc = 0;
	struct if_quotactl *qctl;
	char *mnt, *obd_type;
	struct obd_dqblk *dqb;
	struct option long_opts[] = {
		{.name = "block-softlimit", .has_arg = required_argument, .val = 'b'},
		{.name = "block-hardlimit", .has_arg = required_argument, .val = 'B'},
		{.name = "default", .has_arg = no_argument, .val = 'd'},
		{.name = "group", .has_arg = required_argument, .val = 'g'},
		{.name = "default-grp", .has_arg = no_argument, .val = 'G'},
		{.name = "inode-softlimit", .has_arg = required_argument, .val = 'i'},
		{.name = "inode-hardlimit", .has_arg = required_argument, .val = 'I'},
		{.name = "projid", .has_arg = required_argument, .val = 'p'},
		{.name = "default-prj", .has_arg = no_argument, .val = 'P'},
		{.name = "user", .has_arg = required_argument, .val = 'u'},
		{.name = "default-usr", .has_arg = no_argument, .val = 'U'},
		{.name = "pool", .has_arg = required_argument, .val = 'o'},
		{.name = NULL}};
	unsigned limit_mask = 0;
	char *endptr;
	bool use_default = false;
	int qtype, qctl_len;

	qctl_len = sizeof(*qctl) + LOV_MAXPOOLNAME + 1;
	qctl = malloc(qctl_len);
	if (!qctl)
		return -ENOMEM;

	memset(qctl, 0, qctl_len);
	obd_type = (char *)qctl->obd_type;
	dqb = &qctl->qc_dqblk;

	if (has_times_option(argc, argv))
	{
		rc = lfs_setquota_times(argc, argv, qctl);
		goto out;
	}

	qctl->qc_cmd = LUSTRE_Q_SETQUOTA;
	qctl->qc_type = ALLQUOTA; /* ALLQUOTA makes no sense for setquota,
				  * so it can be used as a marker that qc_type
				  * isn't reinitialized from command line */
	optind = 0;
	while ((c = getopt_long(argc, argv, "b:B:dg:Gi:I:p:Pu:Uo:",
							long_opts, NULL)) != -1)
	{
		switch (c)
		{
		case 'U':
			qctl->qc_cmd = LUSTRE_Q_SETDEFAULT;
			qtype = USRQUOTA;
			qctl->qc_id = 0;
			goto quota_type_def;
		case 'u':
			qtype = USRQUOTA;
			rc = name2uid(&qctl->qc_id, optarg);
			goto quota_type;
		case 'G':
			qctl->qc_cmd = LUSTRE_Q_SETDEFAULT;
			qtype = GRPQUOTA;
			qctl->qc_id = 0;
			goto quota_type_def;
		case 'g':
			qtype = GRPQUOTA;
			rc = name2gid(&qctl->qc_id, optarg);
			goto quota_type;
		case 'P':
			qctl->qc_cmd = LUSTRE_Q_SETDEFAULT;
			qtype = PRJQUOTA;
			qctl->qc_id = 0;
			goto quota_type_def;
		case 'p':
			qtype = PRJQUOTA;
			rc = name2projid(&qctl->qc_id, optarg);
		quota_type:
			if (rc)
			{
				qctl->qc_id = strtoul(optarg, &endptr, 10);
				if (*endptr != '\0')
				{
					fprintf(stderr, "%s setquota: invalid"
									" id '%s'\n",
							progname, optarg);
					rc = -1;
					goto out;
				}
			}

			if (qctl->qc_id == 0)
			{
				fprintf(stderr, "%s setquota: can't set quota"
								" for root usr/group/project.\n",
						progname);
				rc = -1;
				goto out;
			}

		quota_type_def:
			if (qctl->qc_type != ALLQUOTA)
			{
				fprintf(stderr,
						"%s setquota: only one of -u, -U, -g,"
						" -G, -p or -P may be specified\n",
						progname);
				rc = CMD_HELP;
				goto out;
			}
			qctl->qc_type = qtype;
			break;
		case 'd':
			qctl->qc_cmd = LUSTRE_Q_SETDEFAULT;
			use_default = true;
			break;
		case 'b':
			ARG2ULL(dqb->dqb_bsoftlimit, optarg, 1024);
			dqb->dqb_bsoftlimit >>= 10;
			limit_mask |= BSLIMIT;
			if (dqb->dqb_bsoftlimit &&
				dqb->dqb_bsoftlimit <= 1024) /* <= 1M? */
				fprintf(stderr,
						"%s setquota: warning: block softlimit '%llu' smaller than minimum qunit size\n"
						"See '%s help setquota' or Lustre manual for details\n",
						progname,
						(unsigned long long)dqb->dqb_bsoftlimit,
						progname);
			break;
		case 'B':
			ARG2ULL(dqb->dqb_bhardlimit, optarg, 1024);
			dqb->dqb_bhardlimit >>= 10;
			limit_mask |= BHLIMIT;
			if (dqb->dqb_bhardlimit &&
				dqb->dqb_bhardlimit <= 1024) /* <= 1M? */
				fprintf(stderr,
						"%s setquota: warning: block hardlimit '%llu' smaller than minimum qunit size\n"
						"See '%s help setquota' or Lustre manual for details\n",
						progname,
						(unsigned long long)dqb->dqb_bhardlimit,
						progname);
			break;
		case 'i':
			ARG2ULL(dqb->dqb_isoftlimit, optarg, 1);
			limit_mask |= ISLIMIT;
			if (dqb->dqb_isoftlimit &&
				dqb->dqb_isoftlimit <= 1024) /* <= 1K inodes? */
				fprintf(stderr,
						"%s setquota: warning: inode softlimit '%llu' smaller than minimum qunit size\n"
						"See '%s help setquota' or Lustre manual for details\n",
						progname,
						(unsigned long long)dqb->dqb_isoftlimit,
						progname);
			break;
		case 'I':
			ARG2ULL(dqb->dqb_ihardlimit, optarg, 1);
			limit_mask |= IHLIMIT;
			if (dqb->dqb_ihardlimit &&
				dqb->dqb_ihardlimit <= 1024) /* <= 1K inodes? */
				fprintf(stderr,
						"%s setquota: warning: inode hardlimit '%llu' smaller than minimum qunit size\n"
						"See '%s help setquota' or Lustre manual for details\n",
						progname,
						(unsigned long long)dqb->dqb_ihardlimit,
						progname);
			break;
		// case 'o':
		// 	if (lfs_verify_poolarg(optarg)) {
		// 		rc = -1;
		// 		goto out;
		// 	}
		// 	fprintf(stdout,
		// 		"Trying to set quota for pool %s\n", optarg);
		// 	strncpy(qctl->qc_poolname, optarg, LOV_MAXPOOLNAME);
		// 	qctl->qc_cmd  = LUSTRE_Q_SETQUOTAPOOL;
		// 	break;
		default:
			fprintf(stderr,
					"%s setquota: unrecognized option '%s'\n",
					progname, argv[optind - 1]);
			rc = CMD_HELP;
			goto out;
		}
	}

	if (qctl->qc_type == ALLQUOTA)
	{
		fprintf(stderr,
				"%s setquota: either -u or -g must be specified\n",
				progname);
		rc = CMD_HELP;
		goto out;
	}

	if (!use_default && limit_mask == 0)
	{
		fprintf(stderr,
				"%s setquota: at least one limit must be specified\n",
				progname);
		rc = CMD_HELP;
		goto out;
	}

	if (use_default && limit_mask != 0)
	{
		fprintf(stderr,
				"%s setquota: limits should not be specified when"
				" using default quota\n",
				progname);
		rc = CMD_HELP;
		goto out;
	}

	if (use_default && qctl->qc_id == 0)
	{
		fprintf(stderr,
				"%s setquota: can not set default quota for root"
				" user/group/project\n",
				progname);
		rc = CMD_HELP;
		goto out;
	}

	if (optind != argc - 1)
	{
		fprintf(stderr,
				"%s setquota: filesystem not specified or unexpected argument '%s'\n",
				progname, argv[optind]);
		rc = CMD_HELP;
		goto out;
	}

	mnt = argv[optind];

	if (use_default)
	{
		dqb->dqb_bhardlimit = 0;
		dqb->dqb_bsoftlimit = 0;
		dqb->dqb_ihardlimit = 0;
		dqb->dqb_isoftlimit = 0;
		dqb->dqb_itime = 0;
		dqb->dqb_btime = 0;
		dqb->dqb_valid |= QIF_LIMITS | QIF_TIMES;
	}
	else if ((!(limit_mask & BHLIMIT) ^ !(limit_mask & BSLIMIT)) ||
			 (!(limit_mask & IHLIMIT) ^ !(limit_mask & ISLIMIT)))
	{
		/* sigh, we can't just set blimits/ilimits */
		struct if_quotactl tmp_qctl = {.qc_cmd = LUSTRE_Q_GETQUOTA,
									   .qc_type = qctl->qc_type,
									   .qc_id = qctl->qc_id};

		rc = llapi_quotactl(mnt, &tmp_qctl);
		if (rc < 0)
			goto out;

		if (!(limit_mask & BHLIMIT))
			dqb->dqb_bhardlimit = tmp_qctl.qc_dqblk.dqb_bhardlimit;
		if (!(limit_mask & BSLIMIT))
			dqb->dqb_bsoftlimit = tmp_qctl.qc_dqblk.dqb_bsoftlimit;
		if (!(limit_mask & IHLIMIT))
			dqb->dqb_ihardlimit = tmp_qctl.qc_dqblk.dqb_ihardlimit;
		if (!(limit_mask & ISLIMIT))
			dqb->dqb_isoftlimit = tmp_qctl.qc_dqblk.dqb_isoftlimit;

		/* Keep grace times if we have got no softlimit arguments */
		if ((limit_mask & BHLIMIT) && !(limit_mask & BSLIMIT))
		{
			dqb->dqb_valid |= QIF_BTIME;
			dqb->dqb_btime = tmp_qctl.qc_dqblk.dqb_btime;
		}

		if ((limit_mask & IHLIMIT) && !(limit_mask & ISLIMIT))
		{
			dqb->dqb_valid |= QIF_ITIME;
			dqb->dqb_itime = tmp_qctl.qc_dqblk.dqb_itime;
		}
	}

	dqb->dqb_valid |= (limit_mask & (BHLIMIT | BSLIMIT)) ? QIF_BLIMITS : 0;
	dqb->dqb_valid |= (limit_mask & (IHLIMIT | ISLIMIT)) ? QIF_ILIMITS : 0;

	rc = llapi_quotactl(mnt, qctl);
	if (rc)
	{
		if (*obd_type)
			fprintf(stderr,
					"%s setquota: cannot quotactl '%s' '%s': %s",
					progname, obd_type,
					obd_uuid2str(&qctl->obd_uuid), strerror(-rc));
	}
out:
	free(qctl);
	return rc;
}

// __________                   __               __
// \______   \_______  ____    |__| ____   _____/  |_
//  |     ___/\_  __ \/  _ \   |  |/ __ \_/ ___\   __
//  |    |     |  | \(  <_> )  |  \  ___/\  \___|  |
//  |____|     |__|   \____/\__|  |\___  >\___  >__|
//                         \______|    \/     \/

static int lfs_project(int argc, char **argv)
{
	int ret = 0, err = 0, c, i;
	struct project_handle_control phc = {0};
	enum lfs_project_ops_t op;

	phc.newline = true;
	phc.assign_projid = false;
	/* default action */
	op = LFS_PROJECT_LIST;

	optind = 0;
	
	pthread_t thread = pthread_self();
	printf("current thread id: %lld\n", thread);
	
	for (int i = 0; i < argc; ++i)
	{
		printf("index: %d, argv: %s\n", i, argv[i]);
	}

	struct option long_opts[] = {
		{.name = "p", .has_arg = required_argument, .flag = NULL, .val = 'p'},
		{.name = "s", .has_arg = no_argument, .flag = NULL, .val = 's'},
		{.name = "r", .has_arg = no_argument, .flag = NULL, .val = 'r'},
		{.name = NULL, .has_arg = 0, .flag = NULL, .val = 0}};

	while ((c = getopt_long(argc, argv, "p:cCsdkr0",
							long_opts, NULL)) != -1)
	{
		switch (c)
		{
		case 'c':
			if (op != LFS_PROJECT_LIST)
			{
				fprintf(stderr,
						"%s: cannot specify '-c' '-C' '-s' together\n",
						progname);
				return CMD_HELP;
			}

			op = LFS_PROJECT_CHECK;
			break;
		case 'C':
			if (op != LFS_PROJECT_LIST)
			{
				fprintf(stderr,
						"%s: cannot specify '-c' '-C' '-s' together\n",
						progname);
				return CMD_HELP;
			}

			op = LFS_PROJECT_CLEAR;
			break;
		case 's':
			if (op != LFS_PROJECT_LIST)
			{
				fprintf(stderr,
						"%s: cannot specify '-c' '-C' '-s' together\n",
						progname);
				return CMD_HELP;
			}

			phc.set_inherit = true;
			op = LFS_PROJECT_SET;
			break;
		case 'd':
			phc.dironly = true;
			break;
		case 'k':
			phc.keep_projid = true;
			break;
		case 'r':
			phc.recursive = true;
			break;
		case 'p':
			phc.projid = strtoul(optarg, NULL, 0);
			phc.assign_projid = true;

			break;
		case '0':
			phc.newline = false;
			break;
		default:
			fprintf(stderr, "%s: invalid option_custom '%c' opterr: %d; optind: %d\n",
					progname, optopt, opterr, optind);
			return CMD_HELP;
		}
	}

	if (phc.assign_projid && op == LFS_PROJECT_LIST)
	{
		op = LFS_PROJECT_SET;
		phc.set_projid = true;
	}
	else if (phc.assign_projid && op == LFS_PROJECT_SET)
	{
		phc.set_projid = true;
	}

	switch (op)
	{
	case LFS_PROJECT_CHECK:
		if (phc.keep_projid)
		{
			fprintf(stderr,
					"%s: '-k' is useless together with '-c'\n",
					progname);
			return CMD_HELP;
		}
		break;
	case LFS_PROJECT_CLEAR:
		if (!phc.newline)
		{
			fprintf(stderr,
					"%s: '-0' is useless together with '-C'\n",
					progname);
			return CMD_HELP;
		}
		if (phc.assign_projid)
		{
			fprintf(stderr,
					"%s: '-p' is useless together with '-C'\n",
					progname);
			return CMD_HELP;
		}
		break;
	case LFS_PROJECT_SET:
		if (!phc.newline)
		{
			fprintf(stderr,
					"%s: '-0' is useless together with '-s'\n",
					progname);
			return CMD_HELP;
		}
		if (phc.keep_projid)
		{
			fprintf(stderr,
					"%s: '-k' is useless together with '-s'\n",
					progname);
			return CMD_HELP;
		}
		break;
	default:
		if (!phc.newline)
		{
			fprintf(stderr,
					"%s: '-0' is useless for list operations\n",
					progname);
			return CMD_HELP;
		}
		break;
	}

	argv += optind;
	argc -= optind;
	if (argc == 0)
	{
		fprintf(stderr, "%s: missing file or directory target(s)\n",
				progname);
		return CMD_HELP;
	}

	for (i = 0; i < argc; i++)
	{
		switch (op)
		{
		case LFS_PROJECT_CHECK:
			err = lfs_project_check(argv[i], &phc);
			break;
		case LFS_PROJECT_LIST:
			err = lfs_project_list(argv[i], &phc);
			break;
		case LFS_PROJECT_CLEAR:
			err = lfs_project_clear(argv[i], &phc);
			break;
		case LFS_PROJECT_SET:
			err = lfs_project_set(argv[i], &phc);
			break;
		default:
			break;
		}
		if (err && !ret)
			ret = err;
	}

	return ret;
}