#ifndef UART_CONSOLE_H
#define UART_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "frequency_sweep.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t uart_console_init(void);
void uart_console_task(void *arg);

typedef struct {
	uint32_t frequency_hz;
	char wave[8];
	bool busy;
	bool storage_ready;
	sweep_config_t sweep;
	uint32_t local_completion;
	char local_result[64];
	bool sample_valid;
	ad8302_sample_t sample;
	uint32_t sequence;
} analyzer_status_t;

esp_err_t analyzer_submit_command(const char *command);
void analyzer_get_status(analyzer_status_t *status);

#define ANALYZER_LOCAL_MAX_POINTS 256U

esp_err_t analyzer_start_local_sweep(const sweep_config_t *config);
bool analyzer_cancel_local_sweep(void);
bool analyzer_get_local_sample(uint32_t index, ad8302_sample_t *sample);

#ifdef __cplusplus
}
#endif

#endif
