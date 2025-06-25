#include "stm32f2xx.h"
#include "math.h"
#include "stdint.h"
#include "string.h"
#include "cmsis_os.h"
#include "Driver_USART.h"
#include "Board_LED.h"
#include "stdio.h"

// === Définitions de constantes ===
#define PI 3.14159265359f
#define DAC_MAX_VALUE 4095
#define DAC_CENTER_VALUE 2047
#define SINE_TABLE_SIZE 1024
#define SINE_AMPLITUDE 2047
#define RX_BUFFER_SIZE 32
#define MAX_MSG_SIZE 32

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

// === Structures et globales ===
typedef struct {
    char message[MAX_MSG_SIZE];
} NoteMessage_t;

osMailQDef(note_mailbox, 8, NoteMessage_t);
osMailQId note_mailbox_id;

extern ARM_DRIVER_USART Driver_USART1;

unsigned short sine_table[SINE_TABLE_SIZE];
volatile float sine_index = 0.0f;
volatile float g_current_note_step = 4.8f;

uint8_t rx_char;  // <- IMPORTANT : déclaration visible partout

// === Prototypes ===
void app_main(void const *argument);
void USART1_Callback(uint32_t event);
float get_note_step_from_string(const char* note_name);
void generateSineTable(void);
void generateSine(float note_step);

osThreadDef(app_main, osPriorityNormal, 1, 0);

// === Initialisations matérielles ===
void Sound_Init(void) {
    int i;
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

    for (i = 0; i < SINE_TABLE_SIZE; i++) {
        sine_table[i] = (unsigned short)(DAC_CENTER_VALUE + SINE_AMPLITUDE * sinf((float)i * 2.0f * PI / SINE_TABLE_SIZE));
    }
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

// === Callback UART ===
void USART1_Callback(uint32_t event) {
    static char local_buffer[RX_BUFFER_SIZE];
    static uint32_t idx = 0;
    NoteMessage_t *mail;

    if (event & ARM_USART_EVENT_RECEIVE_COMPLETE) {
        if (idx < RX_BUFFER_SIZE - 1) {
            local_buffer[idx++] = rx_char;
        }

        if (rx_char == '\n') {
            local_buffer[idx] = '\0';

            mail = osMailAlloc(note_mailbox_id, 0);
            if (mail) {
                strncpy(mail->message, local_buffer, MAX_MSG_SIZE - 1);
                mail->message[MAX_MSG_SIZE - 1] = '\0';
                osMailPut(note_mailbox_id, mail);
            }

            idx = 0;
        }

        Driver_USART1.Receive(&rx_char, 1);  // relance la réception
    }
}

// === Gestion des notes ===
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
    return 0.0f;
}

// === Génération du son (DAC via Timer) ===
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

// === Tâche principale ===
void app_main(void const *argument) {
    osEvent evt;
    NoteMessage_t *mail;
    char* rx_msg;
    char* note_start;
    size_t len;
    float step;
    char tx_msg[40];
    char note_str_buffer[8];

    TIM6->CR1 |= TIM_CR1_CEN;

    while (1) {
        evt = osMailGet(note_mailbox_id, osWaitForever);
        if (evt.status == osEventMail) {
            mail = (NoteMessage_t *)evt.value.p;
            rx_msg = mail->message;

            if (((strncmp(rx_msg, "N:", 2) == 0) || (strncmp(rx_msg, "F:", 2) == 0)) && (strlen(rx_msg) > 3)) {
                note_start = &rx_msg[2];
                len = strlen(note_start);
                if (len > 0 && note_start[len - 1] == '\n') {
                    note_start[len - 1] = '\0';
                }

                strncpy(note_str_buffer, note_start, sizeof(note_str_buffer) - 1);
                note_str_buffer[sizeof(note_str_buffer) - 1] = '\0';

                if (strncmp(rx_msg, "N:", 2) == 0) {
                    step = get_note_step_from_string(note_str_buffer);
                    g_current_note_step = step;
                    sprintf(tx_msg, "Note On: %s, Step: %.2f\n", note_str_buffer, step);
                    Driver_USART1.Send(tx_msg, strlen(tx_msg));
                    LED_On(0);
                } else {
                    g_current_note_step = 0.0f;
                    sprintf(tx_msg, "Note Off: %s\n", note_str_buffer);
                    Driver_USART1.Send(tx_msg, strlen(tx_msg));
                    LED_Off(0);
                }
            }

            osMailFree(note_mailbox_id, mail);
        }
    }
}

// === main() ===
int main(void) {
    LED_Initialize();
    Sound_Init();
    Init_USART1();

    osKernelInitialize();
    note_mailbox_id = osMailCreate(osMailQ(note_mailbox), NULL);
    osThreadCreate(osThread(app_main), NULL);
    osKernelStart();

    while (1) {}
}
