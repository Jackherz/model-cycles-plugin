/* cloudseed: see fxwrap.h. */
#include <string.h>

#define move_audio_fx_init_v2 cyem_cloudseed_init_v2
#include "../cloudseed/cloudseed.c"
#undef move_audio_fx_init_v2
#include "fxwrap.h"

static host_api_v1_t host;
static audio_fx_api_v2_t *api;

static void *create(void) { return api->create_instance(NULL, "{}"); }
static void destroy(void *p) { api->destroy_instance(p); }
static void set(void *p, const char *k, const char *v) { api->set_param(p, k, v); }
static void process(void *p, float *io, int frames) { cyem_process_float(p, io, frames); }

const cyem_fx *cyem_fx_cloudseed(void)
{
    static const cyem_fx fx = {create, destroy, set, process};
    if (!api) {   /* first use: the processor's constructor, on the message thread */
        memset(&host, 0, sizeof host);
        host.api_version = 1;
        host.sample_rate = 48000;
        host.frames_per_block = 32;
        api = cyem_cloudseed_init_v2(&host);
    }
    return &fx;
}
