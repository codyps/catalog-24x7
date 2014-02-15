#include <stdio.h>

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

int main(int argc, char **argv)
{
	return 0;
}
