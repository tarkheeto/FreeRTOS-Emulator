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
#include "timers.h"
#include "TUM_Ball.h"
#include "TUM_Draw.h"
#include "TUM_Event.h"
#include "TUM_Sound.h"
#include "TUM_Utils.h"
#include "TUM_Font.h"

#include "AsyncIO.h"
#define STACK_SIZE 4
// STATE MACHINE DEFS COPIED FROM THE MASTER DEMO 
#define STATE_QUEUE_LENGTH 1

#define STATE_COUNT 2

#define STATE_ONE 0
#define STATE_TWO 1

#define NEXT_TASK 0
#define PREV_TASK 1

#define STARTING_STATE STATE_ONE

#define STATE_DEBOUNCE_DELAY 300
static TaskHandle_t LeftNumber = NULL;
static TaskHandle_t RightNumber = NULL;
static xTimerHandle Timer1 = NULL;
static xTimerHandle Timer2 = NULL;
static xTimerHandle Timer3 = NULL;
static TaskHandle_t Exercise3SusRes=NULL;
static SemaphoreHandle_t SusRes3;
/* configSUPPORT_STATIC_ALLOCATION is set to 1, so the application must provide an
implementation of vApplicationGetIdleTaskMemory() to provide the memory that is
used by the Idle task. */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer,
                                    StackType_t **ppxIdleTaskStackBuffer,
                                    uint32_t *pulIdleTaskStackSize )
{
/* If the buffers to be provided to the Idle task are declared inside this
function then they must be declared static - otherwise they will be allocated on
the stack and so not exists after this function exits. */
static StaticTask_t xIdleTaskTCB;

static StackType_t uxIdleTaskStack[ configMINIMAL_STACK_SIZE ];

    /* Pass out a pointer to the StaticTask_t structure in which the Idle task's
    state will be stored. */
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;

    /* Pass out the array that will be used as the Idle task's stack. */
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;

    /* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
    Note that, as the array is necessarily of type StackType_t,
    configMINIMAL_STACK_SIZE is specified in words, not bytes. */
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
/*-----------------------------------------------------------*/

/* configSUPPORT_STATIC_ALLOCATION and configUSE_TIMERS are both set to 1, so the
application must provide an implementation of vApplicationGetTimerTaskMemory()
to provide the memory that is used by the Timer service task. */
void vApplicationGetTimerTaskMemory( StaticTask_t **ppxTimerTaskTCBBuffer,
                                     StackType_t **ppxTimerTaskStackBuffer,
                                     uint32_t *pulTimerTaskStackSize )
{
/* If the buffers to be provided to the Timer task are declared inside this
function then they must be declared static - otherwise they will be allocated on
the stack and so not exists after this function exits. */
static StaticTask_t xTimerTaskTCB;
static StackType_t uxTimerTaskStack[ configTIMER_TASK_STACK_DEPTH ];

    /* Pass out a pointer to the StaticTask_t structure in which the Timer
    task's state will be stored. */
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;

    /* Pass out the array that will be used as the Timer task's stack. */
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;

    /* Pass out the size of the array pointed to by *ppxTimerTaskStackBuffer.
    Note that, as the array is necessarily of type StackType_t,
    configTIMER_TASK_STACK_DEPTH is specified in words, not bytes. */
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
typedef struct Exercise3VariableIncrementationStruct {
    int LeftNumber;
    int RightNumber;
    int Variable;
    int Variable2;
    SemaphoreHandle_t lock;
} Exercise3VariableIncrementationStruct_t;
Exercise3VariableIncrementationStruct_t Exercise3VariableIncrementationStruct;
static SemaphoreHandle_t RightButtonSignal=NULL;

const unsigned char next_state_signal = NEXT_TASK;
const unsigned char prev_state_signal = PREV_TASK;
static TaskHandle_t StateMachine = NULL;
static QueueHandle_t StateQueue = NULL;
//////////////////////////
#define mainGENERIC_PRIORITY (tskIDLE_PRIORITY)
#define mainGENERIC_STACK_SIZE ((unsigned short)2560)
#define KEYCODE(CHAR) SDL_SCANCODE_##CHAR
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
static TaskHandle_t LeftCircleHandle = NULL;
static TaskHandle_t RightCircleHandle = NULL;
static TaskHandle_t DrawingTask_Handle = NULL;
static TaskHandle_t PositionIncrementationTask_Handle = NULL;
static TaskHandle_t BufferSwap = NULL;
static TaskHandle_t Exercise3 = NULL;
StaticTask_t LeftCircleBuffer;
StackType_t LeftCircleStack[ STACK_SIZE ];

static SemaphoreHandle_t DrawSignal = NULL;
static SemaphoreHandle_t ScreenLock = NULL;


typedef struct buttons_buffer {
    unsigned char buttons[SDL_NUM_SCANCODES];
    unsigned char lastButtonState[SDL_NUM_SCANCODES];
    TickType_t  lastChange[SDL_NUM_SCANCODES];
    SemaphoreHandle_t lock;
} buttons_buffer_t;

static buttons_buffer_t buttons = { 0 };

// STATE MACHINE CODE 
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
                    if (Exercise3) {
                        vTaskSuspend(Exercise3);
                    }
                    if (PositionIncrementationTask_Handle) {
                        vTaskResume(PositionIncrementationTask_Handle);
                    }
                    if (DrawingTask_Handle) {
                        vTaskResume(DrawingTask_Handle);
                    }
                    break;
                case STATE_TWO:
                    if (DrawingTask_Handle) {
                        vTaskSuspend(DrawingTask_Handle);
                    }
                    if (PositionIncrementationTask_Handle) {
                        vTaskSuspend(PositionIncrementationTask_Handle);
                    }
                    if (Exercise3) {
                        vTaskResume(Exercise3);
                    }
                    break;
                default:
                    break;
            }
            state_changed = 0;
        }
    }
}
//____________________________________________________________________________________
#define FPS_AVERAGE_COUNT 50
#define FPS_FONT "IBMPlexSans-Bold.ttf"

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
    font_handle_t cur_font = tumFontGetCurFontHandle();

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

    tumFontSelectFontFromName(FPS_FONT);

    sprintf(str, "FPS: %2d", fps);

    if (!tumGetTextSize((char *)str, &text_width, NULL))
        checkDraw(tumDrawText(str, SCREEN_WIDTH - text_width - 10,
                              SCREEN_HEIGHT - DEFAULT_FONT_SIZE * 1.5,
                              Skyblue),
                  __FUNCTION__);

    tumFontSelectFontFromHandle(cur_font);
    tumFontPutFontHandle(cur_font);
}
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


void vTimer1(TimerHandle_t xTimer){
    if(xSemaphoreTake(Exercise3VariableIncrementationStruct.lock, portMAX_DELAY) == pdTRUE){
    Exercise3VariableIncrementationStruct.Variable++;}
    xSemaphoreGive(Exercise3VariableIncrementationStruct.lock); 
}
void vTimer2(TimerHandle_t xTimer){
    if(xSemaphoreTake(Exercise3VariableIncrementationStruct.lock, portMAX_DELAY) == pdTRUE){
    Exercise3VariableIncrementationStruct.LeftNumber=0;
    Exercise3VariableIncrementationStruct.RightNumber=0;
    }
    xSemaphoreGive(Exercise3VariableIncrementationStruct.lock); 
}
void vTimer3(TimerHandle_t xTimer){
 xSemaphoreGive(SusRes3);
}
void vExercise3SusRes(void *pvParameters){
    while(1){
        if(xSemaphoreTake(SusRes3,portMAX_DELAY)==pdTRUE){ 
            if(xSemaphoreTake(Exercise3VariableIncrementationStruct.lock, portMAX_DELAY) == pdTRUE){
            Exercise3VariableIncrementationStruct.Variable2++;
            }
            xSemaphoreGive(Exercise3VariableIncrementationStruct.lock);
        }
        //xSemaphoreGive(SusRes3);
    }
}
void vExercise3DrawButton()   
{   int LeftNum =0;
    int RightNum=0;
    int Variable=0;
    int Variable2=0;
    static char str[100] = { 0 };
    if(xSemaphoreTake(Exercise3VariableIncrementationStruct.lock, portMAX_DELAY) == pdTRUE){
    LeftNum= Exercise3VariableIncrementationStruct.LeftNumber;
    RightNum= Exercise3VariableIncrementationStruct.RightNumber;
    Variable= Exercise3VariableIncrementationStruct.Variable;
    Variable2=Exercise3VariableIncrementationStruct.Variable2;
    xSemaphoreGive(Exercise3VariableIncrementationStruct.lock); 
    }
    sprintf(str, "Left Button: %d | Right Button: %d | Variable from timer : %d | Variable from Task Sus/Res: %d ",
            LeftNum,
            RightNum,
            Variable,
            Variable2);
    checkDraw(tumDrawText(str, 10, DEFAULT_FONT_SIZE * 3, TUMBlue),
                __FUNCTION__); 
    sprintf(str, "Press Left and Right arrow keys to increment the variables");
    checkDraw(tumDrawText(str, 10, 400, TUMBlue),
                __FUNCTION__); 
    sprintf(str, "To pause the timer incremented variable press T");
    checkDraw(tumDrawText(str, 10, 415, TUMBlue),
                __FUNCTION__); 
    sprintf(str, "To pause the Task suspend/resume incremented variable press X");
    checkDraw(tumDrawText(str, 10, 430, TUMBlue),
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
                tumDrawSetGlobalXOffset(0);
                tumDrawSetGlobalYOffset(0);
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
                vDrawFPS();
                xSemaphoreGive(ScreenLock);
            }

        vTaskDelay(20);

      vCheckStateInput();
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
            vCheckStateInput();

}
#define LBI 0x001
int debugvarr222 =0;
void vLeftNumber(void *pvParameters){
uint32_t NotificationBuffer;
    while(1){
        NotificationBuffer = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (NotificationBuffer & LBI){
            if(xSemaphoreTake(Exercise3VariableIncrementationStruct.lock, portMAX_DELAY) == pdTRUE){
                Exercise3VariableIncrementationStruct.LeftNumber++;
                xSemaphoreGive(Exercise3VariableIncrementationStruct.lock); debugvarr222++;
            }
        }
        
    }
}
void vRightNumber(void *pvParameters){

    while(1){
        if (xSemaphoreTake(RightButtonSignal,portMAX_DELAY) == pdTRUE){
            if(xSemaphoreTake(Exercise3VariableIncrementationStruct.lock, portMAX_DELAY) == pdTRUE){
                Exercise3VariableIncrementationStruct.RightNumber++;
                xSemaphoreGive(Exercise3VariableIncrementationStruct.lock);
            }
        }
    }
}
#define toggleRightCircle    0x0001 //Bit 0 toggles the right circle
#define toggleLeftCircle   0x0002   //Bit 1 roggles the left cicle
int vLeftCircleDebug = 0;
void vLeftCircle(void *pvParameters){
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(500);
    //xLastWakeTime = xTaskGetTickCount();
    while(1){
        xLastWakeTime = xTaskGetTickCount();
        xTaskNotify(Exercise3, toggleLeftCircle, eSetBits);   //Tell the drawing task to toggle the right circle
        vLeftCircleDebug++;
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}
void vRightCircle(void *pvParameters)
{
TickType_t xLastWakeTime;
const TickType_t xFrequency = pdMS_TO_TICKS(250);
    while(1){

         xLastWakeTime = xTaskGetTickCount();
        xTaskNotify(Exercise3, toggleRightCircle, eSetBits);   //Tell the drawing task to toggle the right circle
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        //printf("toggled right circle!\n");
    }
}
int debugVar = 0;
int debugVarrunningcheck=0;
void vExercise3(void *pvParameters){
    uint32_t NotificationBuffer;
    bool RightCircleFlag = false;
    bool LeftCircleFlag = false;
    xTimerStart(Timer1, 0);
    xTimerStart(Timer2, 0);
    xTimerStart(Timer3, 0);
    bool timer3Flag =true;
    bool timer1Flag = true;
    while (1)
    {
    tumEventFetchEvents(FETCH_EVENT_BLOCK |
                                FETCH_EVENT_NO_GL_CHECK);
    xGetButtonInput(); // Update global input
    NotificationBuffer = ulTaskNotifyTake(pdTRUE, 0);    //Recieve whatever is in the queue and clear it    
    if (xSemaphoreTake(DrawSignal, portMAX_DELAY) ==
                pdTRUE) {            
                xSemaphoreTake(ScreenLock, portMAX_DELAY);
                tumDrawSetGlobalXOffset(0);
                tumDrawSetGlobalYOffset(0);
                tumDrawClear(White); // Clear screen
                    if(NotificationBuffer & toggleRightCircle){         //Toggle the Corrosponding Circle if it's time
                        RightCircleFlag = !RightCircleFlag;
                    }
                
                    if(NotificationBuffer & toggleLeftCircle){   
                        LeftCircleFlag = !LeftCircleFlag;
                    }
                    if (LeftCircleFlag){
                        tumDrawCircle(200,200,50,Red);
                    }
                    if (RightCircleFlag){
                        tumDrawCircle(400,200,50,Blue);
                    }
                    if (ButtonStateChangeCheck(KEYCODE(LEFT))==true){xTaskNotify(LeftNumber,LBI,eSetBits);}
                    if (ButtonStateChangeCheck(KEYCODE(RIGHT))==true){xSemaphoreGive(RightButtonSignal);}
                    if (ButtonStateChangeCheck(KEYCODE(T))==true){
                        if(timer1Flag){
                            xTimerStop(Timer1,0);
                            timer1Flag=false;
                        }else{
                        timer1Flag=true;
                        xTimerStart(Timer1,0);
                        }
                    }
                    if (ButtonStateChangeCheck(KEYCODE(X))==true){
                        if(timer1Flag){
                            vTaskSuspend(Exercise3SusRes);
                            timer1Flag=false;
                        }else{
                            timer1Flag=true;
                            vTaskResume(Exercise3SusRes);
                        }
                    }     

                vDrawFPS();                    
                xSemaphoreGive(ScreenLock);
                    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                        if (buttons.buttons[KEYCODE(
                                                Q)]) { // Equiv to SDL_SCANCODE_Q
                            exit(EXIT_SUCCESS);
                        }
                    xSemaphoreGive(buttons.lock);

                    }
            }
                   
    NotificationBuffer = ulTaskNotifyTake(pdTRUE, 0);    //Recieve whatever is in the queue and clear it

    if(NotificationBuffer & toggleRightCircle){         //Toggle the Corrosponding Circle if it's time
        RightCircleFlag = !RightCircleFlag;
    }

    if(NotificationBuffer & toggleLeftCircle){   
        LeftCircleFlag = !LeftCircleFlag;
        
    }
    //printf("Notification Buffer: 0x%x\n", NotificationBuffer);
    xSemaphoreGive(DrawSignal);
    debugVarrunningcheck++;
    vExercise3DrawButton();
    vCheckStateInput();
    vTaskDelay(20);
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
    Exercise3VariableIncrementationStruct.lock = xSemaphoreCreateMutex();
    buttons.lock = xSemaphoreCreateMutex(); // Locking mechanism
    if (!buttons.lock) {
        PRINT_ERROR("Failed to create buttons lock");
        goto err_buttons_lock;      
    }
    StateQueue = xQueueCreate(STATE_QUEUE_LENGTH, sizeof(unsigned char));
    if (!StateQueue) {
        PRINT_ERROR("Could not open state queue");
        goto err_demotask;
    }

    if (xTaskCreate(basicSequentialStateMachine, "StateMachine",
                    mainGENERIC_STACK_SIZE * 2, NULL,
                    configMAX_PRIORITIES - 1, &StateMachine) != pdPASS) {
        goto err_demotask;
    }
    if (xTaskCreate(Drawing_Task, "Drawing_Task", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY, &DrawingTask_Handle) != pdPASS) {
        goto err_demotask;
    }
    if (xTaskCreate(PositionIncrementation_Task, "Position Incrementation Task", mainGENERIC_STACK_SIZE * 2, NULL,
                mainGENERIC_PRIORITY, &PositionIncrementationTask_Handle) != pdPASS) {
    goto err_demotask;
    }
    if (xTaskCreate(vExercise3, "Exercise 3 main task", mainGENERIC_STACK_SIZE * 2, NULL,
                mainGENERIC_PRIORITY, &Exercise3) != pdPASS) {
    goto err_demotask;
    }
    LeftCircleHandle= xTaskCreateStatic(vLeftCircle, "Left Circle", STACK_SIZE, NULL,
                    mainGENERIC_PRIORITY+1, LeftCircleStack,&LeftCircleBuffer); 

    if (xTaskCreate(vRightCircle, "Right Circle Task", mainGENERIC_STACK_SIZE * 2, NULL,
                mainGENERIC_PRIORITY, &RightCircleHandle) != pdPASS) {
    goto err_demotask;
    }
    if (xTaskCreate(vLeftNumber, "LeftNumb Ex3", mainGENERIC_STACK_SIZE * 2, NULL,
                mainGENERIC_PRIORITY+1, &LeftNumber) != pdPASS) {
    goto err_demotask;
    }
    if (xTaskCreate(vRightNumber, "RightNumb Ex3", mainGENERIC_STACK_SIZE * 2, NULL,
                mainGENERIC_PRIORITY+1, &RightNumber) != pdPASS) {
    goto err_demotask;
    }
    if (xTaskCreate(vExercise3SusRes, "Ex 3Suspend resume timer", mainGENERIC_STACK_SIZE * 2, NULL,
                mainGENERIC_PRIORITY+1, &Exercise3SusRes) != pdPASS) {
    goto err_demotask;
    }

    if (xTaskCreate(vSwapBuffers, "BufferSwapTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, configMAX_PRIORITIES-1,
                    BufferSwap) != pdPASS) {
    }
    SusRes3 = xSemaphoreCreateBinary();
    DrawSignal = xSemaphoreCreateBinary(); // Screen buffer locking
    if (!DrawSignal) {
        PRINT_ERROR("Failed to create draw signal");
    }
    ScreenLock = xSemaphoreCreateMutex();
    if (!ScreenLock) {
        PRINT_ERROR("Failed to create screen lock");
    }
    RightButtonSignal = xSemaphoreCreateBinary();

    Timer1 = xTimerCreate("Ex 3 timer variable",pdMS_TO_TICKS(1000),pdTRUE,0,vTimer1);
    Timer2 = xTimerCreate("Ex3 timer variable clearing",pdMS_TO_TICKS(15000),pdTRUE,0,vTimer2);
    Timer3 = xTimerCreate("Ex3 timer variable with suspend",pdMS_TO_TICKS(1000),pdTRUE,0,vTimer3);
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
