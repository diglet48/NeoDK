/*
 * neodk_main.c -- Main program of the Neostim™ Development Kit demo firmware.
 *
 *  NOTICE (do not remove):
 *      This file is part of project NeoDK (https://github.com/Onwrikbaar/NeoDK).
 *      See https://github.com/Onwrikbaar/NeoDK/blob/main/LICENSE.txt for full license details.
 *
 *  Created on: Oct 20, 2023
 *      Author: mark
 *   Copyright  2023..2025 Neostim™
 */

#include <stdlib.h>

#include "bsp_dbg.h"
#include "bsp_mao.h"
#include "bsp_app.h"
#include "app_event.h"
#include "controller.h"
#include "debug_cli.h"


#ifndef MICROSECONDS_PER_APP_TIMER_TICK
#define MICROSECONDS_PER_APP_TIMER_TICK 1000    // For 1 kHz tick.
#endif

typedef struct {
    EventQueue event_queue;                     // This MUST be the first member.
    uint8_t event_storage[100];
    Controller *controller;
    Sequencer *sequencer;
    uint64_t prev_micros;
    uint8_t keep_running;
} Boss;


static void Boss_init(Boss *me)
{
    EventQueue_init(&me->event_queue, me->event_storage, sizeof me->event_storage);
    me->controller = Controller_new();
    me->sequencer = Sequencer_new();
    me->prev_micros = 0ULL;
    me->keep_running = true;
}


static void Boss_finish(Boss *me)
{
    Sequencer_delete(me->sequencer);
    Controller_delete(me->controller);
}


static void handlePosixSignal(Boss *me, int sig)
{
    BSP_logf("Received signal %d\n", sig);
    if (sig == 2) me->keep_running = false;
}


static void printTime(uint64_t app_timer_micros)
{
    uint32_t seconds_since_boot = (uint32_t)(app_timer_micros / 1000000UL);
    CLI_logf("Time %02u:%02u:%02u\n", seconds_since_boot / 3600, (seconds_since_boot / 60) % 60, seconds_since_boot % 60);
}


static void dispatchEvent(Boss *me, AOEvent const *evt)
{
    uint8_t evt_type = AOEvent_type(evt);
    switch (evt_type)
    {
        case ET_APP_HEARTBEAT:
            printTime(*(uint64_t const *)AOEvent_data(evt));
            break;
        case ET_POSIX_SIGNAL:
            handlePosixSignal(me, *(int const *)AOEvent_data(evt));
            break;
        case ET_BUTTON_PUSHED:
            EventQueue_postEvent((EventQueue *)me->sequencer, ET_TOGGLE_PLAY_PAUSE, NULL, 0);
            break;
        case ET_BUTTON_RELEASED:
            // Ignore for now.
            break;
        case ET_LOG_FROM_IRQ: {
            LogArgs const *la = (LogArgs const *)AOEvent_data(evt);
            BSP_logf(la->fmt, la->as);
            break;
        }
        default:
            BSP_logf("Boss_%s(%hu)?\n", __func__, evt_type);
    }
}


static void onButtonToggle(Boss *me, uint32_t pushed)
{
    EventQueue_postEvent(&me->event_queue, pushed ? ET_BUTTON_PUSHED : ET_BUTTON_RELEASED, NULL, 0);
}


static void onAppTimerTick(Boss *me, uint64_t app_timer_micros)
{
    if (Controller_heartbeatElapsed(me->controller, app_timer_micros - me->prev_micros)) {
        EventQueue_postEvent(&me->event_queue, ET_APP_HEARTBEAT, (uint8_t const *)&app_timer_micros, sizeof app_timer_micros);
        me->prev_micros = app_timer_micros;
    }
}


static bool Boss_handleEvent(Boss *me)
{
    return EventQueue_handleNextEvent(&me->event_queue, (EvtFunc)&dispatchEvent, me);
}


static bool noEventsPending(Boss const *me)
{
    return EventQueue_isEmpty((EventQueue const *)me->sequencer)
        && EventQueue_isEmpty((EventQueue const *)me->controller)
        && EventQueue_isEmpty(&me->event_queue);
}


static void setupAndRunApplication(Boss *me)
{
    DataLink *datalink = DataLink_new();
    Sequencer_init(me->sequencer);
    Controller_init(me->controller, me->sequencer, datalink);
    CLI_init(&me->event_queue, me->sequencer, datalink);
    Sequencer_start(me->sequencer);

    Selector button_selector;
    BSP_registerButtonHandler(Selector_init(&button_selector, (Action)&onButtonToggle, me));

    Controller_start(me->controller);
    while (me->keep_running) {
        // Service the worker objects in order of decreasing priority.
        if (Sequencer_handleEvent(me->sequencer)) continue;
        if (Controller_handleEvent(me->controller)) continue;
        if (Boss_handleEvent(me)) continue;
        BSP_idle((bool (*)(const void *))&noEventsPending, me);
    }
    Controller_stop(me->controller);
    Sequencer_stop(me->sequencer);
    DataLink_delete(datalink);
}


int main()
{
    BSP_initDebug();
    BSP_logf("Initialising %s...\n", BSP_firmwareVersion());
    BSP_init();                                 // Get the hardware ready.

    Boss boss;                                  // The supervisor object.
    Boss_init(&boss);
    BSP_registerAppTimerHandler((void (*)(void *, uint64_t))&onAppTimerTick, &boss, MICROSECONDS_PER_APP_TIMER_TICK);
    setupAndRunApplication(&boss);
    Boss_finish(&boss);

    BSP_close();
    BSP_logf("Bye!\n");
    BSP_closeDebug();
    BSP_shutDown();
    return 0;
}
