#include <stdint.h>

#define REG32(addr)   (*(volatile uint32_t *)(addr))

/* ---- Clock enable (RCC) ---- */
#define RCC_AHB1ENR   REG32(0x40023830U)
#define RCC_APB1ENR   REG32(0x40023840U)
#define RCC_APB2ENR   REG32(0x40023844U)

/* ---- GPIOA ---- */
#define GPIOA_MODER   REG32(0x40020000U)
#define GPIOA_AFRL    REG32(0x40020020U)
#define GPIOA_BSRR    REG32(0x40020018U)

/* ---- ADC1 ---- */
#define ADC1_SR       REG32(0x40012000U)
#define ADC1_CR2      REG32(0x40012008U)
#define ADC1_SMPR2    REG32(0x40012010U)
#define ADC1_SQR1     REG32(0x4001202CU)
#define ADC1_SQR3     REG32(0x40012034U)
#define ADC1_DR       REG32(0x4001204CU)

/* ---- USART2 ---- */
#define USART2_SR     REG32(0x40004400U)
#define USART2_DR     REG32(0x40004404U)
#define USART2_BRR    REG32(0x40004408U)
#define USART2_CR1    REG32(0x4000440CU)

/* ---- SysTick ---- */
#define SYST_CSR      REG32(0xE000E010U)
#define SYST_RVR      REG32(0xE000E014U)
#define SYST_CVR      REG32(0xE000E018U)

#define NUM_SAMPLES   64U
#define FULL_SCALE_MV 16500U   /* 3.3 V reference x divider ratio 5 */
#define ADC_MAX       4095U

#define SETTLE_MS     200U     /* wait after switching, before first reading */
#define HOLD_MS       4000U    /* LED stays ON or OFF for about 4 s */
#define PRINT_MS      500U     /* a new reading every 0.5 s */

__attribute__((weak)) void SystemInit(void)
{
}

static void delay_ms(uint32_t ms)
{
  SYST_RVR = 16000U - 1U;      /* 16 MHz HSI -> 1 ms */
  SYST_CVR = 0U;
  SYST_CSR = 5U;
  while (ms--)
  {
    while ((SYST_CSR & (1U << 16)) == 0U) { }
  }
  SYST_CSR = 0U;
}

static void uart_putc(char c)
{
  while ((USART2_SR & (1U << 7)) == 0U) { }   /* wait TXE */
  USART2_DR = (uint32_t)c;
}

static void uart_puts(const char *s)
{
  while (*s)
  {
    uart_putc(*s++);
  }
}

static void uart_print_uint(uint32_t v)
{
  char buf[11];
  int i = 0;

  if (v == 0U)
  {
    uart_putc('0');
    return;
  }
  while (v > 0U)
  {
    buf[i++] = (char)('0' + (v % 10U));
    v /= 10U;
  }
  while (i > 0)
  {
    uart_putc(buf[--i]);
  }
}

static void gpio_init(void)
{
  RCC_AHB1ENR |= (1U << 0);                  /* GPIOA clock */

  GPIOA_MODER &= ~(3U << (0 * 2));
  GPIOA_MODER |=  (3U << (0 * 2));           /* PA0 analog (ADC) */

  GPIOA_MODER &= ~(3U << (8 * 2));
  GPIOA_MODER |=  (1U << (8 * 2));           /* PA8 output (D7 -> LED) */

  GPIOA_MODER &= ~(3U << (2 * 2));
  GPIOA_MODER |=  (2U << (2 * 2));           /* PA2 alternate function */
  GPIOA_AFRL  &= ~(0xFU << (2 * 4));
  GPIOA_AFRL  |=  (7U << (2 * 4));           /* AF7 = USART2_TX */
}

static void uart_init(void)
{
  RCC_APB1ENR |= (1U << 17);                 /* USART2 clock */
  USART2_BRR = 139U;                         /* 16 MHz / 115200 */
  USART2_CR1 = (1U << 13) | (1U << 3);       /* UE + TE */
}

static void adc_init(void)
{
  RCC_APB2ENR |= (1U << 8);                  /* ADC1 clock */
  ADC1_SMPR2 = 7U;                           /* channel 0: 480 cycles */
  ADC1_SQR1 = 0U;                            /* 1 conversion */
  ADC1_SQR3 = 0U;                            /* first conversion = channel 0 */
  ADC1_CR2 = (1U << 0);                      /* ADC on */
  delay_ms(2);
}

static uint32_t adc_read_average(void)
{
  uint32_t sum = 0U;

  for (uint32_t i = 0U; i < NUM_SAMPLES; i++)
  {
    ADC1_CR2 |= (1U << 30);                  /* start conversion */
    while ((ADC1_SR & (1U << 1)) == 0U) { }  /* wait EOC */
    sum += ADC1_DR;
  }
  return sum / NUM_SAMPLES;
}

static void measure_and_print(const char *state)
{
  uint32_t raw = adc_read_average();
  uint32_t pa0_mv = (raw * 3300U) / ADC_MAX;
  uint32_t led_mv = (raw * FULL_SCALE_MV) / ADC_MAX;
  uint32_t frac = led_mv % 1000U;

  uart_puts("LED ");
  uart_puts(state);
  uart_puts(" | ADC=");
  uart_print_uint(raw);
  uart_puts(" | PA0=");
  uart_print_uint(pa0_mv);
  uart_puts(" mV | LED voltage=");
  uart_print_uint(led_mv / 1000U);
  uart_putc('.');
  if (frac < 100U) { uart_putc('0'); }
  if (frac < 10U)  { uart_putc('0'); }
  uart_print_uint(frac);
  uart_puts(" V\r\n");
}

/* Keeps the LED in its current state and prints a reading every PRINT_MS */
static void hold_and_report(const char *state)
{
  delay_ms(SETTLE_MS);
  for (uint32_t t = SETTLE_MS; t < HOLD_MS; t += PRINT_MS)
  {
    measure_and_print(state);
    delay_ms(PRINT_MS);
  }
}

int main(void)
{
  gpio_init();
  uart_init();
  adc_init();

  while (1)
  {
    GPIOA_BSRR = (1U << 8);                  /* PA8 HIGH: LED on */
    hold_and_report("ON ");

    GPIOA_BSRR = (1U << (8 + 16));           /* PA8 LOW: LED off */
    hold_and_report("OFF");
  }
}
