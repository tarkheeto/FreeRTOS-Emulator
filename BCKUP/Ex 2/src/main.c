#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include <stdbool.h>
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

// Global Variables used in Exercise 2 
coord_t triangle_points[3]; //Struct holding the coordinates of the triangle
int circle_x,circle_y,square_x,square_y; 
float AngleIncrementer =0; // Used in the rotating motion of the circle and square
int buttonPresses_A=0;
int buttonPresses_B=0;
int buttonPresses_C=0;
int buttonPresses_D=0;
const int debounceDelay = pdMS_TO_TICKS(25); 
int xOffset = 0;
int yOffset = 0;
//-----------------------------------------
static TaskHandle_t DrawingTask_Handle = NULL;
static TaskHandle_t PositionIncrementationTask_Handle = NULL;
static TaskHandle_t BufferSwap = NULL;


static SemaphoreHandle_t DrawSignal = NULL;
static SemaphoreHandle_t ScreenLock = NULL;


typedef struct buttons_buffer {
    unsigned char buttons[SDL_NUM_SCANCODES];
    unsigned char lastButtonState[SDL_NUM_SCANCODES];
    TickType_t  lastChange[SDL_NUM_SCANCODES];
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
void checkDraw(unsigned char status, const char *msg)
{
    if (status) {
        if (msg)
            fprints(stderr, "[ERROR] %s, %s\n", msg,
                    tumGetErrorMessage());
        else {
            fprints(stderr, "[ERROR] %s\n", tumGetErrorMessage());
        }
    }   
}
void vDrawButtonText(int buttonPresses_A,
                    int buttonPresses_B,
                    int buttonPresses_C,
                    int buttonPresses_D )   
{
    
    static char str[100] = { 0 };
    sprintf(str, "Axis 1: %5d | Axis 2: %5d", tumEventGetMouseX(),
            tumEventGetMouseY());

    checkDraw(tumDrawText(str, 10, DEFAULT_FONT_SIZE * 0.5, TUMBlue),

              __FUNCTION__);
    sprintf(str, "A: %d | B: %d |C : %d | D: %d",
            buttonPresses_A,
            buttonPresses_B,
            buttonPresses_C,
            buttonPresses_D);
    
    checkDraw(tumDrawText(str, 10, DEFAULT_FONT_SIZE * 3, TUMBlue),
                __FUNCTION__); 
      

}

/*  
 *   The Task that refreshes the screen at a constant frame rate
 */
void vSwapBuffers(void *pvParameters)
{
    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();
    const TickType_t frameratePeriod = 20;

    tumDrawBindThread(); // Setup Rendering handle with correct GL context

    while (1) {
        if (xSemaphoreTake(ScreenLock, portMAX_DELAY) == pdTRUE) {
            tumDrawUpdateScreen();
            tumEventFetchEvents(FETCH_EVENT_BLOCK);
            xSemaphoreGive(ScreenLock);
            xSemaphoreGive(DrawSignal);
            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(frameratePeriod));
        }
    }
}

#define KEYCODE(CHAR) SDL_SCANCODE_##CHAR
/* function that detects a change in the state of a button 

    takes in : The SDL index of the button to be checked 
    
    gives out: true if the button is changed from LOW to HIGH and false else 
*/    
bool ButtonStateChangeCheck(int buttonSDLIndex)
{
    //step 1 initialise a ret and a button state variable 
    unsigned char buttonState = 0;
    bool ret = false;
    
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE)
    {
        buttonState = buttons.buttons[buttonSDLIndex]; //save the state from the buffer onto the variable we just created
    }

    //Step3 Compare the button state to the previous one
    if (buttonState != buttons.lastButtonState[buttonSDLIndex]) {
        //If theyre diff : check the debounce 
        if (xTaskGetTickCount() - buttons.lastChange[buttonSDLIndex] > debounceDelay) {
            if (buttonState ==1 ) {
                //this means LOW to HIGH return TRUE 
                ret = true;
            }
            // Save the time at which the change occured
            buttons.lastChange[buttonSDLIndex] = xTaskGetTickCount();
        }
    }
    // Update the buffer with the new state 
    buttons.lastButtonState[buttonSDLIndex] = buttonState;
    xSemaphoreGive(buttons.lock);

    return ret;
}

void Drawing_Task(void *pvParameters)
{
    //tumDrawBindThread();
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

            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) ==
                pdTRUE) {

                xSemaphoreTake(ScreenLock, portMAX_DELAY);

                tumDrawClear(White); // Clear screen
                circle_x = 262.5 + 87.5*cos(AngleIncrementer);
                circle_y= 225 + 100*sin(AngleIncrementer);
                tumDrawCircle(circle_x,circle_y,25,Aqua);

                tumDrawTriangle(triangle_points,Green);
                square_x= 262.5 - 87.5*cos(AngleIncrementer);
                square_y=225 - 100*sin(AngleIncrementer);
                tumDrawFilledBox(square_x,square_y,50,50,Silver);

                //vTaskDelay((TickType_t)90);
                tumDrawText("LIGHT WEIGHT",circle_x,50,TUMBlue);
                tumDrawText("YEAH BUDDY!",225,400,TUMBlue);  
                //tumDrawUpdateScreen(); // Refresh the screen to draw string

                //Showing the numbers of the button tips 
                vDrawButtonText(buttonPresses_A,buttonPresses_B,buttonPresses_C,buttonPresses_D);



                tumDrawSetGlobalXOffset(xOffset);
                tumDrawSetGlobalYOffset(yOffset);
                xSemaphoreGive(ScreenLock);
            }

        vTaskDelay(20);

      
    }

}
void PositionIncrementation_Task(void *pvParameters){


   
    while(1){
                tumEventFetchEvents(FETCH_EVENT_BLOCK |
                                    FETCH_EVENT_NO_GL_CHECK);
                xGetButtonInput(); // Update global input


                // `buttons` is a global shared variable and as such needs to be
                // guarded with a mutex, mutex must be obtained before accessing the
                // resource and given back when you're finished. If the mutex is not
                // given back then no other task can access the reseource.
                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(
                                            Q)]) { // Equiv to SDL_SCANCODE_Q
                        exit(EXIT_SUCCESS);
                    }
                        xSemaphoreGive(buttons.lock);
                    if (ButtonStateChangeCheck(KEYCODE(A))==true)
                        {buttonPresses_A++;
                        }
                    if (ButtonStateChangeCheck(KEYCODE(B))==true)
                        {buttonPresses_B++;
                        }
                    if (ButtonStateChangeCheck(KEYCODE(C))==true)
                        {buttonPresses_C++;
                        }
                    if (ButtonStateChangeCheck(KEYCODE(D))==true)   
                        {buttonPresses_D++;
                        }
                    xSemaphoreGive(buttons.lock);
                }
                // Reseting the values of the button variables upon left mouse click detection 
                
                if(tumEventGetMouseLeft()){
                    buttonPresses_A=0;
                    buttonPresses_B=0;
                    buttonPresses_C=0;
                    buttonPresses_D=0;
                }
      
                if(tumEventGetMouseX()){
                    xOffset = SCREEN_WIDTH/12 -tumEventGetMouseX()/8 ;
                }

                if(tumEventGetMouseY()){
                    yOffset = SCREEN_HEIGHT/12 -tumEventGetMouseY()/8;
                }
                AngleIncrementer+=0.1;
                vTaskDelay((TickType_t)20);
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
    if (xTaskCreate(vSwapBuffers, "BufferSwapTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, configMAX_PRIORITIES-1,
                    BufferSwap) != pdPASS) {
    }

    DrawSignal = xSemaphoreCreateBinary(); // Screen buffer locking
    if (!DrawSignal) {
        PRINT_ERROR("Failed to create draw signal");
    }
    ScreenLock = xSemaphoreCreateMutex();
    if (!ScreenLock) {
        PRINT_ERROR("Failed to create screen lock");
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
