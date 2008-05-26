/* vi: set sw=4 ts=4: */
/*
 * depmod - generate modules.dep
 * Copyright (c) 2008 Bernhard Fischer
 *
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 */

#undef _GNU_SOURCE
#define _GNU_SOURCE
#include <libbb.h>
#include <sys/utsname.h> /* uname() */

/*
 * Theory of operation:
 * - iterate over all modules and record their full path
 * - iterate over all modules looking for "depends=" entries
 *   for each depends, look through our list of full paths and emit if found
 */
struct globals {
	llist_t *lst;
};
#define G (*(struct globals*)&bb_common_bufsiz1)
/* We have to zero it out because of NOEXEC */
#define INIT_G() memset(&G, 0, sizeof(G))

static int fill_lst(const char *modulename, struct stat ATTRIBUTE_UNUSED *sb,
					void ATTRIBUTE_UNUSED *data, int ATTRIBUTE_UNUSED depth)
{
	llist_add_to(&G.lst, xstrdup(modulename));
	return TRUE;
}

static int fileAction(const char *fname, struct stat *sb,
					void *data, int ATTRIBUTE_UNUSED depth)
{
	size_t len = sb->st_size;
	void *the_module, *ptr;
	int fd;
	char *depends, *deps;

/*XXX: FIXME: does not handle compressed modules!
 * There should be a function that looks at the extension and sets up
 * open_transformer for us.
 */
	if (last_char_is(fname, 'o') == NULL) /* not a module */
		goto skip;

	fd = xopen(fname, O_RDONLY);
	the_module = mmap(NULL, len, PROT_READ, MAP_SHARED
#if defined MAP_POPULATE
						|MAP_POPULATE
#endif
						, fd, 0);
	close(fd);
	if (the_module == MAP_FAILED)
		bb_perror_msg_and_die("mmap");

	ptr = the_module;

	fprintf((FILE*)data, "\n%s:", fname);
//bb_info_msg("fname='%s'", fname);
	do {
		/* search for a 'd' */
		ptr = memchr(ptr, 'd', len);
		if (ptr == NULL) /* no d left, done */
			goto done;
		if (memcmp(ptr, "depends=", sizeof("depends=")-1) == 0)
			break;
		len -= ++ptr - the_module;
	} while (1);
	deps = depends = xstrdup (ptr + sizeof("depends=")-1);
//bb_info_msg(" depends='%s'", depends);
	while (*deps) {
		llist_t * _lst = G.lst;
		char *buf1;

		ptr = strchr(deps, ',');
		if (ptr != NULL)
			*(char*)ptr = '\0';
		/* remember the length of the current dependency plus eventual 0 byte */
		len = strlen(deps) + (ptr != NULL);
		buf1 = xmalloc(len + 3);
		sprintf(buf1, "/%s.", deps); /* make sure we match the correct file */
		while (_lst) {
			ptr = strstr(_lst->data, buf1);
			if (ptr != NULL)
				break; /* found it */
			_lst = _lst->link;
		}
		free(buf1);
		if (_lst /*&& _lst->data*/) {
//bb_info_msg("[%s] -> '%s'", deps, _lst->data);
			fprintf((FILE*)data, " %s", _lst->data);
			deps += len;
		}
	}
	free(depends);
done:
	munmap(the_module, sb->st_size);
skip:
	return TRUE;
}

int depmod_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int depmod_main(int ATTRIBUTE_UNUSED argc, char **argv)
{
	int retval = EXIT_SUCCESS;
	char *moddir_base, *system_map, *chp;
	FILE *filedes = stdout;
	enum {
		ARG_a = (1<<0), /* All modules, ignore mods in argv */
		ARG_A = (1<<1), /* Only emit .ko that are newer than modules.dep file */
		ARG_b = (1<<2), /* not /lib/modules/$(uname -r)/ but this base-dir */
		ARG_e = (1<<3), /* with -F, print unresolved symbols */
		ARG_F = (1<<4), /* System.map that contains the symbols */
		ARG_n = (1<<5)  /* dry-run, print to stdout only */
	};

	getopt32(argv, "aAb:eF:n", &moddir_base, &system_map);
	argv += optind;
	if (*argv) /* got a version to use? */
		chp = *argv++;
	else {
		struct utsname uts;
		if (uname(&uts) < 0)
			bb_simple_perror_msg_and_die("uname");
		chp = xstrdup(uts.release);
	}
	moddir_base
		= concat_path_file(option_mask32 & ARG_b ? moddir_base : "/lib/modules",
							chp);
	if (ENABLE_FEATURE_CLEAN_UP)
		free(chp);
	if (!(option_mask32 & ARG_n)) {
		char *modules_dep = concat_path_file(moddir_base, "modules.dep");
		filedes = xfopen(modules_dep, "w");
		if (ENABLE_FEATURE_CLEAN_UP)
			free(modules_dep);
	}
	do {
		chp = concat_path_file(moddir_base,
				option_mask32 & ARG_a ? "/" : *argv++);

		if (!recursive_action(chp,
				ACTION_RECURSE, /* flags */
				fill_lst, /* file action */
				NULL, /* dir action */
				NULL, /* user data */
				0) || /* depth */
			!recursive_action(chp,
				ACTION_RECURSE, /* flags */
				fileAction, /* file action */
				NULL, /* dir action */
				(void*)filedes, /* user data */
				0)) { /* depth */
			retval = EXIT_FAILURE;
		}
		if (ENABLE_FEATURE_CLEAN_UP)
			free(chp);
	} while (!(option_mask32 & ARG_a) || *argv);

	if (ENABLE_FEATURE_CLEAN_UP) {
		fclose(filedes);
		llist_free(G.lst, free);
		free(moddir_base);
	}
	return retval;
}