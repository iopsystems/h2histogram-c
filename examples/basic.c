#include "h2histogram.h"
#include <inttypes.h>
#include <stdio.h>

int main(void) {
    h2_config config;
    h2_hist *recorder = NULL, *report = NULL;
    if (h2_config_init(&config, 7, 64) != H2_OK || h2_create(config, &recorder) != H2_OK)
        return 1;
    if (h2_record(recorder, 1000, 1) != H2_OK || h2_record(recorder, 2000, 9) != H2_OK ||
        h2_convert(recorder, H2_CUMULATIVE, &report) != H2_OK) {
        h2_free(recorder);
        return 1;
    }
    h2_bucket bucket;
    h2_status status = h2_percentile(report, .99, &bucket);
    if (status == H2_OK)
        printf("p99: [%" PRIu64 ", %" PRIu64 "] count=%" PRIu64 "\n", bucket.start, bucket.end,
               bucket.count);
    h2_free(report);
    h2_free(recorder);
    return status == H2_OK ? 0 : 1;
}
