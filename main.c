#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include <ccan/pr_debug/pr_debug.h>
#include <ccan/err/err.h>
#include <ccan/endian/endian.h>

#define __packed __attribute__((__packed__))
#include "hv-24x7-catalog.h"

#define max(x, y) ({		\
	typeof(x) __x = x;	\
	typeof(y) __y = y;	\
	(void)(&__y == &__x);	\
	__x > __y ? __x : __y;	\
	})


/* 2 mappings:
 * - # to name
 * - name to #
 */
enum hv_perf_domains {
#define DOMAIN(n, v) HV_PERF_DOMAIN_##n = v,
#include "hv-24x7-domains.h"
#undef DOMAIN
};

static size_t domain_to_string(enum hv_perf_domains domain, char *buf, size_t buf_len)
{
	size_t l;
	switch (domain) {
#define DOMAIN(n, v)				\
	case HV_PERF_DOMAIN_##n:		\
		l = max(strlen(#n), buf_len);	\
		memcpy(buf, #n, l);		\
		break;
#include "hv-24x7-domains.h"
#undef DOMAIN
		default:
			l = snprintf(buf, buf_len, "unknown[%d]", domain);
	}

	return l;
}


#define _pr_sz(l, s) pr_debug(l, #s " = %zu", s);
#define pr_sz(l, s) _pr_sz(l, sizeof(s))
#define pr_u(v) pr_debug(1, #v " = %u", v);

static void _usage(const char *p, int e)
{
	FILE *o = stderr;
	fprintf(o, "usage: %s <catalog file>\n", p);
	exit(e);
}

#define _PRGM_NAME "parse"
#define PRGM_NAME  (argc?argv[0]:_PRGM_NAME)
#define usage(argc, argv, e) _usage(PRGM_NAME, e)
#define U(e) usage(argc, argv, e)

int main(int argc, char **argv)
{
	err_set_progname(PRGM_NAME);
	pr_sz(9, struct hv_24x7_catalog_page_0);

	if (argc != 2)
		U(0);

	char *file = argv[1];

	pr_debug(5, "filename = %s", file);
	FILE *f = fopen(file, "rb");
	if (!f)
		err(1, "could not open %s", file);

	char buf[4096];
	ssize_t r = fread(buf, 1, sizeof(buf), f);
	if (r != 4096)
		err(1, "could not read page 0, got %zd bytes", r);

	struct hv_24x7_catalog_page_0 *p0 = (void *)buf;

	size_t catalog_page_length = be_to_cpu(p0->length);
	pr_debug(1, "magic  = %*s", (int)sizeof(p0->magic), (char *)&p0->magic);
	pr_debug(1, "length = %zu pages", catalog_page_length);
	pr_debug(1, "build_time_stamp = %*s", (int)sizeof(p0->build_time_stamp), p0->build_time_stamp);

	pr_debug(1, "version = %"PRIu32, be_to_cpu(p0->version));


	unsigned schema_data_offs = be_to_cpu(p0->schema_data_offs);
	unsigned schema_data_len  = be_to_cpu(p0->schema_data_len);
	unsigned schema_entry_count = be_to_cpu(p0->schema_entry_count);

	unsigned group_data_offs = be_to_cpu(p0->group_data_offs);
	unsigned group_data_len  = be_to_cpu(p0->group_data_len);
	unsigned group_entry_count = be_to_cpu(p0->group_entry_count);

	unsigned formula_data_offs = be_to_cpu(p0->formula_data_offs);
	unsigned formula_data_len = be_to_cpu(p0->formula_data_len);
	unsigned formula_entry_count = be_to_cpu(p0->formula_entry_count);

	pr_u(schema_data_offs);
	pr_u(schema_data_len);
	pr_u(schema_entry_count);
	pr_u(group_data_offs);
	pr_u(group_data_len);
	pr_u(group_entry_count);
	pr_u(formula_data_offs);
	pr_u(formula_data_len);
	pr_u(formula_entry_count);

	return 0;
}
