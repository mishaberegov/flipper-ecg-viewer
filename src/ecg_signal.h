#pragma once
#include <stdbool.h>
#include <stdint.h>

#define ECG_WIDTH 128
#define ECG_SAMPLE_HZ 250
#define ECG_SAMPLES_PER_COLUMN 10

typedef struct {
    int16_t low;
    int16_t high;
    bool valid;
} EcgColumn;

typedef struct {
    EcgColumn columns[ECG_WIDTH];
    uint8_t write_index;
    uint8_t samples_in_column;
    int16_t pending_low;
    int16_t pending_high;
    int32_t baseline_q8;
    bool baseline_ready;
} EcgSignal;

typedef enum {
    EcgControlBack,
    EcgControlUp,
    EcgControlDown,
    EcgControlLeft,
    EcgControlRight,
} EcgControl;

typedef struct {
    uint8_t gain;
    int8_t offset;
    bool exit_requested;
} EcgControls;

void ecg_controls_apply(EcgControls* controls, EcgControl control);
void ecg_signal_init(EcgSignal* signal);
void ecg_signal_push(EcgSignal* signal, uint16_t raw);
uint8_t ecg_signal_to_y(int16_t delta, uint8_t gain);
uint8_t ecg_signal_to_y_with_offset(int16_t delta, uint8_t gain, int8_t offset);
uint32_t ecg_next_sample_tick(uint32_t now, uint32_t previous, uint32_t period);
