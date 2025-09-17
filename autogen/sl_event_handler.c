#include "sl_event_handler.h"

#include "rsi_nvic_priorities_config.h"
#include "sl_si91x_clock_manager.h"
#include "sli_siwx917_soc.h"
#include "rsi_board.h"
#include "rsi_debug.h"
#include "sl_i2c_instances.h"
#include "sl_si91x_iostream_debug.h"
#include "sl_si91x_iostream_vuart.h"
#include "sl_si91x_debug_swo.h"
#include "sl_mbedtls.h"
#include "sl_iostream_init_instances.h"
#include "cmsis_os2.h"
#include "sl_iostream_handles.h"

void sli_driver_permanent_allocation(void)
{
}

void sli_service_permanent_allocation(void)
{
}

void sli_stack_permanent_allocation(void)
{
}

void sli_internal_permanent_allocation(void)
{
}

void sl_platform_init(void)
{
  sl_si91x_device_init_nvic();
  sl_si91x_clock_manager_init();
  sli_si91x_platform_init();
  RSI_Board_Init();
  DEBUGINIT();
}

void sli_internal_init_early(void)
{
}

void sl_kernel_start(void)
{
  osKernelStart();
}

void sl_driver_init(void)
{
  sl_i2c_init_instances();
  sl_si91x_debug_swo_init();
}

void sl_service_init(void)
{
  sl_mbedtls_init();
  sl_iostream_init_instances_stage_1();
  sl_iostream_init_instances_stage_2();
}

void sl_stack_init(void)
{
}

void sl_internal_app_init(void)
{
}

void sl_iostream_init_instances_stage_1(void)
{
  sl_si91x_iostream_debug_init();
  sl_si91x_iostream_vuart_init();
}

void sl_iostream_init_instances_stage_2(void)
{
  sl_iostream_set_console_instance();
}

