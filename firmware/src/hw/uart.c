#include <nrf9160.h>
#include <zephyr.h>
#include <sys/printk.h>
#include <drivers/uart.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <logging/log.h>

LOG_MODULE_REGISTER( uart );

#define UART_BUFFER_SIZE 64
static K_MEM_SLAB_DEFINE( uart_slab, UART_BUFFER_SIZE, 3, 4 );

enum {
  STATE_OFF,
  STATE_CONNECTING,
  STATE_IDLE,
  STATE_ERROR,
  STATE_SYNCING,
  STATE_DOWNLOADING,
  STATE_SHUTDOWN
} esp32_state;

static struct uart_message {
  bool tx;
  union {
    enum esp32_state status;
    enum esp32_cmd   cmd;
  } esp;
  union {
    // put whatever needed here.
    u32 dummy;
  } data;  
} uart_msg;

enum input_state {
  INPUT_STATE_IDLE = 0,
  INPUT_STATE_WAITING_FOR_NEWLINE,
  INPUT_STATE_PROCESSING
};

static struct uart_text {
  struct k_timer timer;
  struct k_work  work;
  char   buffer[320u];
  u16    len;
  enum   input_state state;
};

static struct uart_text text_proc;
static struct k_timer uart_timer;

static void uart_irq_handler( const struct device *dev, void *ctx ) {

  if ( uart_irq_tx_ready( dev ) ) {
    uart_msg.tx = true;
    uart_msg.esp.cmd = CMD_TEST;
    (void)uart_fifo_fill(dev, uart_msg, sizeof(uart_msg) );
    uart_irq_tx_disable(dev);
  }

  if ( uart_irq_rx_ready( dev ) ) {
    int len = uart_fifo_read( dev, status, sizeof( status ) );

    if ( len ) printk("%d bytes read from UART\n", len);
  }
}

void init_uart( void ) {
  LOG_INF("initializing UART.");

  uart_dev = device_get_binding("UART_0");
  // should assert here
  uart_irq_callback_user_data_set( uart_dev, uart_irq_handler, NULL );
  uart_irq_rx_enable(uart_dev);

  text_proc = (struct uart_text){0};
  text_proc.timer = uart_timer;
  k_timer_init(&text_proc.timer, input_timeout_handler, NULL);
  k_work_init(&text_proc.work,   process_text);
}

static void uart_cb( const struct device *dev, struct uart_event *event, void *data ) {
  struct device *uart = data;

  int err;
  switch ( event->type ) {
  	case UART_TX_DONE:
		LOG_INF("Tx sent %d bytes", evt->data.tx.len);
		break;

    case UART_TX_ABORTED:
      LOG_ERR("Tx aborted");
      break;

    case UART_RX_RDY:
      LOG_INF("Received data %d bytes", evt->data.rx.len);
      break;

    case UART_RX_BUF_REQUEST:
    {
      uint8_t *buf;

      err = k_mem_slab_alloc(&uart_slab, (void **)&buf, K_NO_WAIT);
      __ASSERT(err == 0, "Failed to allocate slab");

      err = uart_rx_buf_rsp(uart, buf, BUF_SIZE);
      __ASSERT(err == 0, "Failed to provide new buffer");
      break;
    }

    case UART_RX_BUF_RELEASED:
      k_mem_slab_free(&uart_slab, (void **)&evt->data.rx_buf.buf);
      break;

    case UART_RX_DISABLED:
      break;

    case UART_RX_STOPPED:
      break;  
  }
}

void timeout_handler( struct k_timer *timer ) {

}

void process_text() {

}
