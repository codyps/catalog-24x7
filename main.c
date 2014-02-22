#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

#include <ccan/pr_debug/pr_debug.h>
#include <ccan/err/err.h>
#include <ccan/endian/endian.h>

#include <penny/penny.h>
#include <penny/math.h>
#include <penny/print.h>

#define __packed __attribute__((__packed__))
#include "hv-24x7-catalog.h"

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

static char *event_name(struct hv_24x7_event_data *ev, size_t *len)
{
	*len = be_to_cpu(ev->event_name_len) - 2;
	return (char *)ev->remainder;
}

static char *event_desc(struct hv_24x7_event_data *ev, size_t *len)
{
	unsigned nl = be_to_cpu(ev->event_name_len);
	__be16 *desc_len = (__be16 *)(ev->remainder + nl - 2);
	*len = be_to_cpu(*desc_len) - 2;
	return (char *)ev->remainder + nl;
}

static char *event_long_desc(struct hv_24x7_event_data *ev, size_t *len)
{
	unsigned nl = be_to_cpu(ev->event_name_len);
	__be16 *desc_len_ = (__be16 *)(ev->remainder + nl - 2);
	unsigned desc_len = be_to_cpu(*desc_len_);
	__be16 *long_desc_len = (__be16 *)(ev->remainder + nl + desc_len - 2);
	*len = be_to_cpu(*long_desc_len) - 2;
	return (char *)ev->remainder + nl + desc_len;
}


static bool event_fixed_portion_is_within(struct hv_24x7_event_data *ev, void *end)
{
	void *start = ev;
	return (start + offsetof(struct hv_24x7_event_data, remainder)) < end;
}

/*
 * Things we don't check:
 *  - padding for desc, name, and long/detailed desc is required to be '\0' bytes.
 */
static bool event_is_within(struct hv_24x7_event_data *ev, void *end)
{
	unsigned nl = be_to_cpu(ev->event_name_len);
	void *start = ev;
	if (nl < 2) {
		pr_debug(1, "%s: name length too short: %d", __func__, nl);
		return false;
	}

	if (start + nl > end) {
		pr_debug(1, "%s: start=%p + nl=%u > end=%p", __func__, start, nl, end);
		return false;
	}

	__be16 *dl_ = (__be16 *)(ev->remainder + nl - 2);
	if (!IS_ALIGNED((uintptr_t)dl_, 2))
		warnx("desc len not aligned %p", dl_);
	unsigned dl = be_to_cpu(*dl_);
	if (dl < 2) {
		pr_debug(1, "%s: desc len too short: %d", __func__, dl);
		return false;
	}

	if (start + nl + dl > end) {
		pr_debug(1, "%s: (start=%p + nl=%u + dl=%u)=%p > end=%p", __func__, start, nl, dl, start + nl + dl, end);
		return false;
	}

	__be16 *ldl_ = (__be16 *)(ev->remainder + nl + dl - 2);
	if (!IS_ALIGNED((uintptr_t)ldl_, 2))
		warnx("long desc len not aligned %p", ldl_);
	unsigned ldl = be_to_cpu(*ldl_);
	if (ldl < 2) {
		pr_debug(1, "%s: long desc len too short (ldl=%u)", __func__, ldl);
		return false;
	}

	if (start + nl + dl + ldl > end) {
		pr_debug(1, "%s: start=%p + nl=%u + dl=%u + ldl=%u > end=%p", __func__, start, nl, dl, ldl, end);
		return false;
	}

	return true;
}

static void print_event(struct hv_24x7_event_data *event, FILE *o)
{
	size_t name_len, desc_len, long_desc_len;
	char *name, *desc, *long_desc;

	name = event_name(event, &name_len);
	desc = event_desc(event, &desc_len);
	long_desc = event_long_desc(event, &long_desc_len);

	fprintf(o, "event {\n"
		"	.length=%u,\n"
		"	.domain=%u,\n"
		"	.event_group_record_offs=%u,\n"
		"	.event_group_record_len=%u,\n"
		"	.event_counter_offs=%u,\n"
		"	.flags=%"PRIx32",\n"
		"	.primary_group_ix=%u,\n"
		"	.group_count=%u,\n"
		"	.name=\"",
		be_to_cpu(event->length),
		event->domain,
		be_to_cpu(event->event_group_record_offs),
		be_to_cpu(event->event_group_record_len),
		be_to_cpu(event->event_counter_offs),
		be_to_cpu(event->flags),
		be_to_cpu(event->primary_group_ix),
		be_to_cpu(event->group_count));

	print_bytes_as_cstring_(name, name_len, o);

	fprintf(o, "\", /* %zu */\n"
		"	.desc=\"",
		name_len);

	print_bytes_as_cstring_(desc, desc_len, o);

	fprintf(o, "\", /* %zu */\n"
		"	.detailed_desc=\"",
		desc_len);

	print_bytes_as_cstring_(long_desc, long_desc_len, o);

	fprintf(o, "\", /* %zu */\n"
		"}\n",
		long_desc_len);

	if (debug_is(100))
		print_hex_dump_fmt(event, be_to_cpu(event->length), o);
}

static bool group_fixed_portion_is_within(struct hv_24x7_group_data *group, void *end)
{
	void *start = group;
	return (start + sizeof(*group)) < end;
}

static bool group_is_within(struct hv_24x7_group_data *group, void *end)
{
	unsigned nl = be_to_cpu(group->group_name_len);
	void *start = group;
	if (nl < 2) {
		pr_debug(1, "%s: name length too short: %d", __func__, nl);
		return false;
	}

	if (start + nl > end) {
		pr_debug(1, "%s: start=%p + nl=%u > end=%p", __func__, start, nl, end);
		return false;
	}

	unsigned dl = be_to_cpu(*((__be16*)(group->remainder + nl - 2)));
	if (dl < 2) {
		pr_debug(1, "%s: desc len too short: %d", __func__, dl);
		return false;
	}

	if (start + nl + dl > end) {
		pr_debug(1, "%s: (start=%p + nl=%u + dl=%u)=%p > end=%p", __func__, start, nl, dl, start + nl + dl, end);
		return false;
	}

	return true;
}

static char *group_name(struct hv_24x7_group_data *group, size_t *len)
{
	*len = be_to_cpu(group->group_name_len) - 2;
	return (char *)group->remainder;
}

static char *group_desc(struct hv_24x7_group_data *group, size_t *len)
{
	unsigned nl = be_to_cpu(group->group_name_len);
	__be16 *desc_len = (__be16 *)(group->remainder + nl - 2);
	*len = be_to_cpu(*desc_len) - 2;
	return (char *)group->remainder + nl;
}

static void print_group(struct hv_24x7_group_data *group, FILE *o)
{
	size_t name_len, desc_len;
	char *name, *desc;

	name = group_name(group, &name_len);
	desc = group_desc(group, &desc_len);

	fprintf(o, "group {\n"
		"	.length=%u,\n"
		"	.flags=%"PRIx32",\n"
		"	.domain=%u,\n"
		"	.event_group_record_offs=%u,\n"
		"	.event_group_record_len=%u,\n"
		"	.group_schema_index=%u,\n"
		"	.event_count=%u,\n"
		"	.event_indexes={%u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u},\n"
		"	.name=\"",
		be_to_cpu(group->length),
		be_to_cpu(group->flags),
		be_to_cpu(group->domain),
		be_to_cpu(group->event_group_record_offs),
		be_to_cpu(group->event_group_record_len),
		be_to_cpu(group->group_schema_ix),
		be_to_cpu(group->event_count),
		be_to_cpu(group->event_ixs[0]),
		be_to_cpu(group->event_ixs[1]),
		be_to_cpu(group->event_ixs[2]),
		be_to_cpu(group->event_ixs[3]),
		be_to_cpu(group->event_ixs[4]),
		be_to_cpu(group->event_ixs[5]),
		be_to_cpu(group->event_ixs[6]),
		be_to_cpu(group->event_ixs[7]),
		be_to_cpu(group->event_ixs[8]),
		be_to_cpu(group->event_ixs[9]),
		be_to_cpu(group->event_ixs[10]),
		be_to_cpu(group->event_ixs[11]),
		be_to_cpu(group->event_ixs[12]),
		be_to_cpu(group->event_ixs[13]),
		be_to_cpu(group->event_ixs[14]),
		be_to_cpu(group->event_ixs[15]));

	print_bytes_as_cstring_(name, name_len, o);

	fprintf(o , "\", /* %zu */\n"
		"	.desc=\"", name_len);

	print_bytes_as_cstring_(desc, desc_len, o);

	fprintf(o, "\", /* %zu */\n"
		"}\n", desc_len);
}

static bool schema_fixed_portion_is_within(struct hv_24x7_grs *schema, void *end)
{
	void *start = schema;
	return (start + sizeof(*schema)) < end;
}

static bool schema_is_within(struct hv_24x7_grs *schema, void *end)
{
	unsigned field_entry_count = be_to_cpu(schema->field_entry_count);
	void *start = schema;
	if (!field_entry_count) {
		pr_debug(1, "%s: no field entries", __func__);
		return false;
	}

	size_t field_entry_bytes = field_entry_count * sizeof(schema->field_entrys[0]);

	if (start + field_entry_bytes > end) {
		pr_debug(1, "%s: start=%p + field_entry_bytes=%zu > end=%p", __func__, start, field_entry_bytes, end);
		return false;
	}

	return true;
}

static void print_schema_field_entry(struct hv_24x7_grs_field *field, FILE *o)
{
	fprintf(o, "		{\n"
		"			.enum = %u,\n"
		"			.offs = %u,\n"
		"			.length = %u,\n"
		"			.flags = 0x%X,\n"
		"		}\n",
		be_to_cpu(field->field_enum),
		be_to_cpu(field->offs),
		be_to_cpu(field->length),
		be_to_cpu(field->flags));
}

static void print_schema(struct hv_24x7_grs *schema, FILE *o)
{
	size_t length = be_to_cpu(schema->length);
	size_t field_entry_count = be_to_cpu(schema->field_entry_count);

	fprintf(o, "schema {\n"
		"	.length = %zu,\n"
		"	.descriptor = %u,\n"
		"	.version_id = %u,\n"
		"	.field_entry_count = %zu,\n"
		"	.field_entries = {\n",
		length,
		be_to_cpu(schema->descriptor),
		be_to_cpu(schema->version_id),
		field_entry_count);

	struct hv_24x7_grs_field *field = (void *)schema->field_entrys;
	size_t i = 0;
	for (;;) {
		size_t offset = (void *)schema - (void *)field;
		if (offset >= length)
			break;

		if (i >= field_entry_count) {
			warnx("more space than field entries: %zu >= %zu", i, field_entry_count);
			break;
		}

		print_schema_field_entry(field, o);

		field++;
		i ++;
	}

	if (i != field_entry_count)
		warnx("schema ended before listed # of fields were parsed (got %zu, wanted %zu)", i, field_entry_count);

	fprintf(o, "}\n");
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

	pr_debug(1, "version = %"PRIu64, be_to_cpu(p0->version));


	unsigned schema_data_offs = be_to_cpu(p0->schema_data_offs);
	unsigned schema_data_len  = be_to_cpu(p0->schema_data_len);
	unsigned schema_entry_count = be_to_cpu(p0->schema_entry_count);

	unsigned event_data_offs = be_to_cpu(p0->event_data_offs);
	unsigned event_data_len  = be_to_cpu(p0->event_data_len);
	unsigned event_entry_count = be_to_cpu(p0->event_entry_count);

	unsigned group_data_offs = be_to_cpu(p0->group_data_offs);
	unsigned group_data_len  = be_to_cpu(p0->group_data_len);
	unsigned group_entry_count = be_to_cpu(p0->group_entry_count);

	unsigned formula_data_offs = be_to_cpu(p0->formula_data_offs);
	unsigned formula_data_len = be_to_cpu(p0->formula_data_len);
	unsigned formula_entry_count = be_to_cpu(p0->formula_entry_count);

	pr_u(schema_data_offs);
	pr_u(schema_data_len);
	pr_u(schema_entry_count);
	pr_u(event_data_offs);
	pr_u(event_data_len);
	pr_u(event_entry_count);
	pr_u(group_data_offs);
	pr_u(group_data_len);
	pr_u(group_entry_count);
	pr_u(formula_data_offs);
	pr_u(formula_data_len);
	pr_u(formula_entry_count);

	/*
	 * schema
	 */
	size_t schema_data_bytes = schema_data_len * 4096;
	void *schema_data = malloc(schema_data_bytes);
	if (!schema_data)
		err(1, "alloc failure %zu", schema_data_bytes);
	if (fseek(f, 4096 * schema_data_offs, SEEK_SET))
		err(2, "seek failure");
	if (fread(schema_data, 1, schema_data_bytes, f) != schema_data_bytes)
		err(3, "read failure");

	struct hv_24x7_grs *schema = schema_data;
	void *end = schema_data + schema_data_bytes;
	size_t i;
	for (i = 0; ; i++) {
		if (!schema_fixed_portion_is_within(schema, end)) {
			warnx("schema fixed portion is not within range");
			break;
		}

		size_t offset = (void *)schema - (void *)schema_data;
		if (offset >= schema_data_bytes)
			break;

		if (i >= schema_entry_count) {
			/* Padding follows the last schema, this is expected */
			pr_debug(2, "schema count ends before buffer end (offset=%zu, bytes remaining=%zu)\n",
					offset, schema_data_bytes - offset);
			break;
		}

		if (!schema_fixed_portion_is_within(schema, end)) {
			warnx("schema fixed portion is not within range");
			break;
		}

		size_t schema_len = be_to_cpu(schema->length);
		printf("/* schema %zu of %u: len=%zu offset=%zu */\n", i, schema_entry_count, schema_len, offset);

		if (!IS_ALIGNED(schema_len, 16))
			printf("/* missaligned */\n");

		void *schema_end = (__u8 *)schema + schema_len;
		if (schema_end > end) {
			warnx("schema ends after schema data: schema_end=%p > end=%p", schema_end, end);
			break;
		}

		if (!schema_is_within(schema, end)) {
			warnx("schema exceeds schema data length schema=%p end=%p", schema, end);
			break;
		}

		if (!schema_is_within(schema, schema_end)) {
			warnx("schema exceeds it's own length schema=%p end=%p", schema, schema_end);
			break;
		}

		print_schema(schema, stdout);

		schema = (void *)schema + schema_len;
	}


	/*
	 * events
	 */
	size_t event_data_bytes = event_data_len * 4096;
	void *event_data = malloc(event_data_bytes);
	if (!event_data)
		err(1, "alloc failure %zu", event_data_bytes);
	if (fseek(f, 4096 * event_data_offs, SEEK_SET))
		err(2, "seek failure");
	if (fread(event_data, 1, event_data_bytes, f) != event_data_bytes)
		err(3, "read failure");

	struct hv_24x7_event_data *event = event_data;
	end = event_data + event_data_bytes;
	for (i = 0; ; i++) {
		size_t offset = (void *)event - (void *)event_data;
		if (offset >= event_data_bytes)
			break;

		if (i >= event_entry_count) {
			/* XXX: we have padding following the last event. Completely expected. */
			pr_debug(2, "event count ends before buffer end (offset=%zu, end=%zu bytes remaining=%zu)\n",
					offset, event_data_bytes, event_data_bytes - offset);
			break;
		}

		if (!event_fixed_portion_is_within(event, end)) {
			warnx("event fixed portion is not within range");
			break;
		}

		size_t ev_len = be_to_cpu(event->length);
		printf("/* event %zu of %u: len=%zu offset=%zu */\n", i, event_entry_count, ev_len, offset);

		if (!IS_ALIGNED(ev_len, 16))
			printf("/* missaligned */\n");

		void *ev_end = (__u8 *)event + ev_len;
		if (ev_end > end) {
			warnx("event ends after event data: ev_end=%p > end=%p", ev_end, end);
			break;
		}

		if (!event_is_within(event, end)) {
			warnx("event exceeds event data length event=%p end=%p", event, end);
			break;
		}

		if (!event_is_within(event, ev_end)) {
			warnx("event exceeds it's own length event=%p end=%p", event, ev_end);
			break;
		}


		print_event(event, stdout);

		event = (void *)event + ev_len;
	}

	if (i != event_entry_count)
		warnx("event buffer ended before listed # of events were parsed (got %zu, wanted %u)", i, event_entry_count);

	/*
	 * groups
	 */
	size_t group_data_bytes = group_data_len * 4096;
	void *group_data = malloc(group_data_len);
	if (!group_data)
		err(1, "alloc failure %zu", group_data_bytes);
	if (fseek(f, 4096 * group_data_offs, SEEK_SET))
		err(2, "seek failure");
	if (fread(group_data, 1, group_data_bytes, f) != group_data_bytes)
		err(3, "read failure");

	struct hv_24x7_group_data *group = group_data;
	end = group_data + group_data_bytes;
	for (i = 0; ; i++) {
		if (!group_fixed_portion_is_within(group, end)) {
			warnx("group fixed portion is not within range");
			break;
		}

		size_t offset = (void *)group - (void *)group_data;
		if (offset >= group_data_bytes)
			break;

		if (i >= group_entry_count) {
			/* Padding follows the last group, this is expected */
			pr_debug(2, "group count ends before buffer end (offset=%zu, bytes remaining=%zu)\n",
					offset, group_data_bytes - offset);
			break;
		}

		if (!group_fixed_portion_is_within(group, end)) {
			warnx("group fixed portion is not within range");
			break;
		}

		size_t group_len = be_to_cpu(group->length);
		printf("/* group %zu of %u: len=%zu offset=%zu */\n", i, group_entry_count, group_len, offset);

		if (!IS_ALIGNED(group_len, 16))
			printf("/* missaligned */\n");

		void *group_end = (__u8 *)group + group_len;
		if (group_end > end) {
			warnx("group ends after group data: group_end=%p > end=%p", group_end, end);
			break;
		}

		if (!group_is_within(group, end)) {
			warnx("group exceeds group data length group=%p end=%p", group, end);
			break;
		}

		if (!group_is_within(group, group_end)) {
			warnx("group exceeds it's own length group=%p end=%p", group, group_end);
			break;
		}

		print_group(group, stdout);

		group = (void *)group + group_len;
	}

	/* TODO: for each formula */

	return 0;
}
