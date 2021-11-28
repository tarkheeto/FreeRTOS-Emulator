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
#include "timers.h"

#include "TUM_Ball.h"
#include "TUM_Draw.h"
#include "TUM_Event.h"
#include "TUM_Sound.h"
#include "TUM_Utils.h"
#include "TUM_Font.h"
#include "TUM_Print.h"

#include "AsyncIO.h"

#define mainGENERIC_PRIORITY (tskIDLE_PRIORITY)
#define mainGENERIC_STACK_SIZE ((unsigned short)2560)
#define PI 3.14



#define STATE_QUEUE_LENGTH 1

#define STATE_COUNT 3

#define STATE_ONE 0
#define STATE_TWO 1
#define STATE_THREE 2

#define NEXT_TASK 0
#define PREV_TASK 1

#define STARTING_STATE STATE_ONE

#define STATE_DEBOUNCE_DELAY 300

#define KEYCODE(CHAR) SDL_SCANCODE_##CHAR

#define STATIC_STACK_SIZE 0


const unsigned char next_state_signal = NEXT_TASK;
const unsigned char prev_state_signal = PREV_TASK;

static TaskHandle_t StateMachine = NULL;
static TaskHandle_t Exercise2Task = NULL;
static TaskHandle_t Exercise3Task = NULL;
static TaskHandle_t CircleRight = NULL;
static TaskHandle_t CircleLeft = NULL;
static TaskHandle_t BufferSwap = NULL;
static TaskHandle_t Exercise3_Button1;
static TaskHandle_t Exercise3_Button2;
static TaskHandle_t Exercise3_Reset;
static TaskHandle_t Exercise3_Variable;
static TaskHandle_t Exercise4Task = NULL;
static TaskHandle_t Exercise4_One = NULL;
static TaskHandle_t Exercise4_Two= NULL;
static TaskHandle_t Exercise4_Three= NULL;
static TaskHandle_t Exercise4_Four= NULL;



static QueueHandle_t StateQueue = NULL;
static QueueHandle_t Exercise4Queue = NULL;

static SemaphoreHandle_t DrawSignal = NULL;
static SemaphoreHandle_t ScreenLock = NULL;
static SemaphoreHandle_t ButtonXSignal = NULL;
static SemaphoreHandle_t Exercise4Semaphore = NULL;

static TimerHandle_t Exercise3Timer = NULL;

StaticTask_t CircleLeftBuffer;
StackType_t CircleLeftStack[ STATIC_STACK_SIZE ];



typedef struct buttons_buffer {
    unsigned char buttons[SDL_NUM_SCANCODES];
    unsigned char lastButtonState[SDL_NUM_SCANCODES];
    TickType_t  last_change[SDL_NUM_SCANCODES];
    SemaphoreHandle_t lock;
} buttons_buffer_t;

static buttons_buffer_t buttons = { 0 };

typedef struct exercise_3_counter {
    int numX;
    int numY;
    int variable;
    SemaphoreHandle_t lock;
} exercise_3_counter_t;

typedef struct exercise_4_struct {
    char id; //Used for IDing the task sending the message
    TickType_t timestamp;
}   exercise_4_struct_t;

static exercise_3_counter_t exercise_3_counter = {0};


/* configSUPPORT_STATIC_ALLOCATION is set to 1, so the application must provide an
implementation of vApplicationGetIdleTaskMemory() to provide the memory that is
used by the Idle task. */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer,
                                    StackType_t **ppxIdleTaskStackBuffer,
                                    uint32_t *pulIdleTaskStackSize )
{
/* If the buffers to be provided to the Idle task are declared inside this
function then they must be declared static – otherwise they will be allocated on
the stack and so not exists after this function exits. */
static StaticTask_t xIdleTaskTCB;
static StackType_t uxIdleTaskStack[ configMINIMAL_STACK_SIZE ];

    /* Pass out a pointer to the StaticTask_t structure in which the Idle task’s
    state will be stored. */
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;

    /* Pass out the array that will be used as the Idle task’s stack. */
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;

    /* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
    Note that, as the array is necessarily of type StackType_t,
    configMINIMAL_STACK_SIZE is specified in words, not bytes. */
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
/*———————————————————–*/

void vApplicationGetTimerTaskMemory( StaticTask_t **ppxTimerTaskTCBBuffer,
                                     StackType_t **ppxTimerTaskStackBuffer,
                                     uint32_t *pulTimerTaskStackSize )
{
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[ configTIMER_TASK_STACK_DEPTH ];
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
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

static int vCheckStateInput(void)
{
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
        if (buttons.buttons[KEYCODE(E)]) {
            buttons.buttons[KEYCODE(E)] = 0;
            if (StateQueue) {
                xSemaphoreGive(buttons.lock);
                xQueueSend(StateQueue, &next_state_signal, 0);
                return 0;
            }
            return -1;
        }
        xSemaphoreGive(buttons.lock);
    }

    return 0;
}

#define FPS_AVERAGE_COUNT 50

void vDrawFPS(void)
{
    static unsigned int periods[FPS_AVERAGE_COUNT] = { 0 };
    static unsigned int periods_total = 0;
    static unsigned int index = 0;
    static unsigned int average_count = 0;
    static TickType_t xLastWakeTime = 0, prevWakeTime = 0;
    static char str[10] = { 0 };
    static int text_width;
    int fps = 0;

    if (average_count < FPS_AVERAGE_COUNT) {
        average_count++;
    }
    else {
        periods_total -= periods[index];
    }

    xLastWakeTime = xTaskGetTickCount();

    if (prevWakeTime != xLastWakeTime) {
        periods[index] =
            configTICK_RATE_HZ / (xLastWakeTime - prevWakeTime);
        prevWakeTime = xLastWakeTime;
    }
    else {
        periods[index] = 0;
    }

    periods_total += periods[index];

    if (index == (FPS_AVERAGE_COUNT - 1)) {
        index = 0;
    }
    else {
        index++;
    }

    fps = periods_total / average_count;

  //  tumFontSelectFontFromName(FPS_FONT);

    sprintf(str, "FPS: %2d", fps);

    if (!tumGetTextSize((char *)str, &text_width, NULL))
        checkDraw(tumDrawText(str, SCREEN_WIDTH - text_width - 20,
                              SCREEN_HEIGHT - DEFAULT_FONT_SIZE * 2,
                              Skyblue),
                  __FUNCTION__);

}

/*
 * Changes the state, either forwards of backwards
 */
void changeState(volatile unsigned char *state, unsigned char forwards)
{
    switch (forwards) {
        case NEXT_TASK:
            if (*state == STATE_COUNT - 1) {
                *state = 0;
            }
            else {
                (*state)++;
            }
            break;
        case PREV_TASK:
            if (*state == 0) {
                *state = STATE_COUNT - 1;
            }
            else {
                (*state)--;
            }
            break;
        default:
            break;
    }
}

/*
 * Example basic state machine with sequential states
 */
void basicSequentialStateMachine(void *pvParameters)
{
    unsigned char current_state = STARTING_STATE; // Default state
    unsigned char state_changed =
        1; // Only re-evaluate state if it has changed
    unsigned char input = 0;

    const int state_change_period = STATE_DEBOUNCE_DELAY;

    TickType_t last_change = xTaskGetTickCount();

    while (1) {
        if (state_changed) {
            goto initial_state;
        }

        // Handle state machine input
        if (StateQueue)
            if (xQueueReceive(StateQueue, &input, portMAX_DELAY) ==
                pdTRUE)
                if (xTaskGetTickCount() - last_change >
                    state_change_period) {
                    changeState(&current_state, input);
                    state_changed = 1;
                    last_change = xTaskGetTickCount();
                }

initial_state:
        // Handle current state
        if (state_changed) {
            switch (current_state) {
                case STATE_ONE:
                    if (Exercise3Task) {
                        vTaskSuspend(Exercise3Task);
                    }
                    if (Exercise4Task) {
                        vTaskSuspend(Exercise4Task);
                    }
                    if (CircleRight) {
                        vTaskSuspend(CircleRight);
                    }
                    if (CircleLeft) {
                        vTaskSuspend(CircleLeft);
                    }
                    if (Exercise3_Button1) {
                        vTaskSuspend(Exercise3_Button1);
                    }
                    if (Exercise3_Button2) {
                        vTaskSuspend(Exercise3_Button2);
                    }
                    if (Exercise3_Reset)   {
                        vTaskSuspend(Exercise3_Reset);
                    }
                    if (Exercise3Timer) {
                        xTimerStop(Exercise3Timer, 0);
                    }
                    if (Exercise3_Variable) {
                        vTaskSuspend(Exercise3_Variable);
                    }
                    if (Exercise4_One) {
                        vTaskSuspend(Exercise4_One);
                    }  
                    if (Exercise4_Two) {
                        vTaskSuspend(Exercise4_Two);
                    }  
                    if (Exercise4_Three) {
                        vTaskSuspend(Exercise4_Three);
                    }  
                    if (Exercise4_Four) {
                        vTaskSuspend(Exercise4_Four);
                    }   
                    if (Exercise2Task) {
                        vTaskResume(Exercise2Task);
                    }
                    break;
                case STATE_TWO:
                    if (Exercise2Task) {
                        vTaskSuspend(Exercise2Task);
                    }
                    if (Exercise4Task) {
                        vTaskSuspend(Exercise3Task);
                    }
                    if (Exercise4_One) {
                        vTaskSuspend(Exercise4_One);
                    }  
                    if (Exercise4_Two) {
                        vTaskSuspend(Exercise4_Two);
                    }  
                    if (Exercise4_Three) {
                        vTaskSuspend(Exercise4_Three);
                    }  
                    if (Exercise4_Four) {
                        vTaskSuspend(Exercise4_Four);
                    }    
                    if (Exercise3Task) {
                        vTaskResume(Exercise3Task);
                    }
                    if (CircleRight) {
                        vTaskResume(CircleRight);
                    }
                    if (CircleLeft) {
                        vTaskResume(CircleLeft);
                    }
                    if (Exercise3_Button1) {
                        vTaskResume(Exercise3_Button1);
                    }
                    if (Exercise3_Button2) {
                        vTaskResume(Exercise3_Button2);
                    }
                    if (Exercise3_Reset)   {
                        vTaskResume(Exercise3_Reset);
                    }
                    if (Exercise3Timer) {
                        xTimerStart(Exercise3Timer, 0);
                    }
                    if (Exercise3_Variable) {
                        vTaskResume(Exercise3_Variable);
                    }
                    break;
                case STATE_THREE:
                    if (Exercise2Task) {
                        vTaskSuspend(Exercise2Task);
                    }
                    if (Exercise3Task) {
                        vTaskSuspend(Exercise3Task);
                    }
                    if (CircleRight) {
                        vTaskSuspend(CircleRight);
                    }
                    if (CircleLeft) {
                        vTaskSuspend(CircleLeft);
                    }
                    if (Exercise3_Button1) {
                        vTaskSuspend(Exercise3_Button1);
                    }
                    if (Exercise3_Button2) {
                        vTaskSuspend(Exercise3_Button2);
                    }
                    if (Exercise3_Reset)   {
                        vTaskSuspend(Exercise3_Reset);
                    }
                    if (Exercise3Timer) {
                        xTimerStop(Exercise3Timer, 0);
                    }
                    if (Exercise3_Variable) {
                        vTaskSuspend(Exercise3_Variable);
                    }
                    if (Exercise4Task) {
                        vTaskResume(Exercise4Task);
                    }
                    // if (Exercise4_One) {
                    //     vTaskResume(Exercise4_One);
                    // }  
                    // if (Exercise4_Two) {
                    //     vTaskResume(Exercise4_Two);
                    // }  
                    // if (Exercise4_Three) {
                    //     vTaskResume(Exercise4_Three);
                    // }  
                    // if (Exercise4_Four) {
                    //     vTaskResume(Exercise4_Four);
                    // }  
                    break;
                default:
                    break;
            }
            state_changed = 0;
        }
    }
}



void calculateTriangleCoordinates(coord_t  *coordinates)
{
    int triangleOffset = 25;
    coordinates[0].x = SCREEN_WIDTH/2 - triangleOffset;
    coordinates[0].y = SCREEN_HEIGHT/2 + triangleOffset;
    coordinates[1].x = SCREEN_WIDTH/2 + triangleOffset;
    coordinates[1].y = SCREEN_HEIGHT/2 + triangleOffset;
    coordinates[2].x = SCREEN_WIDTH/2;
    coordinates[2].y = SCREEN_HEIGHT/2 - triangleOffset;
}

void calculateCircleCoordinates(signed short* coordinateX, signed short* coordinateY, float angle  )
{
    unsigned short movementRadius = 75;
    *coordinateX = SCREEN_WIDTH/2 + cos(angle)*movementRadius;
    *coordinateY = SCREEN_HEIGHT/2 + sin(angle)*movementRadius;

}
void caclulateSquareCoordinates(signed short* coordinateX, signed short* coordinateY, float angle)
{
    unsigned short movementRadius = 75;
    *coordinateX = SCREEN_WIDTH/2 + cos(angle)*movementRadius - 20;
    *coordinateY = SCREEN_HEIGHT/2 + sin(angle)*movementRadius- 20;
}


void updateMovingTextPosition(signed short* coordinateX, signed short* coordinateY, signed short* movingTextState)
{
    switch (*movingTextState){
    case 1: { *coordinateX=*coordinateX+5;  break;}
    case 2: { *coordinateX=*coordinateX-5;  break;}
    }

    if (*coordinateX>=480)
        *movingTextState=2;

    if (*coordinateX<=0)
        *movingTextState =1;
}




void vDrawButtonText(int* numPresses)
{
    
    static char str[100] = { 0 };

    sprintf(str, "Mouse X: %5d | Mouse Y : %5d", tumEventGetMouseX(),
            tumEventGetMouseY());

    checkDraw(tumDrawText(str, 10, DEFAULT_FONT_SIZE * 0.5, Black),
              __FUNCTION__);

    sprintf(str, "A: %d | B: %d |C : %d | D: %d",
            numPresses[KEYCODE(A)],
            numPresses[KEYCODE(B)],
            numPresses[KEYCODE(C)],
            numPresses[KEYCODE(D)]);
    
    checkDraw(tumDrawText(str, 10, DEFAULT_FONT_SIZE * 2, Black),
                __FUNCTION__);
      

}

void xGetButtonInput(void)      //Takes a copy of the state of the buttons
{
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
        xQueueReceive(buttonInputQueue, &buttons.buttons, 0);
        xSemaphoreGive(buttons.lock);
    }
}




int checkButton(int buttonIndex)
{
    int buttonState = 0;
    int ret = 0;
    const int debounce_delay = pdMS_TO_TICKS(20);
    
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE)
    {
        buttonState = buttons.buttons[buttonIndex];
    }
    


    // compare the buttonState to its previous state
    if (buttonState != buttons.lastButtonState[buttonIndex]) {
        // if the state has changed, debounce and incremenet
        if (xTaskGetTickCount() - buttons.last_change[buttonIndex] > debounce_delay) {
            if (buttonState ==1 ) {
                // if the current state is HIGH then the button went from off to on:
                ret = 1;
            }
            buttons.last_change[buttonIndex] = xTaskGetTickCount();
        }
    }
    // save the current state as the last state, for next time through the loop
    buttons.lastButtonState[buttonIndex] = buttonState;
    xSemaphoreGive(buttons.lock);

    return ret;
}


void incrementButton(int buttonIndex, int* numPresses)
{
    if(checkButton(buttonIndex)){
        numPresses[buttonIndex]++;
    }
}

void vExercise2Task(void *pvParameters)
{

    coord_t trianglePoints[3];

    signed short circleCoordinateX;
    signed short circleCoordinateY;
    signed short circleRadius = 25;

    signed short squareCoordinateX;
    signed short squareCoordinateY;
    signed short squareWidth = 40;
    signed short squareHeight = 40;

    float angle = 0;                        //used for the circular movement around the triangle

    signed short fixedTextCoordinateX = 280;
    signed short fixedTextCoordinateY = 400;
    signed short movingTextCoordinateX = 0;
    signed short movingTextCoordinateY = 100;

    signed short movingTextState = 1;         //Used for keeping track of moving text direction

    int numPresses[SDL_NUM_SCANCODES] = {0};   

    int xOffset = 0;
    int yOffset = 0;

    unsigned int color = 0;

    while (1) {
        if (DrawSignal)
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) ==
                pdTRUE) {
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
                }
      


                xSemaphoreTake(ScreenLock, portMAX_DELAY);
                tumDrawClear(White); // Clear screen 

                //Calculating the coordinates of the shapes
                calculateTriangleCoordinates(trianglePoints);
                caclulateSquareCoordinates(&squareCoordinateX, &squareCoordinateY, angle);
                calculateCircleCoordinates(&circleCoordinateX, &circleCoordinateY, angle+PI);

                
                //Drawing the actual shapes on the screen
                tumDrawCircle(circleCoordinateX,circleCoordinateY,circleRadius, TUMBlue);
                tumDrawTriangle(trianglePoints,Red);
                tumDrawFilledBox(squareCoordinateX, squareCoordinateY,squareWidth,squareHeight, Orange);



                tumDrawText("Exercise 2", fixedTextCoordinateX, fixedTextCoordinateY, Black);
                tumDrawText("Wubba-Lubba-Dub-Dub!", movingTextCoordinateX, movingTextCoordinateY, color);
                //I used rand() to generate random colours, the flickering is not a bug

                angle = angle+0.1;              //increments the angle by a little bit so that each loop the shapes are rotating
                if (angle == PI) angle = 0;     //reseting the angle every loop to prevent overflow

                color = color+2500;     //Just paying around with the color of the moving text

                updateMovingTextPosition(&movingTextCoordinateX, &movingTextCoordinateY, &movingTextState);


                if(tumEventGetMouseX()){
                    xOffset = tumEventGetMouseX()/8 - SCREEN_WIDTH/16;
                }

                if(tumEventGetMouseY()){
                    yOffset =  tumEventGetMouseY()/8 - SCREEN_HEIGHT/16;
                }

                tumDrawSetGlobalXOffset(xOffset);
                tumDrawSetGlobalYOffset(yOffset);


                incrementButton(KEYCODE(A), numPresses);
                incrementButton(KEYCODE(B), numPresses);
                incrementButton(KEYCODE(C), numPresses);
                incrementButton(KEYCODE(D), numPresses);


                if(tumEventGetMouseLeft()){
                    for(int i = 0; i<4; i++){
                    numPresses[i] = 0;
                    }
                }
                vDrawButtonText(numPresses);

                vDrawFPS();


                xSemaphoreGive(ScreenLock);
                vCheckStateInput();
            }
    }
}

#define toggleRightCircle    0x0001 //Bit 0 toggles the right circle
#define toggleLeftCircle   0x0002   //Bit 1 roggles the left cicle


void vCircleRight(void *pvParameters)
{
TickType_t xLastWakeTime;
const TickType_t xFrequency = pdMS_TO_TICKS(250);
    
    xLastWakeTime = xTaskGetTickCount();
    while(1){

        
        xTaskNotify(Exercise3Task, toggleRightCircle, eSetBits);   //Tell the drawing task to toggle the right circle
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        //printf("toggled right circle!\n");
    }
}

void vCircleLeft(void *pvParameters)
{
TickType_t xLastWakeTime;
const TickType_t xFrequency = pdMS_TO_TICKS(500);
    
    xLastWakeTime = xTaskGetTickCount();
    while(1){

        xTaskNotify(Exercise3Task, toggleLeftCircle, eSetBits);   //Tell the drawing task to toggle the right circle
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        //printf("toggled left circle!\n");
    }
}

#define INCREMENT_Y 0x01

void vExercise3_Button1(void *pvParameters)
{
    uint32_t NotificationBuffer;
    
    while(1){
        NotificationBuffer = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (NotificationBuffer | INCREMENT_Y){
            if(xSemaphoreTake(exercise_3_counter.lock, portMAX_DELAY) == pdTRUE){
                exercise_3_counter.numY++;
                xSemaphoreGive(exercise_3_counter.lock);
            }
        }
        
    }
}

void vExercise3_Button2(void *pvParameters)
{
    while(1){
        if (xSemaphoreTake(ButtonXSignal,portMAX_DELAY) == pdTRUE){
            if(xSemaphoreTake(exercise_3_counter.lock, portMAX_DELAY) == pdTRUE){
                exercise_3_counter.numX++;
                xSemaphoreGive(exercise_3_counter.lock);
            }
        }
    }

}

void vExercise3_Reset(void *pvParameters)
{

    while(1){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if(xSemaphoreTake(exercise_3_counter.lock, portMAX_DELAY) == pdTRUE){
            exercise_3_counter.numX = 0;
            exercise_3_counter.numY = 0;
            xSemaphoreGive(exercise_3_counter.lock);
        }
    }

}

void vExercise3Timer( TimerHandle_t xTimer)
{
     xTaskNotify(Exercise3_Reset, 0x01, eSetBits);
}

void vExercise3_Variable( void * pvParameters)
{
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(1000);

    xLastWakeTime = xTaskGetTickCount();

    while(1){
        xLastWakeTime = xTaskGetTickCount();
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        if(xSemaphoreTake(exercise_3_counter.lock, portMAX_DELAY) == pdTRUE){
            exercise_3_counter.variable++;
            xSemaphoreGive(exercise_3_counter.lock);
        }

    }
}

void vExercise3Task(void *pvParameters)
{

    uint32_t NotificationBuffer; //Buffer to hold task notification with which different drawing flags are set
    int RightCircleFlag = 0;
    int LeftCircleFlag = 0;
    char str1[100] = {0};
    char str2[100] = {0};
    
    int variableFlag = 1;
    while (1) {
        if (DrawSignal)
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) ==
                pdTRUE) {
                tumEventFetchEvents(FETCH_EVENT_BLOCK |
                                    FETCH_EVENT_NO_GL_CHECK);
                xGetButtonInput(); // Update global input

                xSemaphoreTake(ScreenLock, portMAX_DELAY);


                tumDrawSetGlobalXOffset(0);
                tumDrawSetGlobalYOffset(0);
                tumDrawClear(White); // Clear screen 
                vDrawFPS();

                tumDrawText("Exercise 3", 280, 400, Black);

                NotificationBuffer = ulTaskNotifyTake(pdTRUE, 0);    //Recieve whatever is in the queue and clear it

                if(NotificationBuffer & toggleRightCircle){         //Toggle the Corrosponding Circle if it's time
                    RightCircleFlag = !RightCircleFlag;
                }
                
                if(NotificationBuffer & toggleLeftCircle){   
                    LeftCircleFlag = !LeftCircleFlag;
                }
                //printf("Notification Buffer: 0x%x\n", NotificationBuffer);

                xSemaphoreTake(ButtonXSignal, 0);

                if(RightCircleFlag)
                    tumDrawCircle(SCREEN_WIDTH*2/3, SCREEN_HEIGHT/2, 35, Orange);
                
                if(LeftCircleFlag)
                    tumDrawCircle(SCREEN_WIDTH/3, SCREEN_HEIGHT/2, 35, Blue);

                if(checkButton(KEYCODE(Y))){    //If X is pressed
                    xTaskNotify(Exercise3_Button1, INCREMENT_Y, eSetBits);
                }
                if(checkButton(KEYCODE(X))){
                    xSemaphoreGive(ButtonXSignal);
                }

                if(checkButton(KEYCODE(Z))){    //Toggle flag if Z is pressed
                    variableFlag = !variableFlag;
                }

                if(variableFlag){
                    vTaskResume(Exercise3_Variable);
                }
                else
                {
                    vTaskSuspend(Exercise3_Variable);
                }
                


                if(xSemaphoreTake(exercise_3_counter.lock, portMAX_DELAY) == pdTRUE){
                    sprintf(str1, "Counter for X:%d        Counter for Y: %d",exercise_3_counter.numX, exercise_3_counter.numY );
                    sprintf(str2, "Variable Count: %d (Press Z to toggle)", exercise_3_counter.variable);
                    xSemaphoreGive(exercise_3_counter.lock);
                }



                tumDrawText(str1, SCREEN_WIDTH/2 - 130, SCREEN_HEIGHT/3, Black);

                tumDrawText(str2, SCREEN_WIDTH/2 - 130, SCREEN_HEIGHT/3 + 15, Black);

                xSemaphoreGive(ScreenLock);
                vCheckStateInput();
            }
    }
}

void vExercise4_One(void *pvParameters){
    char id = '1';
    TickType_t xLastWakeTime = xTaskGetTickCount();
    exercise_4_struct_t message;
    while (1)
    {
        message.id = id;
        message.timestamp = xTaskGetTickCount();
        xQueueSend(Exercise4Queue, &message, 0);
        //xTaskNotify(Exercise4Task, BIT0, eSetBits);
        vTaskDelayUntil(&xLastWakeTime,1);
    }
    

}
void vExercise4_Two(void *pvParameters){
    char id = '2';
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = 2;
    exercise_4_struct_t message;
    while (1)
    {
        message.id = id;
        message.timestamp = xTaskGetTickCount();
        xQueueSend(Exercise4Queue, &message, 0);
        //xTaskNotify(Exercise4Task, BIT1, eSetBits);
        xSemaphoreGive(Exercise4Semaphore);
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
    
}
void vExercise4_Three(void *pvParameters){
    char id = '3';
    exercise_4_struct_t message;
    while (1)
    {
        xSemaphoreTake(Exercise4Semaphore, portMAX_DELAY);
        message.id = id;
        message.timestamp = xTaskGetTickCount();
        xQueueSend(Exercise4Queue, &message, 0);
        //xTaskNotify(Exercise4Task, BIT2, eSetBits);

    }
    
}
void vExercise4_Four(void *pvParameters){
    char id = '4';
    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = 4;
    exercise_4_struct_t message;
    while (1)
    {
        message.id = id;
        message.timestamp = xTaskGetTickCount();
        xQueueSend(Exercise4Queue, &message, 0);
        //xTaskNotify(Exercise4Task, BIT3, eSetBits);
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
    
}


void vExercise4Task(void *pvParameters)
{

    exercise_4_struct_t buffer;

    TickType_t Ex4WakeupTime = xTaskGetTickCount();


    vTaskResume(Exercise4_One);
    vTaskResume(Exercise4_Two);
    vTaskResume(Exercise4_Three);
    vTaskResume(Exercise4_Four);

    vTaskDelayUntil(&Ex4WakeupTime, 16);     //Let the other tasks run for 15 ticks and wake up on the 16th tick

    vTaskSuspend(Exercise4_One);
    vTaskSuspend(Exercise4_Two);
    vTaskSuspend(Exercise4_Three);
    vTaskSuspend(Exercise4_Four);

    char exercise_4_buffer[15][100] = {0};
    char str[100];

    while (1) {
        if (DrawSignal)
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) ==
                pdTRUE) {
                tumEventFetchEvents(FETCH_EVENT_BLOCK |
                                    FETCH_EVENT_NO_GL_CHECK);
                xGetButtonInput(); // Update global input
      


                xSemaphoreTake(ScreenLock, portMAX_DELAY);
                tumDrawClear(White); // Clear screen 
                vDrawFPS();
                tumDrawText("Exercise 4", 280, 400, Black);



                //Recieve the contents of the Queue and save it into the buffer
                while (xQueueReceive(Exercise4Queue, &buffer, 0) == pdTRUE){ 
                    strncat(exercise_4_buffer[buffer.timestamp - Ex4WakeupTime +16], &buffer.id, 1);
                }

                //Print the stuff from the buffer onto the screen
                for (int i = 0; i < 15; i++)
                {
                    
                    sprintf(str, "Tick: %d, Recieved:%s", i+1, exercise_4_buffer[i]);
                    tumDrawText(str, 50, 50+20*i, Black);
                }
                    

                xSemaphoreGive(ScreenLock);
                vCheckStateInput();

            }
    }
}




#define PRINT_TASK_ERROR(task) PRINT_ERROR("Failed to print task ##task");

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

    buttons.lock = xSemaphoreCreateMutex(); // Creates a Mutex and assigns it to the lock in the buttons structure
    if (!buttons.lock) {
        PRINT_ERROR("Failed to create buttons lock");
        goto err_buttons_lock;
    }


    DrawSignal = xSemaphoreCreateBinary(); // Screen buffer locking
    if (!DrawSignal) {
        PRINT_ERROR("Failed to create draw signal");
        goto err_draw_signal;
    }
    ScreenLock = xSemaphoreCreateMutex();
    if (!ScreenLock) {
        PRINT_ERROR("Failed to create screen lock");
        goto err_screen_lock;
    }


    if (xTaskCreate(vSwapBuffers, "BufferSwapTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, configMAX_PRIORITIES-1,
                    BufferSwap) != pdPASS) {
        PRINT_TASK_ERROR("BufferSwapTask");
        goto err_bufferswap;
    }

    if (xTaskCreate(vExercise2Task, "Exercise 2 Task", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY, &Exercise2Task) != pdPASS) {
        goto err_demotask;
    }

    if (xTaskCreate(vExercise3Task, "Exercise 3 Task", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY+1, &Exercise3Task) != pdPASS) {
        goto err_exercise_2;
    }

    if (xTaskCreate(vExercise4Task, "Exercise 4 Task", mainGENERIC_STACK_SIZE * 2, NULL,
                    configMAX_PRIORITIES-2, &Exercise4Task) != pdPASS) {
        goto err_exercise_3;
    }

    StateQueue = xQueueCreate(STATE_QUEUE_LENGTH, sizeof(unsigned char));
    if (!StateQueue) {
        PRINT_ERROR("Could not open state queue");
        goto err_state_queue;
    }
    if (xTaskCreate(basicSequentialStateMachine, "StateMachine",
                    mainGENERIC_STACK_SIZE * 2, NULL,
                    configMAX_PRIORITIES - 1, StateMachine) != pdPASS) {
        PRINT_TASK_ERROR("StateMachine");
        goto err_statemachine;
    }
    if (xTaskCreate(vCircleRight, "Right Circle", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY+1, &CircleRight) != pdPASS) {
        goto err_circleright;
    }
    CircleLeft = xTaskCreateStatic(vCircleLeft, "Left Circle", STATIC_STACK_SIZE, NULL,
                    mainGENERIC_PRIORITY+1, CircleLeftStack,&CircleLeftBuffer);
    if(!CircleLeft){
        goto err_circleleft;
    }
    if (xTaskCreate(vExercise3_Button1, "3.2.3a", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY+1, &Exercise3_Button1) != pdPASS) {
        goto err_button1;
    }
    if (xTaskCreate(vExercise3_Button2, "3.2.3b", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY, &Exercise3_Button2) != pdPASS) {
        goto err_button2;
    }
    ButtonXSignal = xSemaphoreCreateBinary(); // Screen buffer locking
    if (!ButtonXSignal) {
        PRINT_ERROR("Failed to create Button X Semaphore");
        goto err_buttonxsemaphore;
    }

    exercise_3_counter.lock = xSemaphoreCreateMutex();
    if (!exercise_3_counter.lock) {
        PRINT_ERROR("Failed to create Exerise 3 Counter Mutex");
        goto err_exercise3_counter;
    }

    if (xTaskCreate(vExercise3_Reset, "Exercise 3 Reset", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY, &Exercise3_Reset) != pdPASS) {
        goto err_ex3reset;
    }

    Exercise3Timer = xTimerCreate("Exercise 3 Timer", pdMS_TO_TICKS(15000), pdTRUE, 0, vExercise3Timer);
    if (!Exercise3Timer){
        PRINT_ERROR("Failed to create Exerise 3 Timer");
        goto err_ex3timer;
    }

    if (xTaskCreate(vExercise3_Variable, "Exercise 3 Variable", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY, &Exercise3_Variable) != pdPASS) {
        goto err_ex3variable;
    }
    if (xTaskCreate(vExercise4_One, "Exercise 4 1", mainGENERIC_STACK_SIZE * 2, NULL,
                    1, &Exercise4_One) != pdPASS) {
        //goto err_;
    }
    if (xTaskCreate(vExercise4_Two, "Exercise 4 2", mainGENERIC_STACK_SIZE * 2, NULL,
                    2, &Exercise4_Two) != pdPASS) {
        //goto err;
    }
    if (xTaskCreate(vExercise4_Three, "Exercise 4 3", mainGENERIC_STACK_SIZE * 2, NULL,
                    3, &Exercise4_Three) != pdPASS) {
        //goto err_;
    }
    if (xTaskCreate(vExercise4_Four, "Exercise 4 4", mainGENERIC_STACK_SIZE * 2, NULL,
                    4, &Exercise4_Four) != pdPASS) {
        //goto err_;    To-do: Add error handling for ex4 tasks
    }
    Exercise4Semaphore = xSemaphoreCreateBinary(); // Screen buffer locking
    if (!Exercise4Semaphore) {
        PRINT_ERROR("Failed to create Exercise 4 Semaphore");
        //goto err_;
    }
    Exercise4Queue = xQueueCreate(100, sizeof(exercise_4_struct_t));
    if (!Exercise4Queue) {
        PRINT_ERROR("Could not open exercise 4 queue");
        //goto err_ex4_queue;
    }


    vTaskSuspend(Exercise4Task);
    vTaskSuspend(Exercise4_One);
    vTaskSuspend(Exercise4_Two);
    vTaskSuspend(Exercise4_Three);
    vTaskSuspend(Exercise4_Four);
    vTaskSuspend(Exercise2Task);
    vTaskSuspend(Exercise3Task);
    vTaskSuspend(CircleRight);
    vTaskSuspend(CircleLeft);
    vTaskSuspend(Exercise3_Button1);
    vTaskSuspend(Exercise3_Button2);
    vTaskSuspend(Exercise3_Reset);
    vTaskSuspend(Exercise3_Variable);
    
    vTaskStartScheduler();

    return EXIT_SUCCESS;
err_ex3variable:
    xTimerDelete(Exercise3Timer, portMAX_DELAY);
err_ex3timer:
    vTaskDelete(Exercise3_Reset);
err_ex3reset:
    vSemaphoreDelete(exercise_3_counter.lock);
err_exercise3_counter:
    vSemaphoreDelete(ButtonXSignal);
err_buttonxsemaphore:
    vTaskDelete(Exercise3_Button2);
err_button2:
    vTaskDelete(Exercise3_Button1);
err_button1:
    vTaskDelete(CircleLeft);
err_circleleft:
    vTaskDelete(CircleRight);
err_circleright:
    vTaskDelete(StateMachine);
err_statemachine:
    vQueueDelete(StateQueue);
err_state_queue:
    vTaskDelete(Exercise4Task);
err_exercise_3:
    vTaskDelete(Exercise3Task);
err_exercise_2:
    vTaskDelete(Exercise2Task);
err_demotask:
    vTaskDelete(BufferSwap);
err_bufferswap:
    vSemaphoreDelete(DrawSignal);
err_screen_lock:
    vSemaphoreDelete(DrawSignal);
err_draw_signal:
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
