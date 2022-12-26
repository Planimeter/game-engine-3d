/* Copyright Planimeter. All Rights Reserved. */

#ifndef TIMER_H
#define TIMER_H

#ifdef __cplusplus
extern "C" {
#endif

uint64_t timer_step();
void     timer_sleep(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* TIMER_H */
