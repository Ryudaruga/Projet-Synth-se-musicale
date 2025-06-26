/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SINE_TABLE_SIZE 128
uint16_t sine_table[SINE_TABLE_SIZE];

/* Private variables ---------------------------------------------------------*/
DAC_HandleTypeDef hdac;
DMA_HandleTypeDef hdma_dac1;
TIM_HandleTypeDef htim6;

/* Function prototypes -------------------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_DAC_Init(void);
static void MX_TIM6_Init(void);
void Generate_Sine_Table(void);
void Set_Sine_Frequency(float freq_hz);

/* MAIN ----------------------------------------------------------------------*/
int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_DAC_Init();
  MX_TIM6_Init();

  Generate_Sine_Table();

  HAL_DAC_Start(&hdac, DAC_CHANNEL_1);
  HAL_DAC_Start_DMA(&hdac, DAC_CHANNEL_1, (uint32_t*)sine_table, SINE_TABLE_SIZE, DAC_ALIGN_12B_R);
  HAL_TIM_Base_Start(&htim6);

  Set_Sine_Frequency(750.0f); // fréquence en Hz

  while (1)
  {
    // Exemple dynamique (facultatif) :
    // Set_Sine_Frequency(880.0f);
    // HAL_Delay(1000);
    // Set_Sine_Frequency(440.0f);
    // HAL_Delay(1000);
  }
}

/* -------------------------------------------------------------------------- */
/* Génération table sinusoïdale 12 bits */
void Generate_Sine_Table(void)
{
  for (int i = 0; i < SINE_TABLE_SIZE; i++)
  {
    float angle = 2.0f * M_PI * i / SINE_TABLE_SIZE;
    sine_table[i] = (uint16_t)(2047 + 2047 * sinf(angle)); // 12-bit centered at 2047
  }
}

/* -------------------------------------------------------------------------- */
/* Changement de fréquence dynamique */
void Set_Sine_Frequency(float freq_hz)
{
  if (freq_hz <= 0.0f) return;

  float timer_clock = 60000000.0f; // 60 MHz
  float sample_rate = SINE_TABLE_SIZE * freq_hz;
  uint32_t period = (uint32_t)((timer_clock / sample_rate) - 1);

  if (period < 10) period = 10;
  if (period > 0xFFFF) period = 0xFFFF;

  __HAL_TIM_DISABLE(&htim6);
  __HAL_TIM_SET_AUTORELOAD(&htim6, period);
  __HAL_TIM_SET_COUNTER(&htim6, 0);
  __HAL_TIM_ENABLE(&htim6);
}

/* -------------------------------------------------------------------------- */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 10;
  RCC_OscInitStruct.PLL.PLLN = 225;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV6;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    Error_Handler();

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
    Error_Handler();
}

/* -------------------------------------------------------------------------- */
static void MX_DAC_Init(void)
{
  DAC_ChannelConfTypeDef sConfig = {0};

  hdac.Instance = DAC;
  if (HAL_DAC_Init(&hdac) != HAL_OK)
    Error_Handler();

  sConfig.DAC_Trigger = DAC_TRIGGER_T6_TRGO;
  sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
  if (HAL_DAC_ConfigChannel(&hdac, &sConfig, DAC_CHANNEL_1) != HAL_OK)
    Error_Handler();
}

/* -------------------------------------------------------------------------- */
static void MX_TIM6_Init(void)
{
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 0; // 60 MHz
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 12000 - 1; // Valeur initiale (modifiable via Set_Sine_Frequency)
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
    Error_Handler();

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
    Error_Handler();
}

/* -------------------------------------------------------------------------- */
static void MX_DMA_Init(void)
{
  __HAL_RCC_DMA1_CLK_ENABLE();

  HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
}

/* -------------------------------------------------------------------------- */
static void MX_GPIO_Init(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
}

/* -------------------------------------------------------------------------- */
void Error_Handler(void)
{
  __disable_irq();
  while (1) { }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif
