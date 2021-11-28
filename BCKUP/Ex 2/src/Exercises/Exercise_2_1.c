#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>

#include <SDL2/SDL_scancode.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "TUM_Ball.h"
#include "TUM_Draw.h"
#include "TUM_Event.h"
#include "TUM_Sound.h"
#include "TUM_Utils.h"
#include "TUM_Font.h"

#include "AsyncIO.h"

#define mainGENERIC_PRIORITY (tskIDLE_PRIORITY)
#define mainGENERIC_STACK_SIZE ((unsigned short)2560)
coord_t triangle_points[3];
int circle_x,circle_y,square_x,square_y;
int AngleIncrementer =0;

static TaskHandle_t DrawingTask_Handle = NULL;
static TaskHandle_t PositionIncrementationTask_Handle = NULL;



typedef struct buttons_buffer {
    unsigned char buttons[SDL_NUM_SCANCODES];
    SemaphoreHandle_t lock;
} buttons_buffer_t;

static buttons_buffer_t buttons = { 0 };

void xGetButtonInput(void)
{
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
        xQueueReceive(buttonInputQueue, &buttons.buttons, 0);
        xSemaphoreGive(buttons.lock);
    }
}

#define KEYCODE(CHAR) SDL_SCANCODE_##CHAR

void Drawing_Task(void *pvParameters)
{
    tumDrawBindThread();
    square_x=350;
    square_y=200;
    circle_x=175;
    circle_y=225;
    triangle_points[0].x=250;
    triangle_points[0].y=250;
    triangle_points[1].x=275;
    triangle_points[1].y=200;
    triangle_points[2].x=300;
    triangle_points[2].y=250;     
               

    while (1) {

        tumDrawClear(White); // Clear screen
        circle_x = 262.5 + 87.5*cos(AngleIncrementer);
        circle_y= 225 + 100*sin(AngleIncrementer);
        tumDrawCircle(circle_x,circle_y,25,Aqua);

        tumDrawTriangle(triangle_points,Green);
        square_x= 262.5 - 87.5*cos(AngleIncrementer);
        square_y=225 - 100*sin(AngleIncrementer);
        tumDrawFilledBox(square_x,square_y,50,50,Silver);

        vTaskDelay((TickType_t)90);
        tumDrawText("LIGHT WEIGHT",circle_x,50,TUMBlue);
        tumDrawText("YEAH BUDDY!",225,400,TUMBlue);  
        tumDrawUpdateScreen(); // Refresh the screen to draw string

        // Basic sleep of 1000 milliseconds
      
    }

}
void PositionIncrementation_Task(void *pvParameters){
   
    while(1){
    AngleIncrementer+=1;
    vTaskDelay((TickType_t)90);
            }


}

int main(int argc, char *argv[])
{
    char *bin_folder_path = tumUtilGetBinFolderPath(argv[0]);

    printf("Initializing: ");

    if (tumDrawInit(bin_folder_path)) {
        PRINT_ERROR("Failed to initialize drawing");
        goto err_init_drawing;
    }

    if (tumEventInit()) {
        PRINT_ERROR("Failed to initialize events");
        goto err_init_events;
    }

    if (tumSoundInit(bin_folder_path)) {
        PRINT_ERROR("Failed to initialize audio");
        goto err_init_audio;
    }

    buttons.lock = xSemaphoreCreateMutex(); // Locking mechanism
    if (!buttons.lock) {
        PRINT_ERROR("Failed to create buttons lock");
        goto err_buttons_lock;
    }

    if (xTaskCreate(Drawing_Task, "Drawing_Task", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY, &DrawingTask_Handle) != pdPASS) {
        goto err_demotask;
    }
    if (xTaskCreate(PositionIncrementation_Task, "Position Incrementation Task", mainGENERIC_STACK_SIZE * 2, NULL,
                mainGENERIC_PRIORITY, &PositionIncrementationTask_Handle) != pdPASS) {
    goto err_demotask;
    }

    vTaskStartScheduler();

    return EXIT_SUCCESS;

err_demotask:
    vSemaphoreDelete(buttons.lock);
err_buttons_lock:
    tumSoundExit();
err_init_audio:
    tumEventExit();
err_init_events:
    tumDrawExit();
err_init_drawing:
    return EXIT_FAILURE;
}

// cppcheck-suppress unusedFunction
__attribute__((unused)) void vMainQueueSendPassed(void)
{
    /* This is just an example implementation of the "queue send" trace hook. */
}

// cppcheck-suppress unusedFunction
__attribute__((unused)) void vApplicationIdleHook(void)
{
#ifdef __GCC_POSIX__
    struct timespec xTimeToSleep, xTimeSlept;
    /* Makes the process more agreeable when using the Posix simulator. */
    xTimeToSleep.tv_sec = 1;
    xTimeToSleep.tv_nsec = 0;
    nanosleep(&xTimeToSleep, &xTimeSlept);
#endif
}
