#include "main.h"
#include "gpio.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Private variables */
extern UART_HandleTypeDef huart3;
int gameStarted = 0;
int score = 0;
uint32_t lastTick = 0;

/* Level struct */
typedef struct {
    uint32_t interval;   // mole interval in ms
    uint32_t duration;   // level duration in ms
} Level;

Level levels[3] = {
    {5000, 60000}, // Level 1
    {3000, 60000}, // Level 2
    {1000, 60000}  // Level 3
};

int currentLevel = 0;
uint32_t levelStartTick = 0;

/* LED mapping (active-high, common cathode) */
GPIO_TypeDef* ledPorts[4] = {GPIOF, GPIOF, GPIOF, GPIOG};
uint16_t ledPins[4]  = {GPIO_PIN_8, GPIO_PIN_7, GPIO_PIN_9, GPIO_PIN_1};
// LED1=PF8, LED2=PF7, LED3=PF9, LED4=PG1

/* Button mapping (active-low) */
GPIO_TypeDef* btnPorts[4] = {GPIOE, GPIOE, GPIOB, GPIOB};
uint16_t btnPins[4] = {GPIO_PIN_14, GPIO_PIN_15, GPIO_PIN_10, GPIO_PIN_11};

/* Stop button (PC13, active-high) */
GPIO_TypeDef* stopPort = GPIOC;
uint16_t stopPin = GPIO_PIN_13;

/* Helper variables */
char buffer[256];

/* LED states for current pop-up: 0=not active, 1=active */
int moleLEDs[4] = {0,0,0,0};

/* Software debounce */
#define DEBOUNCE_MS 50
uint32_t lastPressTime[4] = {0};
uint32_t lastStopTime = 0;

void uartPrint(const char* msg) {
    HAL_UART_Transmit(&huart3, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
}

void clearLEDs(void) {
    for (int i=0;i<4;i++){
        HAL_GPIO_WritePin(ledPorts[i], ledPins[i], GPIO_PIN_RESET); // OFF
        moleLEDs[i] = 0;
    }
}

void showMoleSet(int leds[], int count) {
    clearLEDs();
    snprintf(buffer, sizeof(buffer), "\r\nMole popped on LED(s):");
    uartPrint(buffer);
    for (int i=0;i<count;i++){
        HAL_GPIO_WritePin(ledPorts[leds[i]], ledPins[leds[i]], GPIO_PIN_SET); // ON
        moleLEDs[leds[i]] = 1; // mark as active
        snprintf(buffer, sizeof(buffer), " %d", leds[i]+1);
        uartPrint(buffer);
    }
    uartPrint("\r\n");
}

/* Debounced active-low buttons */
int getPressedButton(void){
    int idx = -1;
    uint32_t now = HAL_GetTick();
    for(int i=0;i<4;i++){
        if(HAL_GPIO_ReadPin(btnPorts[i], btnPins[i])==GPIO_PIN_RESET){
            if(now - lastPressTime[i] > DEBOUNCE_MS){
                lastPressTime[i] = now;
                idx = i;
            }
        }
    }
    return idx;
}

/* Debounced stop button (PC13, active-high) */
int stopPressed(void){
    uint32_t now = HAL_GetTick();
    if(HAL_GPIO_ReadPin(stopPort, stopPin)==GPIO_PIN_SET){
        if(now - lastStopTime > DEBOUNCE_MS){
            lastStopTime = now;
            return 1;
        }
    }
    return 0;
}

void showStartPage(void){
    uartPrint("==== Whack-a-Mole Game ====\r\n");
    uartPrint("Press any play button to start!\r\n");
}

int main(void){
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART3_UART_Init();

    showStartPage();

    int currentMoles[3] = {-1,-1,-1};
    int moleCount = 1;
    lastTick = HAL_GetTick();
    levelStartTick = HAL_GetTick();
    currentLevel = 0;

    while(1){
        uint32_t now = HAL_GetTick();

        // Stop button
        if(stopPressed() && gameStarted){
            gameStarted = 0;
            clearLEDs();
            snprintf(buffer,sizeof(buffer),"\r\nGame Stopped! Total Score=%d\r\n",score);
            uartPrint(buffer);
            score = 0;
            showStartPage();
            continue;
        }

        // Start game
        if(!gameStarted){
            while(getPressedButton()==-1 && !stopPressed());
            score = 0;
            gameStarted = 1;
            currentLevel = 0;
            levelStartTick = now;
            lastTick = now;
            uartPrint("Game Started! Level 1\n");
            continue;
        }

        // Level progression
        if(now - levelStartTick >= levels[currentLevel].duration){
            currentLevel++;
            if(currentLevel >=3){
                gameStarted = 0;
                clearLEDs();
                snprintf(buffer,sizeof(buffer),"\r\nGame Finished! Total Score=%d\r\n",score);
                uartPrint(buffer);
                score = 0;
                showStartPage();
                continue;
            }else{
                levelStartTick = now;
                lastTick = now;
                snprintf(buffer,sizeof(buffer),"\r\nStarting Level %d!\r\n",currentLevel+1);
                uartPrint(buffer);
            }
        }

        // Generate new mole if interval passed or all hit
        int anyActive=0;
        for(int i=0;i<4;i++) if(moleLEDs[i]) anyActive=1;

        if(!anyActive || (now - lastTick >= levels[currentLevel].interval)){
            moleCount = 1 + rand()%3;
            for(int i=0;i<moleCount;i++){
                int led;
                do{
                    led = rand()%4;
                    int duplicate=0;
                    for(int j=0;j<i;j++) if(led==currentMoles[j]) duplicate=1;
                    if(!duplicate) break;
                }while(1);
                currentMoles[i] = led;
            }
            showMoleSet(currentMoles,moleCount);
            lastTick = now;
        }

        // Button pressed
        int pressed = getPressedButton();
        if(pressed != -1){
            if(moleLEDs[pressed]){
                // Hit
                HAL_GPIO_WritePin(ledPorts[pressed], ledPins[pressed], GPIO_PIN_RESET); // turn off LED
                moleLEDs[pressed]=0;
                score++;

                // Show remaining LEDs
                char remaining[64]="";
                for(int i=0;i<4;i++){
                    if(moleLEDs[i]){
                        char temp[8];
                        snprintf(temp,sizeof(temp)," %d",i+1);
                        strcat(remaining,temp);
                    }
                }

                snprintf(buffer,sizeof(buffer),"✅ Hit LED %d! Score=%d | Remaining LEDs:%s\r\n",
                         pressed+1,score, remaining[0]?remaining:" None");
                uartPrint(buffer);
            }else{
                snprintf(buffer,sizeof(buffer),"❌ Miss! Pressed %d\r\n",pressed+1);
                uartPrint(buffer);
            }

            // Check if all cleared -> generate next mole immediately
            int allCleared=1;
            for(int i=0;i<4;i++) if(moleLEDs[i]) allCleared=0;
            if(allCleared) lastTick = 0; // will trigger new mole set next loop
        }
    }
}

/* ------------------- STUB FUNCTIONS FOR COMPILATION ------------------ */

void SystemClock_Config(void) {
    // Replace with CubeMX-generated clock config for real use
}

void Error_Handler(void) {
    while(1); // hang on error
}
