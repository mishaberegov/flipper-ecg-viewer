#include "../src/ecg_signal.h"
#include <assert.h>
#include <stdio.h>

static void test_constant_input_produces_flat_column(void) {
    EcgSignal s;
    ecg_signal_init(&s);
    for(unsigned i = 0; i < ECG_SAMPLES_PER_COLUMN; ++i) ecg_signal_push(&s, 2048);
    assert(s.write_index == 1);
    assert(s.columns[0].valid);
    assert(s.columns[0].low == 0);
    assert(s.columns[0].high == 0);
}

static void test_spike_retained_in_min_max_envelope(void) {
    EcgSignal s;
    ecg_signal_init(&s);
    ecg_signal_push(&s, 2048);
    for(unsigned i = 1; i < ECG_SAMPLES_PER_COLUMN; ++i)
        ecg_signal_push(&s, i == 5 ? 2070 : 2048);
    assert(s.columns[0].low <= 0);
    assert(s.columns[0].high >= 20);
}

static void test_gain_and_clipping_keep_trace_below_header(void) {
    assert(ecg_signal_to_y(0, 2) == 38);
    assert(ecg_signal_to_y(10, 2) == 18);
    assert(ecg_signal_to_y(100, 2) == 13);
    assert(ecg_signal_to_y(-100, 2) == 63);
}

static void test_scheduler_skips_missed_slots_without_burst(void) {
    assert(ecg_next_sample_tick(104, 100, 4) == 104);
    assert(ecg_next_sample_tick(113, 100, 4) == 117);
    assert(ecg_next_sample_tick(0, UINT32_MAX - 3, 4) == 0);
}

static void test_back_is_a_control_state_not_a_queued_event(void) {
    EcgControls c = {.gain = 2};
    ecg_controls_apply(&c, EcgControlBack);
    assert(c.exit_requested);
}

static void test_ring_wraps_and_keeps_newest_column(void) {
    EcgSignal s;
    ecg_signal_init(&s);
    for(unsigned col = 0; col < ECG_WIDTH + 1; ++col) {
        for(unsigned i = 0; i < ECG_SAMPLES_PER_COLUMN; ++i)
            ecg_signal_push(&s, col == ECG_WIDTH ? 3000 : 2048);
    }
    assert(s.write_index == 1);
    assert(s.columns[0].valid && s.columns[0].high > 0);
    assert(s.columns[1].valid && s.columns[1].high == 0);
}

static void test_gain_and_offset_stay_bounded(void) {
    EcgControls c = {.gain = 2};
    for(unsigned i = 0; i < 10; ++i) ecg_controls_apply(&c, EcgControlUp);
    assert(c.gain == 16);
    for(unsigned i = 0; i < 10; ++i) ecg_controls_apply(&c, EcgControlDown);
    assert(c.gain == 1);
    for(unsigned i = 0; i < 20; ++i) ecg_controls_apply(&c, EcgControlLeft);
    assert(c.offset == 10);
    for(unsigned i = 0; i < 30; ++i) ecg_controls_apply(&c, EcgControlRight);
    assert(c.offset == -10);
}

static void test_offset_cannot_unclip_saturated_peak(void) {
    assert(ecg_signal_to_y_with_offset(100, 2, 10) == 13);
    assert(ecg_signal_to_y_with_offset(-100, 2, -10) == 63);
    assert(ecg_signal_to_y_with_offset(0, 2, 10) == 48);
}

int main(void) {
    test_offset_cannot_unclip_saturated_peak();
    test_ring_wraps_and_keeps_newest_column();
    test_gain_and_offset_stay_bounded();
    test_back_is_a_control_state_not_a_queued_event();
    test_scheduler_skips_missed_slots_without_burst();
    test_gain_and_clipping_keep_trace_below_header();
    test_constant_input_produces_flat_column();
    test_spike_retained_in_min_max_envelope();
    puts("signal tests passed");
    return 0;
}
