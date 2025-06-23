/* Fusion Bluetooth + LED + Audio (DAC)
   STM32F2 - uVision5 - RTX RTOS
*/

#include "stdint.h"
#include "string.h"
#include "cmsis_os.h"
#include "Driver_USART.h"
#include "Board_LED.h"
#include <stm32f2xx.h>
#include <math.h>

#define PI 3.14159265359f
#define DAC_MAX_VALUE 4095
#define DAC_CENTER_VALUE 2047
#define SINE_TABLE_SIZE 1024
#define SINE_AMPLITUDE 2000

// --- Notes en indices (plus stables que fréquences directes)
#define C4 4.0f
#define D4 4.5f
#define E4 5.0f
#define F4 5.3f
#define G4 5.6f
#define A4 6.0f
#define B4 6.5f
#define C5 7.0f

// === Globales DAC
unsigned short sine_table[SINE_TABLE_SIZE];
volatile float sine_index = 0.0f;
volatile float current_note = 0.0f; // 0 = silence

// === USART
extern ARM_DRIVER_USART Driver_USART1;
osThreadDef(app_main, osPriorityNormal, 1, 0);
osSemaphoreDef(rx_semaphore);
osSemaphoreId rx_semaphore_id;

#define RX_BUFFER_SIZE 32
uint8_t rx_char;
char rx_buffer[RX_BUFFER_SIZE];
uint32_t rx_buffer_idx = 0;

// --- Sine Table
void generateSineTable(void) {
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        sine_table[i] = (unsigned short)(DAC_CENTER_VALUE + SINE_AMPLITUDE * sinf((float)i * 2.0f * PI / SINE_TABLE_SIZE));
    }
}

void generateSine(float note_index) {
    if (note_index == 0.0f) return;
    int idx = (int)sine_index % SINE_TABLE_SIZE;
    DAC->DHR12R1 = sine_table[idx];
    sine_index += note_index;
    if (sine_index >= SINE_TABLE_SIZE) sine_index -= SINE_TABLE_SIZE;
}

// --- Timer IRQ
void TIM6_DAC_IRQHandler(void) {
    if (TIM6->SR & TIM_SR_UIF) {
        TIM6->SR &= ~TIM_SR_UIF;
        generateSine(current_note);
    }
}

void Init_DAC(void) {
    RCC->APB1ENR |= RCC_APB1ENR_DACEN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;

    GPIOA->MODER |= (3 << 8); // PA4 analog

    DAC->CR |= DAC_CR_EN1;

    TIM6->PSC = 0;
    TIM6->ARR = 2; // Pour 22kHz
    TIM6->DIER |= TIM_DIER_UIE;
    NVIC_SetPriority(TIM6_DAC_IRQn, 0);
    NVIC_EnableIRQ(TIM6_DAC_IRQn);

    generateSineTable();
    TIM6->CR1 |= TIM_CR1_CEN;
}

// --- USART init ---
void Init_USART1(void) {
    Driver_USART1.Initialize(USART1_Callback);
    Driver_USART1.PowerControl(ARM_POWER_FULL);
    Driver_USART1.Control(ARM_USART_MODE_ASYNCHRONOUS |
                          ARM_USART_DATA_BITS_8 |
                          ARM_USART_PARITY_NONE |
                          ARM_USART_STOP_BITS_1 |
                          ARM_USART_FLOW_CONTROL_NONE, 115200);
    Driver_USART1.Control(ARM_USART_CONTROL_TX, 1);
    Driver_USART1.Control(ARM_USART_CONTROL_RX, 1);
    Driver_USART1.Receive(&rx_char, 1);
}

// --- USART callback ---
void USART1_Callback(uint32_t event) {
    if (event & ARM_USART_EVENT_RECEIVE_COMPLETE) {
        Driver_USART1.Send(&rx_char, 1);
        if (rx_buffer_idx < (RX_BUFFER_SIZE - 1)) {
            rx_buffer[rx_buffer_idx++] = rx_char;
        }
        if (rx_char == '\n') {
            rx_buffer[rx_buffer_idx] = '\0';
            osSemaphoreRelease(rx_semaphore_id);
            rx_buffer_idx = 0;
        }
        Driver_USART1.Receive(&rx_char, 1);
    }
}

float noteNameToIndex(const char *name) {
    if (strcmp(name, "C4") == 0) return C4;
    if (strcmp(name, "D4") == 0) return D4;
    if (strcmp(name, "E4") == 0) return E4;
    if (strcmp(name, "F4") == 0) return F4;
    if (strcmp(name, "G4") == 0) return G4;
    if (strcmp(name, "A4") == 0) return A4;
    if (strcmp(name, "B4") == 0) return B4;
    if (strcmp(name, "C5") == 0) return C5;
    return 0.0f;
}

void app_main(void const *argument) {
    char *note_name;
    size_t len;

    while (1) {
        osSemaphoreWait(rx_semaphore_id, osWaitForever);
        Driver_USART1.Send((uint8_t*)"\nMessage complet recu: ", 23);
        Driver_USART1.Send((uint8_t*)rx_buffer, strlen(rx_buffer));
        Driver_USART1.Send((uint8_t*)"\n", 1);

        note_name = &rx_buffer[2];
        len = strlen(note_name);
        if (len > 0 && note_name[len - 1] == '\n') note_name[len - 1] = '\0';

        float note_index = noteNameToIndex(note_name);

        if (strncmp(rx_buffer, "N:", 2) == 0) {
            if (note_index > 0.0f) {
                LED_On((int)(note_index - C4));
                current_note = note_index;
            }
        } else if (strncmp(rx_buffer, "F:", 2) == 0) {
            if (note_index > 0.0f) {
                LED_Off((int)(note_index - C4));
                current_note = 0.0f;
            }
        }
        memset(rx_buffer, 0, RX_BUFFER_SIZE);
    }
}

int main(void) {
    LED_Initialize();
    rx_semaphore_id = osSemaphoreCreate(osSemaphore(rx_semaphore), 0);
    Init_USART1();
    Init_DAC();

    osKernelInitialize();
    osThreadCreate(osThread(app_main), NULL);
    osKernelStart();

    while (1) {}
}
