import pathlib
import unittest


SOURCE = pathlib.Path(__file__).with_name("firmware.c").read_text(encoding="utf-8")


class UartTransportRegressionTests(unittest.TestCase):
    def test_uart_rx_uses_interrupt_queue_instead_of_character_polling(self):
        self.assertIn('#include "queue.h"', SOURCE)
        self.assertIn("static void uart_rx_irq_handler(void)", SOURCE)
        self.assertIn("while (uart_is_readable(UART_ID))", SOURCE)
        self.assertIn("xQueueSendFromISR(uart_rx_queue", SOURCE)
        self.assertIn(
            "xQueueReceive(uart_rx_queue, &received, portMAX_DELAY)", SOURCE
        )
        self.assertNotIn(
            "vTaskDelay(milliseconds_to_ticks_min(UART_PERIOD_MS))", SOURCE
        )


if __name__ == "__main__":
    unittest.main()
