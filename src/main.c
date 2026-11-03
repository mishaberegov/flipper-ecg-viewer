#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <stdio.h>
#include <string.h>
#include "ecg_signal.h"

#define GUI_FRAME_MS 40
#define GRAPH_CENTER 38

typedef struct {
    FuriMutex* mutex;
    FuriMessageQueue* events;
    FuriThread* sampler;
    EcgSignal signal;
    uint16_t raw;
    uint32_t late_samples;
    EcgControls controls;
    bool running;
} EcgApp;

static void input_callback(InputEvent* event, void* context) {
    EcgApp* app = context;
    if(event->type == InputTypePress) {
        if(event->key == InputKeyBack) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            ecg_controls_apply(&app->controls, EcgControlBack);
            furi_mutex_release(app->mutex);
        } else {
            furi_message_queue_put(app->events, event, 0);
        }
    }
}

static int32_t sample_worker(void* context) {
    EcgApp* app = context;
    furi_hal_gpio_init_simple(&gpio_ext_pc0, GpioModeAnalog);
    FuriHalAdcHandle* adc = furi_hal_adc_acquire();
    furi_hal_adc_configure_ex(
        adc,
        FuriHalAdcScale2500,
        FuriHalAdcClockSync16,
        FuriHalAdcOversampleNone,
        FuriHalAdcSamplingtime640_5);

    uint32_t period = furi_kernel_get_tick_frequency() / ECG_SAMPLE_HZ;
    if(period == 0) period = 1;
    uint32_t next = furi_get_tick();
    while(true) {
        if((int32_t)(next - furi_get_tick()) > 0) furi_delay_until_tick(next);
        uint16_t raw = furi_hal_adc_read(adc, FuriHalAdcChannel1); // PC0
        uint32_t now = furi_get_tick();

        furi_mutex_acquire(app->mutex, FuriWaitForever);
        if(!app->running) {
            furi_mutex_release(app->mutex);
            break;
        }
        app->raw = raw;
        ecg_signal_push(&app->signal, raw);
        uint32_t scheduled = next + period;
        if((int32_t)(now - scheduled) > 0) app->late_samples++;
        furi_mutex_release(app->mutex);
        next = ecg_next_sample_tick(now, next, period);
    }
    furi_hal_adc_release(adc);
    return 0;
}

static void draw_callback(Canvas* canvas, void* context) {
    EcgApp* app = context;
    EcgSignal signal;
    uint16_t raw;
    uint32_t late;
    uint8_t gain;
    int8_t offset;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    memcpy(&signal, &app->signal, sizeof(signal));
    raw = app->raw;
    late = app->late_samples;
    gain = app->controls.gain;
    offset = app->controls.offset;
    furi_mutex_release(app->mutex);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    char label[32];
    snprintf(label, sizeof(label), "250Hz x%u ADC%u%s", gain, raw, late ? " !" : "");
    canvas_draw_str(canvas, 0, 9, label);
    canvas_draw_line(canvas, 0, 11, ECG_WIDTH - 1, 11);

    int16_t previous_y = -1;
    for(uint8_t x = 0; x < ECG_WIDTH; x++) {
        const EcgColumn* col = &signal.columns[(signal.write_index + x) % ECG_WIDTH];
        if(!col->valid) {
            previous_y = -1;
            continue;
        }
        int16_t top = ecg_signal_to_y_with_offset(col->high, gain, offset);
        int16_t bottom = ecg_signal_to_y_with_offset(col->low, gain, offset);
        int16_t middle = (top + bottom) / 2;
        if(previous_y >= 0) canvas_draw_line(canvas, x - 1, previous_y, x, middle);
        canvas_draw_line(canvas, x, top, x, bottom);
        previous_y = middle;
    }
}

int32_t ecg_viewer_app(void* context) {
    UNUSED(context);
    EcgApp* app = malloc(sizeof(EcgApp));
    furi_check(app);
    memset(app, 0, sizeof(*app));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->events = furi_message_queue_alloc(8, sizeof(InputEvent));
    furi_check(app->mutex && app->events);
    ecg_signal_init(&app->signal);
    app->controls.gain = 2;
    app->running = true;

    ViewPort* port = view_port_alloc();
    view_port_draw_callback_set(port, draw_callback, app);
    view_port_input_callback_set(port, input_callback, app);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, port, GuiLayerFullscreen);

    app->sampler = furi_thread_alloc_ex("ECG sample", 2048, sample_worker, app);
    furi_check(app->sampler);
    furi_thread_start(app->sampler);

    while(true) {
        InputEvent event;
        if(furi_message_queue_get(app->events, &event, GUI_FRAME_MS) == FuriStatusOk) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            switch(event.key) {
            case InputKeyUp: ecg_controls_apply(&app->controls, EcgControlUp); break;
            case InputKeyDown: ecg_controls_apply(&app->controls, EcgControlDown); break;
            case InputKeyLeft: ecg_controls_apply(&app->controls, EcgControlLeft); break;
            case InputKeyRight: ecg_controls_apply(&app->controls, EcgControlRight); break;
            default: break;
            }
            furi_mutex_release(app->mutex);
        }
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        bool exit_requested = app->controls.exit_requested;
        furi_mutex_release(app->mutex);
        if(exit_requested) break;
        view_port_update(port);
    }

    // Detach GUI before destroying its input queue or render context.
    gui_remove_view_port(gui, port);
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->running = false;
    furi_mutex_release(app->mutex);
    furi_thread_join(app->sampler);
    furi_thread_free(app->sampler);
    view_port_free(port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app->events);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
