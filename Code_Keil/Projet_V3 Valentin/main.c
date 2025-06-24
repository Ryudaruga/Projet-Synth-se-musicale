#include "stm32f2xx.h"
#include "math.h"
#include "stdint.h"
#include "string.h"
#include "cmsis_os.h"
#include "Driver_USART.h"
#include "Board_LED.h"
#include "stdio.h"

// ==============================================================================
// === SECTION 1 : Définitions et variables pour la génération de son ===
// ==============================================================================

#define PI 3.14159265359f

#define DAC_MAX_VALUE 4095
#define DAC_CENTER_VALUE 2047
#define SINE_TABLE_SIZE 1024
#define SINE_AMPLITUDE 2047

// MODIFICATION CORRECTE: Les "steps" sont divisés par deux pour correspondre à l'octave 4
#define C4      2.40f
#define CSHARP4 2.60f
#define D4      2.73f
#define DSHARP4 2.84f
#define E4      3.10f
#define F4      3.24f
#define FSHARP4 3.45f
#define G4      3.62f
#define GSHARP4 3.86f
#define A4      4.00f
#define ASHARP4 4.25f
#define B4      4.50f

unsigned short sine_table[SINE_TABLE_SIZE];
volatile float sine_index = 0.0f;
volatile float g_current_note_step = 4.8f;

// ============================================================================
// === SECTION 2 : Définitions et variables pour l'RTOS et l'USART ===
// ============================================================================

extern ARM_DRIVER_USART Driver_USART1;

void app_main(void const *argument);
void USART1_Callback(uint32_t event);
float note_step = 4.8f;
osThreadDef(app_main, osPriorityNormal, 1, 0);
osSemaphoreDef(rx_semaphore);
osSemaphoreId rx_semaphore_id;

#define RX_BUFFER_SIZE 32
uint8_t rx_char;
char rx_buffer[RX_BUFFER_SIZE];
uint32_t rx_buffer_idx = 0;

// ===============================================================
// === SECTION 3 : Fonctions pour la génération de son ===
// ===============================================================

void generateSineTable(void) {
    int i;
    for (i = 0; i < SINE_TABLE_SIZE; i++) {
        sine_table[i] = (unsigned short)(DAC_CENTER_VALUE + SINE_AMPLITUDE * sin((float)i * 2.0f * PI / SINE_TABLE_SIZE));
    }
}

void generateSine(float note_step) {
    int index_int = (int)sine_index;
    DAC->DHR12R1 = sine_table[index_int];
    sine_index += note_step;
    if (sine_index >= SINE_TABLE_SIZE) {
        sine_index -= SINE_TABLE_SIZE;
    }
}

void TIM6_DAC_IRQHandler(void) {
    if (TIM6->SR & TIM_SR_UIF) {
        TIM6->SR &= ~TIM_SR_UIF;
        if (g_current_note_step > 0.0f) {
            generateSine(g_current_note_step);
        } else {
            DAC->DHR12R1 = DAC_CENTER_VALUE;
        }
    }
}

// MODIFICATION CORRECTE: La fonction reconnaît les commandes "C4", "D4", etc.
float get_note_step_from_string(const char* note_name) {
    if (strcmp(note_name, "C4") == 0) return C4;
    if (strcmp(note_name, "C#4") == 0) return CSHARP4;
    if (strcmp(note_name, "D4") == 0) return D4;
    if (strcmp(note_name, "D#4") == 0) return DSHARP4;
    if (strcmp(note_name, "E4") == 0) return E4;
    if (strcmp(note_name, "F4") == 0) return F4;
    if (strcmp(note_name, "F#4") == 0) return FSHARP4;
    if (strcmp(note_name, "G4") == 0) return G4;
    if (strcmp(note_name, "G#4") == 0) return GSHARP4;
    if (strcmp(note_name, "A4") == 0) return A4;
    if (strcmp(note_name, "A#4") == 0) return ASHARP4;
    if (strcmp(note_name, "B4") == 0) return B4;
    
    return 0.0f; // Note inconnue
}

// ==========================================================
// === SECTION 4 : Fonctions d'initialisation et Callbacks ===
// ==========================================================

void Sound_Init(void) {
    RCC->APB1ENR |= (1 << 29);
    RCC->AHB1ENR |= (1 << 0);
    RCC->APB1ENR |= (1 << 4);
    GPIOA->MODER |= ((1 << 8) | (1 << 9));
    DAC->CR |= (1 << 0);
    TIM6->PSC = 0;
    TIM6->ARR = 2;
    TIM6->DIER |= TIM_DIER_UIE;
    NVIC_SetPriority(TIM6_DAC_IRQn, 0);
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
    generateSineTable();
}

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

void USART1_Callback(uint32_t event) {
    if (event & ARM_USART_EVENT_RECEIVE_COMPLETE) {
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

// ==============================================
// === SECTION 5 : Tâche principale RTOS et main ===
// ==============================================

void app_main(void const *argument) {
    char note_str_buffer[8];
    char tx_msg[40];
    char* note_start;
    size_t len;
    float step;
		TIM6->CR1 |= TIM_CR1_CEN;
    while (1) {
        osSemaphoreWait(rx_semaphore_id, osWaitForever);
        if (((strncmp(rx_buffer, "N:", 2) == 0) || (strncmp(rx_buffer, "F:", 2) == 0)) && (strlen(rx_buffer) > 3)) {
            note_start = &rx_buffer[2];
            len = strlen(note_start);
            if (len > 0 && note_start[len-1] == '\n') {
                note_start[len-1] = '\0';
            }
            strncpy(note_str_buffer, note_start, sizeof(note_str_buffer) - 1);
            note_str_buffer[sizeof(note_str_buffer) - 1] = '\0';
        
            if (strncmp(rx_buffer, "N:", 2) == 0) {
                step = get_note_step_from_string(note_str_buffer);
                g_current_note_step = step;
                
                sprintf(tx_msg, "Note On: %s, Step: %f\n", note_str_buffer, step);
                Driver_USART1.Send(tx_msg, strlen(tx_msg));
                LED_On(0);
                
            } else {
                g_current_note_step = 0.0f;
                
                sprintf(tx_msg, "Note Off: %s\n", note_str_buffer);
                Driver_USART1.Send(tx_msg, strlen(tx_msg));
                LED_Off(0);
            }
        }
        memset(rx_buffer, 0, RX_BUFFER_SIZE);
    }
}

int main(void) {
    LED_Initialize();
    Sound_Init();
    Init_USART1();
    osKernelInitialize();
    rx_semaphore_id = osSemaphoreCreate(osSemaphore(rx_semaphore), 0);
    osThreadCreate(osThread(app_main), NULL);
    osKernelStart();
    
    while (1) {}
}
