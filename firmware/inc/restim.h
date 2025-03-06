/*
 * restim.h
 *
 * TODO header
 */

#ifndef INC_RESTIM_H_
#define INC_RESTIM_H_

#include "bsp_app.h"

#define RESTIM_POWER_SCALE 1024u
#define RESTIM_DUTY_CYCLE_SCALE 1024u
#define RESTIM_MAX_BURST_WIDTH_µS 1024u

#define RESTIM_DEADMAN_TIMEOUT_µS 1000000ULL

typedef struct {
    uint16_t a_bd_power;                        // in 1/1024 of max
    uint16_t b_ac_power;                        // in 1/1024 of max
    uint16_t c_bd_power;                        // in 1/1024 of max
    uint16_t d_ac_power;                        // in 1/1024 of max

    uint16_t ab_power;                          // in 1/1024 of max
    uint16_t bc_power;                          // in 1/1024 of max
    uint16_t cd_power;                          // in 1/1024 of max
    uint16_t ad_power;                          // in 1/1024 of max

    uint16_t burst_duty_cycle_at_max_power;     // in 1/1024 of max
    uint16_t burst_width_µs;                    // 0 .. 1024 µs
    uint16_t inversion_time_µs;                 // µs between positive and negative part of the pulse
    uint16_t triac_switch_time_µs;              // µs between 2 pulses with different triac confics.

    uint32_t interval_between_pulses_µs;        

    uint8_t flags;                              // unused
    uint8_t defeat_pulse_randomization;         // DANGER
    uint16_t padding;
} RestimPulseParameters ;

typedef struct {
    RestimPulseParameters params;
    uint64_t last_parameter_update_µs;
    uint8_t seq_nr;
    uint8_t burst_nr;
    uint8_t nr_of_bursts;
    Burst bursts_storage[30];
} Restim;

void Restim_Init(Restim *);
void Restim_SetParameters(Restim *, RestimPulseParameters const*);
void Restim_ResetParameters(Restim *);
bool Restim_ScheduleFirstBurst(Restim *);
bool Restim_ScheduleNextBurst(Restim *);
void Restim_Stop(Restim *);

#endif
