#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdlib.h>
#include <ioall.h>
#include <unistd.h>
#include <gui-fatal.h>
#include "filecopy.h"
#include "crc32.h"

enum {
	PROGRESS_FLAG_NORMAL,
	PROGRESS_FLAG_INIT,
	PROGRESS_FLAG_DONE
};

unsigned long crc32_sum;
int write_all_with_crc(int fd, void *buf, int size) {
	crc32_sum = Crc32_ComputeBuf(crc32_sum, buf, size);
	return write_all(fd, buf, size);
}


char *client_flags;
void do_notify_progress(long long total, int flag)
{
	FILE *progress;
	if (!client_flags[0])
		return;
	progress = fopen(client_flags, "w");
	if (progress < 0)
		return;
	fprintf(progress, "%d %lld %s", getpid(), total,
		flag == PROGRESS_FLAG_DONE ? "DONE" : "BUSY");
	fclose(progress);
}

void notify_progress(int size, int flag)
{
	static long long total = 0;
	static long long prev_total = 0;
	total += size;
	if (total > prev_total + PROGRESS_NOTIFY_DELTA
	    || (flag != PROGRESS_FLAG_NORMAL)) {
		do_notify_progress(total, flag);
		prev_total = total;
	}
}

void write_headers(struct file_header *hdr, char *filename)
{
	if (!write_all_with_crc(1, hdr, sizeof(*hdr))
	    || !write_all_with_crc(1, filename, hdr->namelen))
		exit(1);
}

int single_file_processor(char *filename, struct stat *st)
{
	struct file_header hdr;
	int fd;
	mode_t mode = st->st_mode;

	hdr.namelen = strlen(filename) + 1;
	hdr.mode = mode;
	hdr.atime = st->st_atim.tv_sec;
	hdr.atime_nsec = st->st_atim.tv_nsec;
	hdr.mtime = st->st_mtim.tv_sec;
	hdr.mtime_nsec = st->st_mtim.tv_nsec;

	if (S_ISREG(mode)) {
		int ret;
		fd = open(filename, O_RDONLY);
		if (fd < 0)
			gui_fatal("open %s", filename);
		hdr.filelen = st->st_size;
		write_headers(&hdr, filename);
		ret = copy_file(1, fd, hdr.filelen, &crc32_sum);
		// if COPY_FILE_WRITE_ERROR, hopefully remote will produce a message
		if (ret != COPY_FILE_OK) {
			if (ret != COPY_FILE_WRITE_ERROR)
				gui_fatal("Copying file %s: %s", filename,
					  copy_file_status_to_str(ret));
			else
				exit(1);
		}
		close(fd);
	}
	if (S_ISDIR(mode)) {
		hdr.filelen = 0;
		write_headers(&hdr, filename);
	}
	if (S_ISLNK(mode)) {
		char name[st->st_size + 1];
		if (readlink(filename, name, sizeof(name)) != st->st_size)
			gui_fatal("readlink %s", filename);
		hdr.filelen = st->st_size + 1;
		write_headers(&hdr, filename);
		if (!write_all_with_crc(1, name, st->st_size + 1))
			exit(1);
	}
	return 0;
}

int do_fs_walk(char *file)
{
	char *newfile;
	struct stat st;
	struct dirent *ent;
	DIR *dir;

	if (lstat(file, &st))
		gui_fatal("stat %s", file);
	single_file_processor(file, &st);
	if (!S_ISDIR(st.st_mode))
		return 0;
	dir = opendir(file);
	if (!dir)
		gui_fatal("opendir %s", file);
	while ((ent = readdir(dir))) {
		char *fname = ent->d_name;
		if (!strcmp(fname, ".") || !strcmp(fname, ".."))
			continue;
		asprintf(&newfile, "%s/%s", file, fname);
		do_fs_walk(newfile);
		free(newfile);
	}
	closedir(dir);
	// directory metadata is resent; this makes the code simple,
	// and the atime/mtime is set correctly at the second time
	single_file_processor(file, &st);
	return 0;
}

void send_vmname(char *vmname)
{
	char buf[FILECOPY_VMNAME_SIZE];
	memset(buf, 0, sizeof(buf));
	strncat(buf, vmname, sizeof(buf) - 1);
	if (!write_all(1, buf, sizeof buf))
		exit(1);
}

char *get_item(char *data, char **current, int size)
{
	char *ret;
	if ((unsigned long) *current >= (unsigned long) data + size)
		return NULL;
	ret = *current;
	*current += strlen(ret) + 1;
	return ret;
}

void notify_end_and_wait_for_result()
{
	struct result_header hdr;
	struct file_header end_hdr;

	/* nofity end of transfer */
	memset(&end_hdr, 0, sizeof(end_hdr));
	end_hdr.namelen = 0;
	end_hdr.filelen = 0;
	write_all_with_crc(1, &end_hdr, sizeof(end_hdr));

	/* wait for result */
	if (!read_all(0, &hdr, sizeof(hdr))) {
		exit(1); // hopefully remote has produced error message
	}
	if (hdr.error_code != 0) {
		gui_fatal("Error writing files: %s", strerror(hdr.error_code));
	}
	if (hdr.crc32 != crc32_sum) {
		gui_fatal("File transfer failed: checksum mismatch");
	}
}

void parse_entry(char *data, int datasize)
{
	char *current = data;
	char *vmname, *entry, *sep;
	vmname = get_item(data, &current, datasize);
	client_flags = get_item(data, &current, datasize);
	notify_progress(0, PROGRESS_FLAG_INIT);
	send_vmname(vmname);
	crc32_sum = 0;
	while ((entry = get_item(data, &current, datasize))) {
		do {
			sep = rindex(entry, '/');
			if (!sep)
				gui_fatal
				    ("Internal error: nonabsolute filenames not allowed");
			*sep = 0;
		} while (sep[1] == 0);
		if (entry[0] == 0)
			chdir("/");
		else if (chdir(entry))
			gui_fatal("chdir to %s", entry);
		do_fs_walk(sep + 1);
	}
	notify_end_and_wait_for_result();
	notify_progress(0, PROGRESS_FLAG_DONE);
}

void process_spoolentry(char *entry_name)
{
	char *abs_spool_entry_name;
	int entry_fd;
	struct stat st;
	char *entry;
	int entry_size;
	asprintf(&abs_spool_entry_name, "%s/%s", FILECOPY_SPOOL,
		 entry_name);
	entry_fd = open(abs_spool_entry_name, O_RDONLY);
	unlink(abs_spool_entry_name);
	if (entry_fd < 0 || fstat(entry_fd, &st))
		gui_fatal("bad file copy spool entry");
	entry_size = st.st_size;
	entry = calloc(1, entry_size + 1);
	if (!entry)
		gui_fatal("malloc");
	if (!read_all(entry_fd, entry, entry_size))
		gui_fatal("read filecopy entry");
	close(entry_fd);
	parse_entry(entry, entry_size);
}

void scan_spool(char *name)
{
	struct dirent *ent;
	DIR *dir = opendir(name);
	if (!dir)
		gui_fatal("opendir %s", name);
	while ((ent = readdir(dir))) {
		char *fname = ent->d_name;
		if (fname[0] != '.') {
			process_spoolentry(fname);
			break;
		}
	}
	closedir(dir);
}

int main()
{
	signal(SIGPIPE, SIG_IGN);
	scan_spool(FILECOPY_SPOOL);
	return 0;
}
