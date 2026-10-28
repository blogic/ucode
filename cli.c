/*
 * Copyright (C) 2020-2021 Jo-Philipp Wich <jo@mein.io>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Command line options shared by the ucode CLI and native programs. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "ucode/lib.h"
#include "ucode/vm.h"
#include "ucode/internal/cli.h"
#include "ucode/internal/json-c-compat.h"

bool
uc_cli_define_file(char *opt, uc_value_t *globals, FILE **stdin_unused)
{
	enum json_tokener_error err = json_tokener_continue;
	char buf[128], *name = NULL, *p;
	struct json_tokener *tok;
	json_object *jso = NULL;
	size_t rlen;
	FILE *fp;

	p = strchr(opt, '=');

	if (p) {
		name = opt;
		*p++ = 0;
	}
	else {
		p = opt;
	}

	if (!strcmp(p, "-")) {
		if (!*stdin_unused) {
			fprintf(stderr, "The stdin can only be read once\n");

			return false;
		}

		fp = *stdin_unused;
		*stdin_unused = NULL;
	}
	else
		fp = fopen(p, "r");

	if (!fp) {
		fprintf(stderr, "Unable to open definition file \"%s\": %s\n",
		        p, strerror(errno));

		return true;
	}

	tok = xjs_new_tokener();

	while (true) {
		rlen = fread(buf, 1, sizeof(buf), fp);

		if (rlen == 0)
			break;

		jso = json_tokener_parse_ex(tok, buf, rlen);
		err = json_tokener_get_error(tok);

		if (err != json_tokener_continue)
			break;
	}

	json_tokener_free(tok);
	fclose(fp);

	if (err != json_tokener_success || !json_object_is_type(jso, json_type_object)) {
		json_object_put(jso);

		fprintf(stderr, "Invalid definition file \"%s\": %s\n",
		        p, (err != json_tokener_success)
		             ? "JSON parse failure" : "Not a valid JSON object");

		return false;
	}

	if (name && *name) {
		ucv_object_add(globals, name, ucv_from_json(NULL, jso));
	}
	else {
		json_object_object_foreach(jso, key, val)
			ucv_object_add(globals, key, ucv_from_json(NULL, val));
	}

	json_object_put(jso);

	return true;
}

bool
uc_cli_define_string(char *opt, uc_value_t *globals)
{
	enum json_tokener_error err;
	struct json_tokener *tok;
	json_object *jso = NULL;
	char *name = NULL, *p;
	bool rv = false;
	size_t len;

	p = strchr(opt, '=');

	if (p) {
		name = opt;
		*p++ = 0;
	}
	else {
		p = opt;
	}

	len = strlen(p);
	tok = xjs_new_tokener();

	/* NB: the len + 1 here is intentional to pass the terminating \0 byte
	 * to the json-c parser. This is required to work-around upstream
	 * issue #681 <https://github.com/json-c/json-c/issues/681> */
	jso = json_tokener_parse_ex(tok, p, len + 1);

	err = json_tokener_get_error(tok);

	/* Treat trailing bytes after a parsed value as error */
	if (err == json_tokener_success && json_tokener_get_parse_end(tok) < len)
		err = json_tokener_error_parse_unexpected;

	json_tokener_free(tok);

	if (err != json_tokener_success) {
		json_object_put(jso);

		if (!name || !*name) {
			fprintf(stderr, "Invalid -D option value \"%s\": %s\n",
			        p, json_tokener_error_desc(err));

			return false;
		}

		ucv_object_add(globals, name, ucv_string_new(p));

		return true;
	}

	if (name && *name) {
		ucv_object_add(globals, name, ucv_from_json(NULL, jso));
		rv = true;
	}
	else if (json_object_is_type(jso, json_type_object)) {
		json_object_object_foreach(jso, key, val)
			ucv_object_add(globals, key, ucv_from_json(NULL, val));
		rv = true;
	}
	else {
		fprintf(stderr, "Invalid -D option value \"%s\": Not a valid JSON object\n", p);
	}

	json_object_put(jso);

	return rv;
}

void
uc_cli_search_path(char *pattern, uc_parse_config_t *config)
{
	size_t len;
	char *p;

	if (strchr(pattern, '*')) {
		uc_search_path_add(&config->module_search_path, pattern);
		return;
	}

	len = strlen(pattern);

	if (!len)
		return;

	while (pattern[len-1] == '/')
		pattern[--len] = 0;

	xasprintf(&p, "%s/*.so", pattern);
	uc_search_path_add(&config->module_search_path, p);
	free(p);

	xasprintf(&p, "%s/*.uc", pattern);
	uc_search_path_add(&config->module_search_path, p);
	free(p);
}

bool
uc_cli_library_load(char *opt, uc_vm_t *vm)
{
	char *name = NULL, *p;
	uc_value_t *lib, *ctx;

	p = strchr(opt, '=');

	if (p) {
		name = opt;
		*p++ = 0;
	}
	else {
		p = opt;
	}

	lib = ucv_string_new(p);
	ctx = uc_vm_invoke(vm, "require", 1, lib);
	ucv_put(lib);

	if (!ctx)
		return vm->exception.type == EXCEPTION_NONE;

	ucv_object_add(uc_vm_scope_get(vm), name ? name : p, ctx);

	return true;
}
