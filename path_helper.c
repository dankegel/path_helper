/*
 * Copyright (c) 2008 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

#include <fts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

static void usage() {
	fprintf(stderr, "usage: path_helper [-c | -s]");
	exit(1);
}

// Append path segment if it does not exist.  Reallocate
// the path buffer as necessary.

int append_path_segment(char** path, const char* segment) {
	if (*path == NULL || segment == NULL) return -1;

	size_t pathlen = strlen(*path);
	size_t seglen = strlen(segment);

	if (seglen == 0) return 0;

	// Does the segment already exist in the path?
	// (^|:)segment(:|$)
	char* match = strstr(*path, segment);
	while (match) {
		if ((match == *path || match[-1] == ':') &&
			(match[seglen] == ':' || match[seglen] == 0)) {
			return 0;
		}
		match = strstr(match+1, segment);
	}
	
	// size = pathlen + ':' + segment + '\0'
	size_t size = pathlen + seglen + 2;
	*path = reallocf(*path, size);
	if (*path == NULL) return -1;

	if (pathlen > 0) strlcat(*path, ":", size);
	strlcat(*path, segment, size);
	return 0;
}

// Convert fgetln output into a sanitized segment
// escape quotes, dollars, etc.
char* read_segment(const char* line, size_t len) {
	int escapes = 0;
	size_t i, j;
	
	for (i = 0; i < len; ++i) {
		char c = line[i];
		if (c == '\"' || c == '\'' || c == '$') {
			++escapes;
		}
	}

	size_t size = len + escapes + 1;

	char* segment = calloc(1, size);
	if (segment == NULL) return NULL;
	
	for (i = 0, j = 0; i < len; ++i, ++j) {
		char c = line[i];
		if (c == '\"' || c == '\'' || c == '$') {
			segment[j++] = '\\';
			segment[j] = c;
		} else if (c == '\n') {
			segment[j] = 0;
			break;
		} else {
			segment[j] = line[i];
		}
	}

	return segment;
}

// Append segments from given file.

void append_path_file(char** result, const char *prefix, const char* fname) {
	FILE* f;

	int len = strlen(prefix) + strlen(fname) + 2;
	char *buf = malloc(len);
	sprintf(buf, "%s/%s", prefix, fname);

	f = fopen(buf, "r");
	if (f == NULL) {
		perror(buf);
		free(buf);
		return;
	}
	free(buf);

	for (;;) {
		size_t len;
		char* line = fgetln(f, &len);
		if (line == NULL) break;
		char* segment = read_segment(line, len);

		append_path_segment(result, segment);
	}

	fclose(f);
}

// Trivial datastructure for sorting a list of strings
struct strlist_s {
	int n;
	int size;
	char **strs;
};

void strlist_init(struct strlist_s *d)
{
	d->n = 0;
	d->size = 100;
	d->strs = malloc(d->size * sizeof(char *));
}

void strlist_append(struct strlist_s *d, const char *s)
{
	if (d->n == d->size) {
		d->size *= 2;
		d->strs = realloc(d->strs, d->size * sizeof(char *));
	}
	d->strs[d->n++] = strdup(s);
}

int strlist_orderfn(const void *a, const void *b) {
	return strcasecmp(*(char **)a, *(char **)b);
}

void strlist_sort(struct strlist_s *d)
{
	qsort(d->strs, d->n, sizeof(char *), strlist_orderfn);
}

// Construct a path variable, starting with the contents
// of the given environment variable, adding the contents
// of the default file and files in the path directory.

char* construct_path(char* env_var, char* defaults_path, char* dir_path) {
	FTS* fts;
	FTSENT* ent;

	char* result = calloc(sizeof(char), 1);

	char* dirpathv[] = { dir_path, NULL };
	fts = fts_open(dirpathv, FTS_PHYSICAL | FTS_XDEV, NULL);
	if (!fts) {
		perror(dir_path);
		return NULL;
	}

	struct strlist_s files;
	strlist_init(&files);
	while ((ent = fts_read(fts)) != NULL) {
		// only interested in regular files, one level deep
		if (ent->fts_info != FTS_F) {
			if (ent->fts_level >= 1) fts_set(fts, ent, FTS_SKIP);
			continue;
		}
		strlist_append(&files, ent->fts_accpath);
	}
	fts_close(fts);
	
	// .d directories, by convention, are processed in alphabetical order.
	strlist_sort(&files);

	append_path_file(&result, "", defaults_path);
	int i;
	for (i=0; i<files.n; i++)
		append_path_file(&result, dir_path, files.strs[i]);

	// merge in any existing custom PATH elemenets
	char* str = getenv(env_var);
	if (str) str = strdup(str);
	while (str) {
		char* sep = strchr(str, ':');
		if (sep) *sep = 0;
		
		append_path_segment(&result, str);
		if (sep) {
			str = sep + 1;
		} else {
			str = NULL;
		}
	}
	
	return result;
}

enum {
	STYLE_CSH,
	STYLE_SH
};

int main(int argc, char* argv[]) {
	int style = STYLE_SH;

	if (argc > 2) usage();
	
	// default to csh style, if $SHELL ends with "csh".
	char* shell = getenv("SHELL");
	if (shell) {
		char* str = strstr(shell, "csh");
		if (str) style = STYLE_CSH;
	}
	
	if (argc == 2 && strcmp(argv[1], "-c") == 0) style = STYLE_CSH;
	if (argc == 2 && strcmp(argv[1], "-s") == 0) style = STYLE_SH;

	char* path = construct_path("PATH", "/etc/paths", "/etc/paths.d");
	char* manpath = NULL;

	// only adjust manpath if already set
	int do_manpath = (getenv("MANPATH") != NULL);
	if (do_manpath) {
		manpath = construct_path("MANPATH", "/etc/manpaths", "/etc/manpaths.d");
	}

	if (style == STYLE_CSH) {
		printf("setenv PATH \"%s\";\n", path);
		if (do_manpath) printf("setenv MANPATH \"%s\";\n", manpath);
	} else {
		printf("PATH=\"%s\"; export PATH;\n", path);
		if (do_manpath) printf("MANPATH=\"%s\"; export MANPATH;\n", manpath);
	}

	return 0;
}
