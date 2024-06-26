/* LVGL Example project to make minimal slider
 * Optional: When using v7 Kaleido PCB, trigger PWM from Front-Light (FL)
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_freertos_hooks.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

// LVGL
#include "lvgl/lvgl.h"
#include "lvgl_helpers.h"

/*********************
 *      DEFINES
 *********************/
#define TAG "Explr"

extern "C"
{
    void app_main();
}
/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_tick_task(void *arg);
static void guiTask(void *pvParameter);
static void create_demo_application(void);

#define DISPLAY_FRONTLIGHT    GPIO_NUM_11

#define LV_TICK_PERIOD_MS 1
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          int(DISPLAY_FRONTLIGHT)
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY               (0) // 4096 Set duty to 50%. (2 ** 13) * 50% = 4096
#define LEDC_FREQUENCY          (4000) // Frequency in Hertz. Set frequency at 4 kHz

static void slider_event_cb(lv_event_t * e);

static lv_obj_t * slider;
static lv_obj_t * slider_label;
uint8_t led_duty_multiplier = 80;

/**********************
 *   APPLICATION MAIN
 **********************/

void app_main() {
    printf("Epaper example. LVGL version %d.%d\n\n", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR);

    //printf("app_main started. DISP_BUF_SIZE:%d LV_HOR_RES_MAX:%d V_RES_MAX:%d\n", DISP_BUF_SIZE, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    gpio_set_direction(DISPLAY_FRONTLIGHT, GPIO_MODE_OUTPUT);
    gpio_set_level(DISPLAY_FRONTLIGHT, 0);
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,  // Set output frequency at 4 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
        // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .gpio_num       = LEDC_OUTPUT_IO,
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = LEDC_TIMER,
        
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    // Set duty to 50%
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
    /* If you want to use a task to create the graphic, you NEED to create a Pinned task
     * Otherwise there can be problem such as memory corruption and so on.
     * NOTE: When not using Wi-Fi nor Bluetooth you can pin the guiTask to core 0 */
    xTaskCreatePinnedToCore(guiTask, "gui", 4096*2, NULL, 0, NULL, 1);
}

/* Creates a semaphore to handle concurrent call to lvgl stuff
 * If you wish to call *any* lvgl function from other threads/tasks
 * you should lock on the very same semaphore! */
SemaphoreHandle_t xGuiSemaphore;

static void guiTask(void *pvParameter) {

    (void) pvParameter;
    xGuiSemaphore = xSemaphoreCreateMutex();

    lv_init();

    /* Initialize SPI or I2C bus used by the drivers */
    lvgl_driver_init();
    // Screen is cleaned in first flush
    printf("DISP_BUF*sizeof(lv_color_t) %d", DISP_BUF_SIZE * sizeof(lv_color_t));
    lv_color_t* buf1 = (lv_color_t*) heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    assert(buf1 != NULL);

    // OPTIONAL: Do not use double buffer for epaper
    lv_color_t* buf2 = NULL;
    //lv_color_t* buf2 = (lv_color_t*) heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    
    /* PLEASE NOTE:
       This size must much the size of DISP_BUF_SIZE declared on lvgl_helpers.h
    */
    uint32_t size_in_px = DISP_BUF_SIZE;
    //size_in_px /= 8; // In v9 size is in bytes
    lv_display_t * disp = lv_display_create(LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_display_set_flush_cb(disp, (lv_display_flush_cb_t) disp_driver_flush);

    printf("LV ROTATION:%d\n\n",lv_display_get_rotation(disp));
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_0);
    // COLOR SETTING after v9:
    // LV_COLOR_FORMAT_L8 = monochrome 1BPP (8 bits per pixel) Does not work correctly
    // LV_COLOR_FORMAT_RGB332
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB332);

    // Needed?
    //lv_display_add_event_cb(disp, disp_release_cb, LV_EVENT_DELETE, lv_display_get_user_data(disp));

    /**MODE
     * LV_DISPLAY_RENDER_MODE_PARTIAL This way the buffers can be smaller then the display to save RAM. At least 1/10 screen sized buffer(s) are recommended.
     * LV_DISPLAY_RENDER_MODE_DIRECT The buffer(s) has to be screen sized and LVGL will render into the correct location of the buffer. This way the buffer always contain the whole image. With 2 buffers the buffers’ content are kept in sync automatically. (Old v7 behavior)
     * LV_DISPLAY_RENDER_MODE_FULL Just always redraw the whole screen.
    */
    lv_display_set_buffers(disp, buf1, buf2, size_in_px, LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* Register an input device when enabled on the menuconfig */
#if CONFIG_LV_TOUCH_CONTROLLER != TOUCH_CONTROLLER_NONE
    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, (lv_indev_read_cb_t) touch_driver_read);
#endif

    /* Create and start a periodic timer interrupt to call lv_tick_inc */
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "periodic_gui"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, LV_TICK_PERIOD_MS * 1000));

    /* Create the demo application */
    create_demo_application();
    /* Force screen refresh */
    lv_refr_now(NULL);

    while (1) {
        /* Delay 1 tick (assumes FreeRTOS tick is 10ms */
        vTaskDelay(pdMS_TO_TICKS(10));

        /* Try to take the semaphore, call lvgl related function on success */
        if (pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY)) {
            lv_task_handler();
            xSemaphoreGive(xGuiSemaphore);
       }
    }

    /* A task should NEVER return */
    free(buf1);
    vTaskDelete(NULL);
}

/***
 * slider event - updates PWM duty
 **/
static void slider_event_cb(lv_event_t * e)
{
    slider = (lv_obj_t*) lv_event_get_target(e);
    char buf[8];
    int sliderv = (int)lv_slider_get_value(slider);
    int led_duty = sliderv * led_duty_multiplier;
    printf("v:%d\n",sliderv);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, led_duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

    lv_snprintf(buf, sizeof(buf), "%d%%", sliderv);
    lv_label_set_text(slider_label, buf);
    lv_obj_align_to(slider_label, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
}

static void file_explorer_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = (lv_obj_t*)lv_event_get_target(e);

    if(code == LV_EVENT_VALUE_CHANGED) {
        const char * cur_path =  lv_file_explorer_get_current_path(obj);
        const char * sel_fn = lv_file_explorer_get_selected_file_name(obj);
        LV_LOG_USER("%s%s", cur_path, sel_fn);
    }
}

void lv_example_file_explorer(void)
{
    lv_obj_t * file_explorer = lv_file_explorer_create(lv_screen_active());
    lv_file_explorer_set_sort(file_explorer, LV_EXPLORER_SORT_KIND);
}

bool fl_status = false;

static void event_handler_on(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int led_duty = 0;
    
    if(code == LV_EVENT_VALUE_CHANGED) {
        fl_status = !fl_status;
        int duty = (fl_status) ? 90 : 0;
        led_duty = duty * led_duty_multiplier;
        lv_slider_set_value(slider, duty, LV_ANIM_ON);
        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%d%%", duty);
        lv_label_set_text(slider_label, buf);

        //printf("code: %d\n", (int)code);
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, led_duty);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    }
}

/**
 * A default slider with a label displaying the current value
 */
void create_demo_application(void)
{
    /*Create a slider in the center of the display*/
    slider = lv_slider_create(lv_scr_act());
    lv_obj_center(slider);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /*Create a label below the slider*/
    slider_label = lv_label_create(lv_scr_act());
    lv_label_set_text(slider_label, "0%");
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 0);
    lv_obj_align_to(slider_label, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    lv_obj_t * label;
    lv_obj_t * btn2 = lv_btn_create(lv_scr_act());
    lv_obj_add_event_cb(btn2, event_handler_on, LV_EVENT_ALL, NULL);
    lv_obj_align(btn2, LV_ALIGN_CENTER, 0, 120);
    lv_obj_add_flag(btn2, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_height(btn2, LV_SIZE_CONTENT);

    label = lv_label_create(btn2);
    lv_label_set_text(label, "Toggle ON/OFF");
    lv_obj_center(label);
    //lv_example_file_explorer();
}

static void lv_tick_task(void *arg) {
    (void) arg;
    lv_tick_inc(LV_TICK_PERIOD_MS);
}
