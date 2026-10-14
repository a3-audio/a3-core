// pipewire-jack (1.6.9, still in master) answers jack_client_max_real_time_priority()
// from an uninitialised variable: the client thread utils have no get_rt_range,
// so REAPER reads -6 and runs its worker threads without realtime priority.
// Preloaded into REAPER, this answers with what pipewire-jack intends: one
// below the client limit, five below that for the workers, under the
// PipeWire data loop (83). A3_JACK_RT_PRIORITY overrides the worker value.
#include <stdlib.h>

static int worker_priority(void)
{
    const char *value = getenv("A3_JACK_RT_PRIORITY");
    int priority = value ? atoi(value) : 77;
    return (priority > 0 && priority < 99) ? priority : 77;
}

int jack_client_real_time_priority(void *client)
{
    (void)client;
    return worker_priority();
}

int jack_client_max_real_time_priority(void *client)
{
    (void)client;
    return worker_priority() + 5;
}
