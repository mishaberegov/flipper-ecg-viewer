#include "ecg_signal.h"
#include <string.h>

void ecg_controls_apply(EcgControls* controls, EcgControl control) {
    switch(control) {
    case EcgControlBack: controls->exit_requested = true; break;
    case EcgControlUp: if(controls->gain < 16) controls->gain *= 2; break;
    case EcgControlDown: if(controls->gain > 1) controls->gain /= 2; break;
    case EcgControlLeft: if(controls->offset < 10) controls->offset++; break;
    case EcgControlRight: if(controls->offset > -10) controls->offset--; break;
    }
}

void ecg_signal_init(EcgSignal* signal) {
    memset(signal, 0, sizeof(*signal));
}

uint8_t ecg_signal_to_y_with_offset(int16_t delta, uint8_t gain, int8_t offset) {
    int32_t y = 38 - (int32_t)delta * gain + offset;
    if(y < 13) y = 13;
    if(y > 63) y = 63;
    return (uint8_t)y;
}

uint8_t ecg_signal_to_y(int16_t delta, uint8_t gain) {
    return ecg_signal_to_y_with_offset(delta, gain, 0);
}

uint32_t ecg_next_sample_tick(uint32_t now, uint32_t previous, uint32_t period) {
    const uint32_t next = previous + period;
    return (int32_t)(now - next) > 0 ? now + period : next;
}

void ecg_signal_push(EcgSignal* signal, uint16_t raw) {
    const int32_t input_q8 = (int32_t)raw << 8;
    if(!signal->baseline_ready) {
        signal->baseline_q8 = input_q8;
        signal->baseline_ready = true;
    }
    const int16_t delta = (int16_t)((input_q8 - signal->baseline_q8) / 256);
    signal->baseline_q8 += (input_q8 - signal->baseline_q8) / 256;

    if(signal->samples_in_column == 0) {
        signal->pending_low = delta;
        signal->pending_high = delta;
    } else {
        if(delta < signal->pending_low) signal->pending_low = delta;
        if(delta > signal->pending_high) signal->pending_high = delta;
    }
    if(++signal->samples_in_column == ECG_SAMPLES_PER_COLUMN) {
        EcgColumn* column = &signal->columns[signal->write_index];
        column->low = signal->pending_low;
        column->high = signal->pending_high;
        column->valid = true;
        signal->write_index = (uint8_t)((signal->write_index + 1) % ECG_WIDTH);
        signal->samples_in_column = 0;
    }
}
