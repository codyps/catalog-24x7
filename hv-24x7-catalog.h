#ifndef LINUX_POWERPC_PERF_HV_24X7_H_
#define LINUX_POWERPC_PERF_HV_24X7_H_

#include <linux/types.h>

/* From document "24x7 Event and Group Catalog Formats Proposal" v0.14 */

struct hv_24x7_catalog_page_0 {
#define HV_24X7_CATALOG_MAGIC 0x32347837 /* "24x7" in ASCII */
	__be32 magic;
	__be32 length; /* In 4096 byte pages */
	__u8 reserved1[4];
	__be32 version; /* XXX: arbitrary? what's the meaning/useage/purpose? */
	__u8 build_time_stamp[16]; /* "YYYYMMDDHHMMSS\0\0" */
	__u8 reserved2[32];
	__be16 schema_data_offs; /* in 4096 byte pages */
	__be16 schema_data_len;  /* in 4096 byte pages */
	__be16 schema_entry_count;
	__u8 reserved3[2];
	__be16 group_data_offs; /* in 4096 byte pages */
	__be16 group_data_len;  /* in 4096 byte pages */
	__be16 group_entry_count;
	__u8 reserved4[2];
	__be16 formula_data_offs; /* in 4096 byte pages */
	__be16 formula_data_len;  /* in 4096 byte pages */
	__be16 formula_entry_count;
	__u8 reserved5[2];
} __packed;

struct hv_24x7_event_data {
	__be16 length; /* in bytes, must be a multiple of 16 */
	__u8 reserved1[2];
	__u8 domain; /* Chip = 1, Core = 2 */
	__u8 reserved2[1];
	__be16 event_group_record_offs; /* in bytes, must be 8 byte aligned */
	__be16 event_group_record_len; /* in bytes */

	/* in bytes, offset from event_group_record */
	__be16 event_counter_offs;

	/* verified_state, unverified_state, caveat_state, broken_state, ... */
	__be32 flags;

	__be16 primary_group_ix;
	__be16 group_count;
	__be16 event_name_len;
	__u8 remainder[];
	/* __u8 event_name[event_name_len - 2]; */
	/* __be16 event_description_len; */
	/* __u8 event_desc[event_description_len - 2]; */
	/* __be16 detailed_desc_len; */
	/* __u8 detailed_desc[detailed_desc_len - 2]; */
} __packed;

struct hv_24x7_group_data {
	__be16 length; /* in bytes, must be multiple of 16 */
	__u8 reserved1[2];
	__be32 flags; /* undefined contents */
	__u8 domain; /* Chip = 1, Core = 2 */
	__u8 reserved2[1];
	__be16 event_group_record_offs;
	__be16 event_group_record_len;
	__u8 group_schema_ix;
	__u8 event_count; /* 1 to 16 */
	__be16 event_ixs;
	__be16 group_name_len;
	__u8 remainder[];
	/* __u8 group_name[group_name_len]; */
	/* __be16 group_desc_len; */
	/* __u8 group_desc[group_desc_len]; */
} __packed;

/* TODO: Schema Data */
/* TODO: Event Counter Group Record (see the PORE/SLW workbook) */

/* "Get Event Counter Group Record Schema hypervisor interface" */

enum hv_24x7_grs_field_enums {
	/* GRS_COUNTER_1 = 1
	 * GRS_COUNTER_2 = 2
	 * ...
	 * GRS_COUNTER_31 = 32 // FIXME: Doc issue.
	 */
	GRS_COUNTER_BASE = 1,
	GRS_COUNTER_LAST = 32,
	GRS_TIMEBASE_UPDATE = 48,
	GRS_TIMEBASE_FENCE = 49,
	GRS_UPDATE_COUNT = 50,
	GRS_MEASUREMENT_PERIOD = 51,
	GRS_ACCUMULATED_MEASUREMENT_PERIOD = 52,
	GRS_LAST_UPDATE_PERIOD = 53,
	GRS_STATUS_FLAGS = 54,
};

enum hv_24x7_grs_enums {
	GRS_CORE_SCHEMA_INDEX = 0,
};

struct hv_24x7_grs_field {
	__be16 field_enum;
	__be16 offs; /* in bytes, within Event Counter group record */
	__be16 length; /* in bytes */
	__be16 flags; /* presently unused */
} __packed;

struct hv_24x7_grs {
	__be16 length;
	__u8 reserved1[2];
	__be16 descriptor;
	__be16 version_id;
	__u8 reserved2[6];
	__be16 field_entry_count;
	__u8 field_entrys[];
} __packed;

struct hv_24x7_formula_data {
	__be32 length; /* in bytes, must be multiple of 16 */
	__u8 reserved1[2];
	__be32 flags; /* not yet defined */
	__be16 group;
	__u8 reserved2[6];
	__be16 name_len;
	__u8 remainder[];
	/* __u8 name[name_len]; */
	/* __be16 desc_len; */
	/* __u8 desc[name_len]; */
	/* __be16 formula_len */
	/* __u8 formula[formula_len]; */
} __packed;

/* Formula Syntax: ie, impliment a forth interpereter. */
/* need fast lookup of the formula names, event names, "delta-timebase",
 * "delta-cycles", "delta-instructions", "delta-seconds" */
/* operators: '+', '-', '*', '/', 'mod', 'rem', 'sqr', 'x^y' (XXX: pow? xor?),
 *            'rot', 'dup' */

#endif
