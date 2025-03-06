/*
 * pattern_iter.c
 *
 * TODO header
 */

#include <string.h>
#include "bsp_dbg.h"

// This module implements:
#include "restim.h"

enum { EL_0, EL_A, EL_B, EL_C = 4, EL_AC = (EL_A | EL_C), EL_D = 8, EL_BD = (EL_B | EL_D) };


static void AddBurst(Restim *me, uint32_t start_time_µs, uint8_t elcon, 
    uint16_t pace_µs, uint16_t pulse_width_¼µs, uint16_t nr_of_pulses, uint16_t burst_width_µs, bool phase)
{
    Burst b = {
        .start_time_µs = start_time_µs,
        .elcon = {0, elcon},
        .pace_µs = pace_µs,
        .nr_of_pulses = nr_of_pulses,
        .pulse_width_¼µs = pulse_width_¼µs,
        .phase = phase ? 1 : 0,
        .amplitude = 0,
        .flags = 0,
    };
    // Burst_print(&b);
    me->bursts_storage[me->nr_of_bursts++] = b;
}

// static void AddBurst_2(Restim *me, uint32_t start_time_µs, uint8_t elcon, uint16_t burst_width_µs, uint16_t pulse_width_µs, bool phase)
// {
//     M_ASSERT(me->nr_of_bursts <= sizeof me->bursts_storage / sizeof *me->bursts_storage);

//     {
//         Burst b = {
//             .start_time_µs = start_time_µs,
//             .elcon = {0, elcon},
//             .pace_µs = 0,
//             .nr_of_pulses = 1,
//             .pulse_width_¼µs = pulse_width_µs * 4,
//             .phase = phase ? 1 : 0,
//             .amplitude = 0,
//             .flags = 0,
//         };
//         Burst_print(&b);
//         me->bursts_storage[me->nr_of_bursts++] = b;
//     }
//     {
//         Burst b = {
//             .start_time_µs = start_time_µs + burst_width_µs - pulse_width_µs,
//             .elcon = {0, elcon},
//             .pace_µs = 0,
//             .nr_of_pulses = 1,
//             .pulse_width_¼µs = pulse_width_µs * 4,
//             .phase = phase ? 1 : 0,
//             .amplitude = 0,
//             .flags = 0,
//         };
//         Burst_print(&b);
//         me->bursts_storage[me->nr_of_bursts++] = b;
//     }
// }

static void AddBurstGroup(Restim *me, uint32_t *t, uint8_t elcon, uint16_t power, bool polarity)
{
    if (power == 0) {
        return;
    }

    uint32_t burst_width_µs = me->params.burst_width_µs;
    uint32_t burst_duty_cycle_at_max_power = me->params.burst_duty_cycle_at_max_power;
    M_ASSERT(burst_width_µs <= RESTIM_MAX_BURST_WIDTH_µS);
    M_ASSERT(power <= RESTIM_POWER_SCALE);
    M_ASSERT(burst_duty_cycle_at_max_power <= RESTIM_POWER_SCALE);
    _Static_assert((uint64_t)RESTIM_MAX_BURST_WIDTH_µS * RESTIM_POWER_SCALE * RESTIM_POWER_SCALE <= UINT32_MAX); // overflow check
    uint32_t total_high_time_¼µs = (uint32_t)power * burst_duty_cycle_at_max_power * burst_width_µs / (1024 * 1024 / 4);
    uint32_t nr_of_pulses = 3;
    uint16_t pace_µs = (burst_width_µs - total_high_time_¼µs / 4 / nr_of_pulses) / (nr_of_pulses - 1);
    uint16_t pulse_width_¼µs = total_high_time_¼µs / nr_of_pulses;

    if (pulse_width_¼µs >= MIN_PULSE_WIDTH_¼µs) {
        AddBurst(me, *t, elcon, pace_µs, pulse_width_¼µs, nr_of_pulses, burst_width_µs, polarity);
        *t += me->params.burst_width_µs + me->params.inversion_time_µs;
        AddBurst(me, *t, elcon, pace_µs, pulse_width_¼µs, nr_of_pulses, burst_width_µs, !polarity);
        *t += me->params.burst_width_µs + me->params.triac_switch_time_µs;
    }
}

static void AddDummyBurst(Restim *me, uint32_t *t)
{   
    Burst b = {
        .start_time_µs = *t,
        .elcon = {0, 0},
        .pace_µs = 0,
        .nr_of_pulses = 1,
        .pulse_width_¼µs = 2 * 4,   // HACK
        .phase = 0,
        .amplitude = 0,
        .flags = 0,
    };
    // Burst_print(&b);
    me->bursts_storage[me->nr_of_bursts++] = b;
}



static void RecomputeBursts(Restim *me)
{
    // uint64_t start = BSP_microsecondsSinceBoot();

    memset(me->bursts_storage, 0, sizeof(me->bursts_storage));
    me->seq_nr++;
    me->burst_nr = 0;
    me->nr_of_bursts = 0;

    // TODO: check if data is valid


    bool polarity = me->seq_nr % 2;
    if (me->params.defeat_pulse_randomization) {
        polarity = 0;
    }

    uint32_t t = 0;
    AddBurstGroup(me, &t, EL_A | EL_BD, me->params.a_bd_power, polarity);
    AddBurstGroup(me, &t, EL_B | EL_AC, me->params.b_ac_power, polarity);
    AddBurstGroup(me, &t, EL_C | EL_BD, me->params.c_bd_power, polarity);
    AddBurstGroup(me, &t, EL_D | EL_AC, me->params.d_ac_power, polarity);
    AddBurstGroup(me, &t, EL_A | EL_B, me->params.ab_power, polarity);
    AddBurstGroup(me, &t, EL_B | EL_C, me->params.bc_power, polarity);
    AddBurstGroup(me, &t, EL_C | EL_D, me->params.cd_power, polarity);
    AddBurstGroup(me, &t, EL_A | EL_D, me->params.ad_power, polarity);
    // schedule a dummy burst for flow control
    t += 5000;
    if (t < me->params.interval_between_pulses_µs) {
        t = me->params.interval_between_pulses_µs;
    }
    AddDummyBurst(me, &t);

    // uint64_t end = BSP_microsecondsSinceBoot();
    // uint32_t elapsed = (uint32_t)(end - start);
    // BSP_logf("%s took %u µs\n", __func__, elapsed);
}

static bool ScheduleNextBurst(Restim *me)
{
    Burst burst;
    if (me->burst_nr >= me->nr_of_bursts) {
        BSP_stopSequencerClock();
        if (BSP_microsecondsSinceBoot() - me->last_parameter_update_µs > RESTIM_DEADMAN_TIMEOUT_µS) {
            BSP_logf("%s: Activating deadman switch.\n", __func__);
            Restim_ResetParameters(me);
            return false;
        }

        RecomputeBursts(me);
        BSP_setElectrodeConfiguration(me->bursts_storage[0].elcon);
        BSP_startSequencerClock(-200);
    }
    burst = me->bursts_storage[me->burst_nr++];
    return BSP_scheduleBurst(&burst);
}

/*
 * Below are the functions implementing this module's interface.
 */

void Restim_Init(Restim *me)
{
    BSP_logf("%s\n", __func__);
    memset(me, 0, sizeof(Restim));
};

void Restim_SetParameters(Restim *me, RestimPulseParameters const *params)
{
    // BSP_logf("%s\n", __func__);
    me->params = *params;
    me->last_parameter_update_µs = BSP_microsecondsSinceBoot();
}

void Restim_ResetParameters(Restim *me)
{
    memset(me, 0, sizeof(Restim));
    me->last_parameter_update_µs = BSP_microsecondsSinceBoot();
};

bool Restim_ScheduleFirstBurst(Restim *me)
{
    BSP_logf("%s\n", __func__);
    me->nr_of_bursts = 0;   // trigger recomputation
    return ScheduleNextBurst(me);
}

bool Restim_ScheduleNextBurst(Restim *me)
{
    // BSP_logf("%s\n", __func__);
    return ScheduleNextBurst(me);
}

void Restim_Stop(Restim *me)
{
    BSP_logf("Restim: stop\n");
    BSP_stopSequencerClock();
}