// SPDX-License-Identifier: MIT
// Copyright (c) 2019-2023 The Pybricks Authors

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <lego/lump.h>

#include <pbio/port_interface.h>
#include <pbio/port_lump.h>

#include <tinytest.h>
#include <tinytest_macros.h>

#include <pbdrv/uart.h>
#include <pbio/main.h>
#include <pbio/os.h>
#include <pbio/util.h>
#include <test-pbio.h>

#include "../drv/clock/clock_test.h"

// TODO: submit this upstream
#ifndef tt_want_float_op
#define tt_want_float_op(a, op, b) \
    tt_assert_test_type(a, b,#a " "#op " "#b, float, (val1_ op val2_), "%f", (void)0)
#endif

struct _pbdrv_uart_dev_t {
    int baud;
    pbio_os_timer_t rx_timer;
    uint8_t *rx_msg;
    uint8_t rx_msg_length;
    pbio_error_t rx_msg_result;
    uint8_t *tx_msg;
    pbio_os_timer_t tx_timer;
    uint8_t tx_msg_length;
    pbio_error_t tx_msg_result;
    struct process *parent_process;
};

pbdrv_uart_dev_t test_uart;

/**
 * RX completion normally creates an IRQ. This mimics such as handler. Since
 * the buffer is already copied, this just needs to call the callback to inform
 * the parent process that the buffer is ready to be read.
 */
static void simulate_uart_complete_irq(void) {
    process_poll(test_uart.parent_process);
}

pbio_error_t simulate_rx_msg(pbio_os_state_t *state, const uint8_t *msg, uint8_t length) {
    PBIO_OS_ASYNC_BEGIN(state);

    // First uartdev reads one byte header
    PBIO_OS_AWAIT_UNTIL(state, ({
        pbio_test_clock_tick(1);
        test_uart.rx_msg_result == PBIO_ERROR_AGAIN;
    }));
    tt_uint_op(test_uart.rx_msg_length, ==, 1);
    memcpy(test_uart.rx_msg, msg, 1);
    test_uart.rx_msg_result = PBIO_SUCCESS;

    simulate_uart_complete_irq();

    if (length == 1) {
        return PBIO_SUCCESS;
    }

    // then read rest of message
    PBIO_OS_AWAIT_UNTIL(state, ({
        pbio_test_clock_tick(1);
        test_uart.rx_msg_result == PBIO_ERROR_AGAIN;
    }));
    tt_uint_op(test_uart.rx_msg_length, ==, length - 1);
    memcpy(test_uart.rx_msg, &msg[1], length - 1);
    test_uart.rx_msg_result = PBIO_SUCCESS;

    simulate_uart_complete_irq();

end:
    PBIO_OS_ASYNC_END(PBIO_SUCCESS);
}

pbio_error_t simulate_tx_msg(pbio_os_state_t *state, const uint8_t *msg, uint8_t length) {
    PBIO_OS_ASYNC_BEGIN(state);

    PBIO_OS_AWAIT_UNTIL(state, ({
        pbio_test_clock_tick(1);
        test_uart.tx_msg_result == PBIO_ERROR_AGAIN;
    }));
    tt_uint_op(test_uart.tx_msg_length, ==, length);

    for (int i = 0; i < length; i++) {
        tt_uint_op(test_uart.tx_msg[i], ==, msg[i]);
    }

    test_uart.tx_msg_result = PBIO_SUCCESS;

    simulate_uart_complete_irq();

end:
    PBIO_OS_ASYNC_END(PBIO_SUCCESS);
}

#define SIMULATE_RX_MSG(msg) do { \
        PBIO_OS_AWAIT(state, &child, err = simulate_rx_msg(&child, (msg), PBIO_ARRAY_SIZE(msg))); \
        tt_assert_msg(err == PBIO_SUCCESS, #msg); \
} while (0)

#define SIMULATE_TX_MSG(msg) do { \
        PBIO_OS_AWAIT(state, &child, err = simulate_tx_msg(&child, (msg), PBIO_ARRAY_SIZE(msg))); \
        tt_assert_msg(err == PBIO_SUCCESS, #msg); \
} while (0)

static const uint8_t msg_speed_115200[] = { 0x52, 0x00, 0xC2, 0x01, 0x00, 0x6E }; // SPEED 115200
static const uint8_t msg_ack[] = { 0x04 }; // ACK

static pbio_error_t test_boost_color_distance_sensor(pbio_os_state_t *state, void *context) {

    // info messages captured from BOOST Color Distance Sensor with logic analyzer
    static const uint8_t msg0[] = { 0x40, 0x25, 0x9A };
    static const uint8_t msg1[] = { 0x51, 0x07, 0x07, 0x0A, 0x07, 0xA3 };
    static const uint8_t msg2[] = { 0x52, 0x00, 0xC2, 0x01, 0x00, 0x6E };
    static const uint8_t msg3[] = { 0x5F, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0xA0 };
    static const uint8_t msg4[] = { 0x9A, 0x20, 0x43, 0x41, 0x4C, 0x49, 0x42, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t msg5[] = { 0x9A, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x7F, 0x47, 0x83 };
    static const uint8_t msg6[] = { 0x9A, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x42, 0xCD };
    static const uint8_t msg7[] = { 0x9A, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x7F, 0x47, 0x81 };
    static const uint8_t msg8[] = { 0x92, 0x24, 0x4E, 0x2F, 0x41, 0x00, 0x69 };
    static const uint8_t msg9[] = { 0x8A, 0x25, 0x10, 0x00, 0x40 };
    static const uint8_t msg10[] = { 0x92, 0xA0, 0x08, 0x01, 0x05, 0x00, 0xC1 };
    static const uint8_t msg11[] = { 0x99, 0x20, 0x44, 0x45, 0x42, 0x55, 0x47, 0x00, 0x00, 0x00, 0x17 };
    static const uint8_t msg12[] = { 0x99, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x7F, 0x44, 0xBC };
    static const uint8_t msg13[] = { 0x99, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x42, 0xCE };
    static const uint8_t msg14[] = { 0x99, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x41, 0x24 };
    static const uint8_t msg15[] = { 0x91, 0x24, 0x4E, 0x2F, 0x41, 0x00, 0x6A };
    static const uint8_t msg16[] = { 0x89, 0x25, 0x10, 0x00, 0x43 };
    static const uint8_t msg17[] = { 0x91, 0xA0, 0x02, 0x01, 0x05, 0x00, 0xC8 };
    static const uint8_t msg18[] = { 0x98, 0x20, 0x53, 0x50, 0x45, 0x43, 0x20, 0x31, 0x00, 0x00, 0x53 };
    static const uint8_t msg19[] = { 0x98, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x43, 0x7A };
    static const uint8_t msg20[] = { 0x98, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x42, 0xCF };
    static const uint8_t msg21[] = { 0x98, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x43, 0x78 };
    static const uint8_t msg22[] = { 0x90, 0x24, 0x4E, 0x2F, 0x41, 0x00, 0x6B };
    static const uint8_t msg23[] = { 0x88, 0x25, 0x00, 0x00, 0x52 };
    static const uint8_t msg24[] = { 0x90, 0xA0, 0x04, 0x00, 0x03, 0x00, 0xC8 };
    static const uint8_t msg25[] = { 0x9F, 0x00, 0x49, 0x52, 0x20, 0x54, 0x78, 0x00, 0x00, 0x00, 0x77 };
    static const uint8_t msg26[] = { 0x9F, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x7F, 0x47, 0xA6 };
    static const uint8_t msg27[] = { 0x9F, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x42, 0xE8 };
    static const uint8_t msg28[] = { 0x9F, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x7F, 0x47, 0xA4 };
    static const uint8_t msg29[] = { 0x97, 0x04, 0x4E, 0x2F, 0x41, 0x00, 0x4C };
    static const uint8_t msg30[] = { 0x8F, 0x05, 0x00, 0x04, 0x71 };
    static const uint8_t msg31[] = { 0x97, 0x80, 0x01, 0x01, 0x05, 0x00, 0xED };
    static const uint8_t msg32[] = { 0x9E, 0x00, 0x52, 0x47, 0x42, 0x20, 0x49, 0x00, 0x00, 0x00, 0x5F };
    static const uint8_t msg33[] = { 0x9E, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x7F, 0x44, 0x9B };
    static const uint8_t msg34[] = { 0x9E, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x42, 0xE9 };
    static const uint8_t msg35[] = { 0x9E, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x7F, 0x44, 0x99 };
    static const uint8_t msg36[] = { 0x96, 0x04, 0x52, 0x41, 0x57, 0x00, 0x29 };
    static const uint8_t msg37[] = { 0x8E, 0x05, 0x10, 0x00, 0x64 };
    static const uint8_t msg38[] = { 0x96, 0x80, 0x03, 0x01, 0x05, 0x00, 0xEE };
    static const uint8_t msg39[] = { 0x9D, 0x00, 0x43, 0x4F, 0x4C, 0x20, 0x4F, 0x00, 0x00, 0x00, 0x4D };
    static const uint8_t msg40[] = { 0x9D, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x41, 0x02 };
    static const uint8_t msg41[] = { 0x9D, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x42, 0xEA };
    static const uint8_t msg42[] = { 0x9D, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x41, 0x00 };
    static const uint8_t msg43[] = { 0x95, 0x04, 0x49, 0x44, 0x58, 0x00, 0x3B };
    static const uint8_t msg44[] = { 0x8D, 0x05, 0x00, 0x04, 0x73 };
    static const uint8_t msg45[] = { 0x95, 0x80, 0x01, 0x00, 0x03, 0x00, 0xE8 };
    static const uint8_t msg46[] = { 0x94, 0x00, 0x41, 0x4D, 0x42, 0x49, 0x6C };
    static const uint8_t msg47[] = { 0x9C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x42, 0xE8 };
    static const uint8_t msg48[] = { 0x9C, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x42, 0xEB };
    static const uint8_t msg49[] = { 0x9C, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x42, 0xEA };
    static const uint8_t msg50[] = { 0x94, 0x04, 0x50, 0x43, 0x54, 0x00, 0x28 };
    static const uint8_t msg51[] = { 0x8C, 0x05, 0x10, 0x00, 0x66 };
    static const uint8_t msg52[] = { 0x94, 0x80, 0x01, 0x00, 0x03, 0x00, 0xE9 };
    static const uint8_t msg53[] = { 0x9B, 0x00, 0x52, 0x45, 0x46, 0x4C, 0x54, 0x00, 0x00, 0x00, 0x2D };
    static const uint8_t msg54[] = { 0x9B, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x42, 0xEF };
    static const uint8_t msg55[] = { 0x9B, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x42, 0xEC };
    static const uint8_t msg56[] = { 0x9B, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x42, 0xED };
    static const uint8_t msg57[] = { 0x93, 0x04, 0x50, 0x43, 0x54, 0x00, 0x2F };
    static const uint8_t msg58[] = { 0x8B, 0x05, 0x10, 0x00, 0x61 };
    static const uint8_t msg59[] = { 0x93, 0x80, 0x01, 0x00, 0x03, 0x00, 0xEE };
    static const uint8_t msg60[] = { 0x9A, 0x00, 0x43, 0x4F, 0x55, 0x4E, 0x54, 0x00, 0x00, 0x00, 0x26 };
    static const uint8_t msg61[] = { 0x9A, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x42, 0xEE };
    static const uint8_t msg62[] = { 0x9A, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x42, 0xED };
    static const uint8_t msg63[] = { 0x9A, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x42, 0xEC };
    static const uint8_t msg64[] = { 0x92, 0x04, 0x43, 0x4E, 0x54, 0x00, 0x30 };
    static const uint8_t msg65[] = { 0x8A, 0x05, 0x08, 0x00, 0x78 };
    static const uint8_t msg66[] = { 0x92, 0x80, 0x01, 0x02, 0x04, 0x00, 0xEA };
    static const uint8_t msg67[] = { 0x91, 0x00, 0x50, 0x52, 0x4F, 0x58, 0x7B };
    static const uint8_t msg68[] = { 0x99, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x41, 0x06 };
    static const uint8_t msg69[] = { 0x99, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x42, 0xEE };
    static const uint8_t msg70[] = { 0x99, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x41, 0x04 };
    static const uint8_t msg71[] = { 0x91, 0x04, 0x44, 0x49, 0x53, 0x00, 0x34 };
    static const uint8_t msg72[] = { 0x89, 0x05, 0x50, 0x00, 0x23 };
    static const uint8_t msg73[] = { 0x91, 0x80, 0x01, 0x00, 0x03, 0x00, 0xEC };
    static const uint8_t msg74[] = { 0x98, 0x00, 0x43, 0x4F, 0x4C, 0x4F, 0x52, 0x00, 0x00, 0x00, 0x3A };
    static const uint8_t msg75[] = { 0x98, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x41, 0x07 };
    static const uint8_t msg76[] = { 0x98, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x42, 0xEF };
    static const uint8_t msg77[] = { 0x98, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x41, 0x05 };
    static const uint8_t msg78[] = { 0x90, 0x04, 0x49, 0x44, 0x58, 0x00, 0x3E };
    static const uint8_t msg79[] = { 0x88, 0x05, 0xC4, 0x00, 0xB6 };
    static const uint8_t msg80[] = { 0x90, 0x80, 0x01, 0x00, 0x03, 0x00, 0xED };
    static const uint8_t msg81[] = { 0x88, 0x06, 0x4F, 0x00, 0x3E };
    static const uint8_t msg82[] = { 0x04 };

    static const uint8_t msg83[] = { 0x04 }; // ACK

    static const uint8_t msg83b[] = { 0x43, 0x06, 0xBA }; // set default mode

    static const uint8_t msg84[] = { 0x02 }; // NACK

    // mode 6 DATA message captured from BOOST Color and Distance Sensor
    static const uint8_t msg85[] = { 0x46, 0x00, 0xB9 }; // extended mode info
    static const uint8_t msg86[] = { 0xC0 | 0x18 | 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21 }; // mode 6 data

    static const uint8_t msg87[] = { 0x43, 0x01, 0xBD }; // set mode 1
    static const uint8_t msg88[] = { 0xC1, 0x00, 0x3E }; // mode 1 data

    static const uint8_t msg89[] = { 0x43, 0x08, 0xB4 }; // set mode 8
    static const uint8_t msg90[] = { 0x46, 0x08, 0xB1 }; // extended mode info
    static const uint8_t msg91[] = { 0xD0, 0x00, 0x00, 0x00, 0x00, 0x2F }; // mode 8 data

    // used in SIMULATE_RX/TX_MSG macros
    static pbio_os_state_t child;

    static pbio_port_t *port;
    static pbio_port_lump_dev_t *lump_dev;
    static lego_device_type_id_t type_id;
    static pbio_port_lump_mode_info_t *mode_info;
    static uint8_t current_mode;
    static uint8_t num_modes;

    pbio_error_t err;

    PBIO_OS_ASYNC_BEGIN(state);

    // Should be able to get port, but device won't be ready yet since it isn't
    // synched up.
    static lego_device_type_id_t expected_id = LEGO_DEVICE_TYPE_ID_COLOR_DIST_SENSOR;
    tt_uint_op(pbio_port_get_port(PBIO_PORT_ID_D, &port), ==, PBIO_SUCCESS);

    // starting baud rate of hub
    PBIO_OS_AWAIT_UNTIL(state, ({
        pbio_test_clock_tick(1);
        test_uart.baud == 115200;
    }));

    // this device does not support syncing at 115200
    SIMULATE_TX_MSG(msg_speed_115200);
    PBIO_OS_AWAIT_UNTIL(state, ({
        pbio_test_clock_tick(1);
        test_uart.baud == 2400;
    }));

    // send BOOST Color and Distance sensor info
    SIMULATE_RX_MSG(msg0);
    SIMULATE_RX_MSG(msg1);
    SIMULATE_RX_MSG(msg2);
    SIMULATE_RX_MSG(msg3);
    SIMULATE_RX_MSG(msg4);
    SIMULATE_RX_MSG(msg5);
    SIMULATE_RX_MSG(msg6);
    SIMULATE_RX_MSG(msg7);
    SIMULATE_RX_MSG(msg8);
    SIMULATE_RX_MSG(msg9);
    SIMULATE_RX_MSG(msg10);
    SIMULATE_RX_MSG(msg11);
    SIMULATE_RX_MSG(msg12);
    SIMULATE_RX_MSG(msg13);
    SIMULATE_RX_MSG(msg14);
    SIMULATE_RX_MSG(msg15);
    SIMULATE_RX_MSG(msg16);
    SIMULATE_RX_MSG(msg17);
    SIMULATE_RX_MSG(msg18);
    SIMULATE_RX_MSG(msg19);
    SIMULATE_RX_MSG(msg20);
    SIMULATE_RX_MSG(msg21);
    SIMULATE_RX_MSG(msg22);
    SIMULATE_RX_MSG(msg23);
    SIMULATE_RX_MSG(msg24);
    SIMULATE_RX_MSG(msg25);
    SIMULATE_RX_MSG(msg26);
    SIMULATE_RX_MSG(msg27);
    SIMULATE_RX_MSG(msg28);
    SIMULATE_RX_MSG(msg29);
    SIMULATE_RX_MSG(msg30);
    SIMULATE_RX_MSG(msg31);
    SIMULATE_RX_MSG(msg32);
    SIMULATE_RX_MSG(msg33);
    SIMULATE_RX_MSG(msg34);
    SIMULATE_RX_MSG(msg35);
    SIMULATE_RX_MSG(msg36);
    SIMULATE_RX_MSG(msg37);
    SIMULATE_RX_MSG(msg38);
    SIMULATE_RX_MSG(msg39);
    SIMULATE_RX_MSG(msg40);
    SIMULATE_RX_MSG(msg41);
    SIMULATE_RX_MSG(msg42);
    SIMULATE_RX_MSG(msg43);
    SIMULATE_RX_MSG(msg44);
    SIMULATE_RX_MSG(msg45);
    SIMULATE_RX_MSG(msg46);
    SIMULATE_RX_MSG(msg47);
    SIMULATE_RX_MSG(msg48);
    SIMULATE_RX_MSG(msg49);
    SIMULATE_RX_MSG(msg50);
    SIMULATE_RX_MSG(msg51);
    SIMULATE_RX_MSG(msg52);
    SIMULATE_RX_MSG(msg53);
    SIMULATE_RX_MSG(msg54);
    SIMULATE_RX_MSG(msg55);
    SIMULATE_RX_MSG(msg56);
    SIMULATE_RX_MSG(msg57);
    SIMULATE_RX_MSG(msg58);
    SIMULATE_RX_MSG(msg59);
    SIMULATE_RX_MSG(msg60);
    SIMULATE_RX_MSG(msg61);
    SIMULATE_RX_MSG(msg62);
    SIMULATE_RX_MSG(msg63);
    SIMULATE_RX_MSG(msg64);
    SIMULATE_RX_MSG(msg65);
    SIMULATE_RX_MSG(msg66);
    SIMULATE_RX_MSG(msg67);
    SIMULATE_RX_MSG(msg68);
    SIMULATE_RX_MSG(msg69);
    SIMULATE_RX_MSG(msg70);
    SIMULATE_RX_MSG(msg71);
    SIMULATE_RX_MSG(msg72);
    SIMULATE_RX_MSG(msg73);
    SIMULATE_RX_MSG(msg74);
    SIMULATE_RX_MSG(msg75);
    SIMULATE_RX_MSG(msg76);
    SIMULATE_RX_MSG(msg77);
    SIMULATE_RX_MSG(msg78);
    SIMULATE_RX_MSG(msg79);
    SIMULATE_RX_MSG(msg80);
    SIMULATE_RX_MSG(msg81);
    SIMULATE_RX_MSG(msg82);

    SIMULATE_TX_MSG(msg83);

    // wait for baud rate change
    PBIO_OS_AWAIT_UNTIL(state, ({
        pbio_test_clock_tick(1);
        test_uart.baud == 115200;
    }));

    // Simulate setting default mode
    SIMULATE_TX_MSG(msg83b);

    // should be synced now and receive regular pings
    static int i;
    for (i = 0; i < 10; i++) {

        // receive data
        SIMULATE_RX_MSG(msg85);
        SIMULATE_RX_MSG(msg86);

        // Send nack (keep alive)
        SIMULATE_TX_MSG(msg84);
    }

    // Wait for default mode to complete
    PBIO_OS_AWAIT_WHILE(state, ({
        pbio_test_clock_tick(1);
        (err = pbio_port_get_lump_device(port, &expected_id, &lump_dev)) == PBIO_ERROR_AGAIN;
    }));

    tt_uint_op(err, ==, PBIO_SUCCESS);

    type_id = LEGO_DEVICE_TYPE_ID_ANY_LUMP_UART;
    tt_uint_op(pbio_port_lump_assert_type_id(lump_dev, &type_id), ==, PBIO_SUCCESS);
    tt_uint_op(pbio_port_lump_get_info(lump_dev, &num_modes, &current_mode, &mode_info), ==, PBIO_SUCCESS);

    tt_want_uint_op(type_id, ==, LEGO_DEVICE_TYPE_ID_COLOR_DIST_SENSOR);
    tt_want_uint_op(num_modes, ==, 11);
    // TODO: verify fw/hw versions

    tt_want_uint_op(current_mode, ==, LEGO_DEVICE_MODE_PUP_COLOR_DISTANCE_SENSOR__RGB_I);

    tt_want_uint_op(mode_info[0].num_values, ==, 1);
    tt_want_uint_op(mode_info[0].data_type, ==, LUMP_DATA_TYPE_DATA8);
    tt_want_uint_op(mode_info[0].writable, ==, 0);

    tt_want_uint_op(mode_info[1].num_values, ==, 1);
    tt_want_uint_op(mode_info[1].data_type, ==, LUMP_DATA_TYPE_DATA8);
    tt_want_uint_op(mode_info[1].writable, ==, 0);

    tt_want_uint_op(mode_info[2].num_values, ==, 1);
    tt_want_uint_op(mode_info[2].data_type, ==, LUMP_DATA_TYPE_DATA32);
    tt_want_uint_op(mode_info[2].writable, ==, 0);

    tt_want_uint_op(mode_info[3].num_values, ==, 1);
    tt_want_uint_op(mode_info[3].data_type, ==, LUMP_DATA_TYPE_DATA8);
    tt_want_uint_op(mode_info[3].writable, ==, 0);

    tt_want_uint_op(mode_info[4].num_values, ==, 1);
    tt_want_uint_op(mode_info[4].data_type, ==, LUMP_DATA_TYPE_DATA8);
    tt_want_uint_op(mode_info[4].writable, ==, 0);

    tt_want_uint_op(mode_info[5].num_values, ==, 1);
    tt_want_uint_op(mode_info[5].data_type, ==, LUMP_DATA_TYPE_DATA8);
    tt_want_uint_op(mode_info[5].writable, ==, 1);

    tt_want_uint_op(mode_info[6].num_values, ==, 3);
    tt_want_uint_op(mode_info[6].data_type, ==, LUMP_DATA_TYPE_DATA16);
    tt_want_uint_op(mode_info[6].writable, ==, 0);

    tt_want_uint_op(mode_info[7].num_values, ==, 1);
    tt_want_uint_op(mode_info[7].data_type, ==, LUMP_DATA_TYPE_DATA16);
    tt_want_uint_op(mode_info[7].writable, ==, 1);

    tt_want_uint_op(mode_info[8].num_values, ==, 4);
    tt_want_uint_op(mode_info[8].data_type, ==, LUMP_DATA_TYPE_DATA8);
    tt_want_uint_op(mode_info[8].writable, ==, 0);

    tt_want_uint_op(mode_info[9].num_values, ==, 2);
    tt_want_uint_op(mode_info[9].data_type, ==, LUMP_DATA_TYPE_DATA16);
    tt_want_uint_op(mode_info[9].writable, ==, 0);

    tt_want_uint_op(mode_info[10].num_values, ==, 8);
    tt_want_uint_op(mode_info[10].data_type, ==, LUMP_DATA_TYPE_DATA16);
    tt_want_uint_op(mode_info[10].writable, ==, 0);

    // test changing the mode

    err = pbio_port_lump_set_mode(lump_dev, 1);
    tt_uint_op(err, ==, PBIO_SUCCESS);

    // wait for mode change message to be sent
    SIMULATE_TX_MSG(msg87);

    // should be blocked since data with new mode has not been received yet
    tt_uint_op(pbio_port_lump_is_ready(lump_dev), ==, PBIO_ERROR_AGAIN);

    // data message with new mode
    SIMULATE_RX_MSG(msg88);

    PBIO_OS_AWAIT_WHILE(state, ({
        pbio_test_clock_tick(1);
        (err = pbio_port_lump_is_ready(lump_dev)) == PBIO_ERROR_AGAIN;
    }));
    tt_uint_op(err, ==, PBIO_SUCCESS);

    type_id = LEGO_DEVICE_TYPE_ID_ANY_LUMP_UART;
    tt_uint_op(pbio_port_lump_assert_type_id(lump_dev, &type_id), ==, PBIO_SUCCESS);
    tt_uint_op(pbio_port_lump_get_info(lump_dev, &num_modes, &current_mode, &mode_info), ==, PBIO_SUCCESS);
    tt_uint_op(current_mode, ==, 1);


    // also do mode 8 since it requires the extended mode flag
    PBIO_OS_AWAIT_WHILE(state, ({
        pbio_test_clock_tick(1);
        (err = pbio_port_lump_set_mode(lump_dev, 8)) == PBIO_ERROR_AGAIN;
    }));
    tt_uint_op(err, ==, PBIO_SUCCESS);

    // wait for mode change message to be sent
    SIMULATE_TX_MSG(msg89);

    // should be blocked since data with new mode has not been received yet
    tt_uint_op(pbio_port_lump_is_ready(lump_dev), ==, PBIO_ERROR_AGAIN);

    // send data message with new mode
    SIMULATE_RX_MSG(msg90);
    SIMULATE_RX_MSG(msg91);

    PBIO_OS_AWAIT_WHILE(state, ({
        pbio_test_clock_tick(1);
        (err = pbio_port_lump_is_ready(lump_dev)) == PBIO_ERROR_AGAIN;
    }));
    tt_uint_op(err, ==, PBIO_SUCCESS);
    type_id = LEGO_DEVICE_TYPE_ID_ANY_LUMP_UART;
    tt_uint_op(pbio_port_lump_assert_type_id(lump_dev, &type_id), ==, PBIO_SUCCESS);
    tt_uint_op(pbio_port_lump_get_info(lump_dev, &num_modes, &current_mode, &mode_info), ==, PBIO_SUCCESS);
    tt_uint_op(current_mode, ==, 8);


end:

    PBIO_OS_ASYNC_END(PBIO_SUCCESS);
}

static pbio_error_t test_boost_interactive_motor(pbio_os_state_t *state, void *context) {
    // info messages captured from BOOST Interactive Motor with logic analyzer
    static const uint8_t msg0[] = { 0x40, 0x26, 0x99 };
    static const uint8_t msg1[] = { 0x49, 0x03, 0x02, 0xB7 };
    static const uint8_t msg2[] = { 0x52, 0x00, 0xC2, 0x01, 0x00, 0x6E };
    static const uint8_t msg3[] = { 0x5F, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0xA0 };
    static const uint8_t msg4[] = { 0x93, 0x00, 0x54, 0x45, 0x53, 0x54, 0x7A };
    static const uint8_t msg5[] = { 0x9B, 0x01, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE5 };
    static const uint8_t msg6[] = { 0x9B, 0x02, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE6 };
    static const uint8_t msg7[] = { 0x9B, 0x03, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE7 };
    static const uint8_t msg8[] = { 0x93, 0x04, 0x54, 0x53, 0x54, 0x00, 0x3B };
    static const uint8_t msg9[] = { 0x8B, 0x05, 0x00, 0x00, 0x71 };
    static const uint8_t msg10[] = { 0x93, 0x80, 0x05, 0x01, 0x06, 0x00, 0xEE };
    static const uint8_t msg11[] = { 0x92, 0x00, 0x50, 0x4F, 0x53, 0x00, 0x21 };
    static const uint8_t msg12[] = { 0x9A, 0x01, 0x00, 0x00, 0xB4, 0xC3, 0x00, 0x00, 0xB4, 0x43, 0xE4 };
    static const uint8_t msg13[] = { 0x9A, 0x02, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE7 };
    static const uint8_t msg14[] = { 0x9A, 0x03, 0x00, 0x00, 0xB4, 0xC3, 0x00, 0x00, 0xB4, 0x43, 0xE6 };
    static const uint8_t msg15[] = { 0x92, 0x04, 0x44, 0x45, 0x47, 0x00, 0x2F };
    static const uint8_t msg16[] = { 0x8A, 0x05, 0x08, 0x00, 0x78 };
    static const uint8_t msg17[] = { 0x92, 0x80, 0x01, 0x02, 0x06, 0x00, 0xE8 };
    static const uint8_t msg18[] = { 0x99, 0x00, 0x53, 0x50, 0x45, 0x45, 0x44, 0x00, 0x00, 0x00, 0x21 };
    static const uint8_t msg19[] = { 0x99, 0x01, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE7 };
    static const uint8_t msg20[] = { 0x99, 0x02, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE4 };
    static const uint8_t msg21[] = { 0x99, 0x03, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE5 };
    static const uint8_t msg22[] = { 0x91, 0x04, 0x50, 0x43, 0x54, 0x00, 0x2D };
    static const uint8_t msg23[] = { 0x89, 0x05, 0x10, 0x00, 0x63 };
    static const uint8_t msg24[] = { 0x91, 0x80, 0x01, 0x00, 0x04, 0x00, 0xEB };
    static const uint8_t msg25[] = { 0x98, 0x00, 0x50, 0x4F, 0x57, 0x45, 0x52, 0x00, 0x00, 0x00, 0x38 };
    static const uint8_t msg26[] = { 0x98, 0x01, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE6 };
    static const uint8_t msg27[] = { 0x98, 0x02, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE5 };
    static const uint8_t msg28[] = { 0x98, 0x03, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE4 };
    static const uint8_t msg29[] = { 0x90, 0x04, 0x50, 0x43, 0x54, 0x00, 0x2C };
    static const uint8_t msg30[] = { 0x88, 0x05, 0x00, 0x50, 0x22 };
    static const uint8_t msg31[] = { 0x90, 0x80, 0x01, 0x00, 0x04, 0x00, 0xEA };
    static const uint8_t msg32[] = { 0x88, 0x06, 0x06, 0x00, 0x77 };
    static const uint8_t msg33[] = { 0x04 };

    static const uint8_t msg34[] = { 0x04 }; // ACK

    static const uint8_t msg35[] = { 0x43, 0x02, 0xBE }; // set default mode

    static const uint8_t msg36[] = { 0xC0 | 0x10 | 0x02, 0x00, 0x00, 0x00, 0x00, 0x2D }; // mode 2, angle 0

    static const uint8_t msg37[] = { 0x02 }; // NACK

    // used in SIMULATE_RX/TX_MSG macros
    static pbio_os_state_t child;

    static pbio_port_t *port;
    static pbio_port_lump_dev_t *lump_dev;
    static lego_device_type_id_t type_id;
    static pbio_port_lump_mode_info_t *mode_info;
    static uint8_t current_mode;
    static uint8_t num_modes;
    static pbio_error_t err;

    PBIO_OS_ASYNC_BEGIN(state);


    // Should be able to get port, but device won't be ready yet since it isn't
    // synched up.
    static lego_device_type_id_t expected_id = LEGO_DEVICE_TYPE_ID_INTERACTIVE_MOTOR;
    tt_uint_op(pbio_port_get_port(PBIO_PORT_ID_D, &port), ==, PBIO_SUCCESS);

    // starting baud rate of hub
    PBIO_OS_AWAIT_UNTIL(state, ({
        pbio_test_clock_tick(1);
        test_uart.baud == 115200;
    }));

    // this device does not support syncing at 115200
    SIMULATE_TX_MSG(msg_speed_115200);
    PBIO_OS_AWAIT_UNTIL(state, ({
        pbio_test_clock_tick(1);
        test_uart.baud == 2400;
    }));

    // send BOOST Interactive Motor info
    SIMULATE_RX_MSG(msg0);
    SIMULATE_RX_MSG(msg1);
    SIMULATE_RX_MSG(msg2);
    SIMULATE_RX_MSG(msg3);
    SIMULATE_RX_MSG(msg4);
    SIMULATE_RX_MSG(msg5);
    SIMULATE_RX_MSG(msg6);
    SIMULATE_RX_MSG(msg7);
    SIMULATE_RX_MSG(msg8);
    SIMULATE_RX_MSG(msg9);
    SIMULATE_RX_MSG(msg10);
    SIMULATE_RX_MSG(msg11);
    SIMULATE_RX_MSG(msg12);
    SIMULATE_RX_MSG(msg13);
    SIMULATE_RX_MSG(msg14);
    SIMULATE_RX_MSG(msg15);
    SIMULATE_RX_MSG(msg16);
    SIMULATE_RX_MSG(msg17);
    SIMULATE_RX_MSG(msg18);
    SIMULATE_RX_MSG(msg19);
    SIMULATE_RX_MSG(msg20);
    SIMULATE_RX_MSG(msg21);
    SIMULATE_RX_MSG(msg22);
    SIMULATE_RX_MSG(msg23);
    SIMULATE_RX_MSG(msg24);
    SIMULATE_RX_MSG(msg25);
    SIMULATE_RX_MSG(msg26);
    SIMULATE_RX_MSG(msg27);
    SIMULATE_RX_MSG(msg28);
    SIMULATE_RX_MSG(msg29);
    SIMULATE_RX_MSG(msg30);
    SIMULATE_RX_MSG(msg31);
    SIMULATE_RX_MSG(msg32);
    SIMULATE_RX_MSG(msg33);

    // wait for ACK
    SIMULATE_TX_MSG(msg34);

    // wait for baud rate change
    PBIO_OS_AWAIT_UNTIL(state, ({
        pbio_test_clock_tick(1);
        test_uart.baud == 115200;
    }));


    // Simulate setting default mode
    SIMULATE_TX_MSG(msg35);

    // should be synced now are receive regular pings
    static int i;
    for (i = 0; i < 10; i++) {
        // receive data
        SIMULATE_RX_MSG(msg36);

        // wait for NACK
        SIMULATE_TX_MSG(msg37);
    }

    PBIO_OS_AWAIT_WHILE(state, ({
        pbio_test_clock_tick(1);
        (err = pbio_port_get_lump_device(port, &expected_id, &lump_dev)) == PBIO_ERROR_AGAIN;
    }));
    tt_uint_op(err, ==, PBIO_SUCCESS);

    type_id = LEGO_DEVICE_TYPE_ID_ANY_LUMP_UART;
    tt_uint_op(pbio_port_lump_assert_type_id(lump_dev, &type_id), ==, PBIO_SUCCESS);
    tt_uint_op(pbio_port_lump_get_info(lump_dev, &num_modes, &current_mode, &mode_info), ==, PBIO_SUCCESS);

    tt_want_uint_op(type_id, ==, LEGO_DEVICE_TYPE_ID_INTERACTIVE_MOTOR);
    tt_want_uint_op(num_modes, ==, 4);
    tt_want_uint_op(current_mode, ==, LEGO_DEVICE_MODE_PUP_REL_MOTOR__POS);

    tt_want_uint_op(mode_info[0].num_values, ==, 1);
    tt_want_uint_op(mode_info[0].data_type, ==, LUMP_DATA_TYPE_DATA8);
    tt_want_uint_op(mode_info[0].writable, ==, 1);

    tt_want_uint_op(mode_info[1].num_values, ==, 1);
    tt_want_uint_op(mode_info[1].data_type, ==, LUMP_DATA_TYPE_DATA8);
    tt_want_uint_op(mode_info[1].writable, ==, 0);

    tt_want_uint_op(mode_info[2].num_values, ==, 1);
    tt_want_uint_op(mode_info[2].data_type, ==, LUMP_DATA_TYPE_DATA32);
    tt_want_uint_op(mode_info[2].writable, ==, 0);

    tt_want_uint_op(mode_info[3].num_values, ==, 5);
    tt_want_uint_op(mode_info[3].data_type, ==, LUMP_DATA_TYPE_DATA16);
    tt_want_uint_op(mode_info[3].writable, ==, 0);


end:

    PBIO_OS_ASYNC_END(PBIO_SUCCESS);
}

static pbio_error_t test_technic_large_motor(pbio_os_state_t *state, void *context) {
    // info messages captured from Technic Large Linear Motor with logic analyzer
    static const uint8_t msg2[] = { 0x40, 0x2E, 0x91 };
    static const uint8_t msg3[] = { 0x49, 0x05, 0x03, 0xB0 };
    static const uint8_t msg4[] = { 0x52, 0x00, 0xC2, 0x01, 0x00, 0x6E };
    static const uint8_t msg5[] = { 0x5F, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0xB4 };
    static const uint8_t msg6[] = { 0xA5, 0x00, 0x53, 0x54, 0x41, 0x54, 0x53, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x04, 0x00, 0x00, 0x00, 0x00, 0x1A };
    static const uint8_t msg7[] = { 0x9D, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x7F, 0x47, 0xA4 };
    static const uint8_t msg8[] = { 0x9D, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x42, 0xEA };
    static const uint8_t msg9[] = { 0x9D, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x7F, 0x47, 0xA6 };
    static const uint8_t msg10[] = { 0x95, 0x04, 0x4D, 0x49, 0x4E, 0x00, 0x24 };
    static const uint8_t msg11[] = { 0x8D, 0x05, 0x00, 0x00, 0x77 };
    static const uint8_t msg12[] = { 0x95, 0x80, 0x0E, 0x01, 0x05, 0x00, 0xE0 };
    static const uint8_t msg13[] = { 0xA4, 0x00, 0x43, 0x41, 0x4C, 0x49, 0x42, 0x00, 0x22, 0x40, 0x00, 0x00, 0x05, 0x04, 0x00, 0x00, 0x00, 0x00, 0x7D };
    static const uint8_t msg14[] = { 0x9C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x61, 0x45, 0x46 };
    static const uint8_t msg15[] = { 0x9C, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x42, 0xEB };
    static const uint8_t msg16[] = { 0x9C, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x61, 0x45, 0x44 };
    static const uint8_t msg17[] = { 0x94, 0x04, 0x43, 0x41, 0x4C, 0x00, 0x21 };
    static const uint8_t msg18[] = { 0x8C, 0x05, 0x00, 0x00, 0x76 };
    static const uint8_t msg19[] = { 0x94, 0x80, 0x02, 0x01, 0x05, 0x00, 0xED };
    static const uint8_t msg20[] = { 0xA3, 0x00, 0x41, 0x50, 0x4F, 0x53, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00, 0x05, 0x04, 0x00, 0x00, 0x00, 0x00, 0x72 };
    static const uint8_t msg21[] = { 0x9B, 0x01, 0x00, 0x00, 0x34, 0xC3, 0x00, 0x00, 0x33, 0x43, 0xE2 };
    static const uint8_t msg22[] = { 0x9B, 0x02, 0x00, 0x00, 0x48, 0xC3, 0x00, 0x00, 0x48, 0x43, 0xE6 };
    static const uint8_t msg23[] = { 0x9B, 0x03, 0x00, 0x00, 0x34, 0xC3, 0x00, 0x00, 0x33, 0x43, 0xE0 };
    static const uint8_t msg24[] = { 0x93, 0x04, 0x44, 0x45, 0x47, 0x00, 0x2E };
    static const uint8_t msg25[] = { 0x8B, 0x05, 0x32, 0x32, 0x71 };
    static const uint8_t msg26[] = { 0x93, 0x80, 0x01, 0x01, 0x03, 0x00, 0xEF };
    static const uint8_t msg27[] = { 0xA2, 0x00, 0x50, 0x4F, 0x53, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x05, 0x04, 0x00, 0x00, 0x00, 0x00, 0x34 };
    static const uint8_t msg28[] = { 0x9A, 0x01, 0x00, 0x00, 0xB4, 0xC3, 0x00, 0x00, 0xB4, 0x43, 0xE4 };
    static const uint8_t msg29[] = { 0x9A, 0x02, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE7 };
    static const uint8_t msg30[] = { 0x9A, 0x03, 0x00, 0x00, 0xB4, 0xC3, 0x00, 0x00, 0xB4, 0x43, 0xE6 };
    static const uint8_t msg31[] = { 0x92, 0x04, 0x44, 0x45, 0x47, 0x00, 0x2F };
    static const uint8_t msg32[] = { 0x8A, 0x05, 0x28, 0x68, 0x30 };
    static const uint8_t msg33[] = { 0x92, 0x80, 0x01, 0x02, 0x0B, 0x00, 0xE5 };
    static const uint8_t msg34[] = { 0xA1, 0x00, 0x53, 0x50, 0x45, 0x45, 0x44, 0x00, 0x21, 0x00, 0x00, 0x00, 0x05, 0x04, 0x00, 0x00, 0x00, 0x00, 0x39 };
    static const uint8_t msg35[] = { 0x99, 0x01, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE7 };
    static const uint8_t msg36[] = { 0x99, 0x02, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE4 };
    static const uint8_t msg37[] = { 0x99, 0x03, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE5 };
    static const uint8_t msg38[] = { 0x91, 0x04, 0x50, 0x43, 0x54, 0x00, 0x2D };
    static const uint8_t msg39[] = { 0x89, 0x05, 0x30, 0x70, 0x33 };
    static const uint8_t msg40[] = { 0x91, 0x80, 0x01, 0x00, 0x04, 0x00, 0xEB };
    static const uint8_t msg41[] = { 0xA0, 0x00, 0x50, 0x4F, 0x57, 0x45, 0x52, 0x00, 0x30, 0x00, 0x00, 0x00, 0x05, 0x04, 0x00, 0x00, 0x00, 0x00, 0x31 };
    static const uint8_t msg42[] = { 0x98, 0x01, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE6 };
    static const uint8_t msg43[] = { 0x98, 0x02, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE5 };
    static const uint8_t msg44[] = { 0x98, 0x03, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE4 };
    static const uint8_t msg45[] = { 0x90, 0x04, 0x50, 0x43, 0x54, 0x00, 0x2C };
    static const uint8_t msg46[] = { 0x88, 0x05, 0x00, 0x50, 0x22 };
    static const uint8_t msg47[] = { 0x90, 0x80, 0x01, 0x00, 0x04, 0x00, 0xEA };
    static const uint8_t msg48[] = { 0x88, 0x06, 0x0E, 0x00, 0x7F };
    static const uint8_t msg49[] = { 0xA0, 0x08, 0x00, 0x40, 0x00, 0x2E, 0x09, 0x47, 0x38, 0x33, 0x36, 0x36, 0x36, 0x30, 0x00, 0x00, 0x00, 0x00, 0x7A };
    static const uint8_t msg50[] = { 0xA0, 0x09, 0x88, 0x13, 0x00, 0x00, 0xFA, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00, 0xBE, 0x05, 0x00, 0x00, 0xBB };
    static const uint8_t msg51[] = { 0xA0, 0x0A, 0x98, 0x3A, 0x00, 0x00, 0x96, 0x00, 0x00, 0x00, 0x98, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC3 };
    static const uint8_t msg52[] = { 0x98, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6C };
    static const uint8_t msg53[] = { 0x90, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x63 };
    static const uint8_t msg54[] = { 0x04 };

    static const uint8_t msg55[] = { 0x04 }; // ACK

    static const uint8_t msg56[] = { 0x43, 0x04, 0xB8 }; // set default mode

    static const uint8_t msg57[] = { 0xC0 | 0x10 | 0x04, 0x00, 0x00, 0x00, 0x00, 0x2B }; // mode 4, data 0, 0

    static const uint8_t msg58[] = { 0x02 }; // NACK

    // used in SIMULATE_RX/TX_MSG macros
    static pbio_os_state_t child;

    static pbio_port_t *port;
    static pbio_port_lump_dev_t *lump_dev;
    static lego_device_type_id_t type_id;
    static pbio_port_lump_mode_info_t *mode_info;
    static uint8_t current_mode;
    static uint8_t num_modes;
    static pbio_error_t err;

    PBIO_OS_ASYNC_BEGIN(state);


    // Expect no device at first.
    static lego_device_type_id_t expected_id = LEGO_DEVICE_TYPE_ID_TECHNIC_L_MOTOR;
    tt_uint_op(pbio_port_get_port(PBIO_PORT_ID_D, &port), ==, PBIO_SUCCESS);

    // baud rate for sync messages
    PBIO_OS_AWAIT_UNTIL(state, ({
        pbio_test_clock_tick(1);
        test_uart.baud == 115200;
    }));

    // this device supports syncing at 115200
    SIMULATE_TX_MSG(msg_speed_115200);
    SIMULATE_RX_MSG(msg_ack);

    // send Technic Large Motor info
    SIMULATE_RX_MSG(msg2);
    SIMULATE_RX_MSG(msg3);
    SIMULATE_RX_MSG(msg4);
    SIMULATE_RX_MSG(msg5);
    SIMULATE_RX_MSG(msg6);
    SIMULATE_RX_MSG(msg7);
    SIMULATE_RX_MSG(msg8);
    SIMULATE_RX_MSG(msg9);
    SIMULATE_RX_MSG(msg10);
    SIMULATE_RX_MSG(msg11);
    SIMULATE_RX_MSG(msg12);
    SIMULATE_RX_MSG(msg13);
    SIMULATE_RX_MSG(msg14);
    SIMULATE_RX_MSG(msg15);
    SIMULATE_RX_MSG(msg16);
    SIMULATE_RX_MSG(msg17);
    SIMULATE_RX_MSG(msg18);
    SIMULATE_RX_MSG(msg19);
    SIMULATE_RX_MSG(msg20);
    SIMULATE_RX_MSG(msg21);
    SIMULATE_RX_MSG(msg22);
    SIMULATE_RX_MSG(msg23);
    SIMULATE_RX_MSG(msg24);
    SIMULATE_RX_MSG(msg25);
    SIMULATE_RX_MSG(msg26);
    SIMULATE_RX_MSG(msg27);
    SIMULATE_RX_MSG(msg28);
    SIMULATE_RX_MSG(msg29);
    SIMULATE_RX_MSG(msg30);
    SIMULATE_RX_MSG(msg31);
    SIMULATE_RX_MSG(msg32);
    SIMULATE_RX_MSG(msg33);
    SIMULATE_RX_MSG(msg34);
    SIMULATE_RX_MSG(msg35);
    SIMULATE_RX_MSG(msg36);
    SIMULATE_RX_MSG(msg37);
    SIMULATE_RX_MSG(msg38);
    SIMULATE_RX_MSG(msg39);
    SIMULATE_RX_MSG(msg40);
    SIMULATE_RX_MSG(msg41);
    SIMULATE_RX_MSG(msg42);
    SIMULATE_RX_MSG(msg43);
    SIMULATE_RX_MSG(msg44);
    SIMULATE_RX_MSG(msg45);
    SIMULATE_RX_MSG(msg46);
    SIMULATE_RX_MSG(msg47);
    SIMULATE_RX_MSG(msg48);
    SIMULATE_RX_MSG(msg49);
    SIMULATE_RX_MSG(msg50);
    SIMULATE_RX_MSG(msg51);
    SIMULATE_RX_MSG(msg52);
    SIMULATE_RX_MSG(msg53);
    SIMULATE_RX_MSG(msg54);

    // ensure that baud rate didn't change during sync
    tt_want_uint_op(test_uart.baud, ==, 115200);

    // wait for ACK
    SIMULATE_TX_MSG(msg55);


    // Simulate setting default mode
    SIMULATE_TX_MSG(msg56);

    // should be synced now are receive regular pings
    static int i;
    for (i = 0; i < 10; i++) {
        // receive with data
        SIMULATE_RX_MSG(msg57);

        // wait for NACK
        SIMULATE_TX_MSG(msg58);
    }

    PBIO_OS_AWAIT_WHILE(state, ({
        pbio_test_clock_tick(1);
        (err = pbio_port_get_lump_device(port, &expected_id, &lump_dev)) == PBIO_ERROR_AGAIN;
    }));

    tt_uint_op(err, ==, PBIO_SUCCESS);

    type_id = LEGO_DEVICE_TYPE_ID_ANY_LUMP_UART;
    tt_uint_op(pbio_port_lump_assert_type_id(lump_dev, &type_id), ==, PBIO_SUCCESS);
    tt_uint_op(pbio_port_lump_get_info(lump_dev, &num_modes, &current_mode, &mode_info), ==, PBIO_SUCCESS);

    tt_want_uint_op(type_id, ==, LEGO_DEVICE_TYPE_ID_TECHNIC_L_MOTOR);
    tt_want_uint_op(num_modes, ==, 6);
    tt_want_uint_op(current_mode, ==, LEGO_DEVICE_MODE_PUP_ABS_MOTOR__CALIB);

    tt_want_uint_op(mode_info[0].num_values, ==, 1);
    tt_want_uint_op(mode_info[0].data_type, ==, LUMP_DATA_TYPE_DATA8);
    tt_want_uint_op(mode_info[0].writable, ==, 1);

    tt_want_uint_op(mode_info[1].num_values, ==, 1);
    tt_want_uint_op(mode_info[1].data_type, ==, LUMP_DATA_TYPE_DATA8);
    tt_want_uint_op(mode_info[1].writable, ==, 1);

    tt_want_uint_op(mode_info[2].num_values, ==, 1);
    tt_want_uint_op(mode_info[2].data_type, ==, LUMP_DATA_TYPE_DATA32);
    tt_want_uint_op(mode_info[2].writable, ==, 1);

    tt_want_uint_op(mode_info[3].num_values, ==, 1);
    tt_want_uint_op(mode_info[3].data_type, ==, LUMP_DATA_TYPE_DATA16);
    tt_want_uint_op(mode_info[3].writable, ==, 1);

    tt_want_uint_op(mode_info[4].num_values, ==, 2);
    tt_want_uint_op(mode_info[4].data_type, ==, LUMP_DATA_TYPE_DATA16);
    tt_want_uint_op(mode_info[4].writable, ==, 0);

    tt_want_uint_op(mode_info[5].num_values, ==, 14);
    tt_want_uint_op(mode_info[5].data_type, ==, LUMP_DATA_TYPE_DATA16);
    tt_want_uint_op(mode_info[5].writable, ==, 0);



end:
    PBIO_OS_ASYNC_END(PBIO_SUCCESS);
}

static pbio_error_t test_technic_xl_motor(pbio_os_state_t *state, void *context) {
    // info messages captured from Technic XL Linear Motor with logic analyzer
    static const uint8_t msg2[] = { 0x40, 0x2F, 0x90 };
    static const uint8_t msg3[] = { 0x49, 0x05, 0x03, 0xB0 };
    static const uint8_t msg4[] = { 0x52, 0x00, 0xC2, 0x01, 0x00, 0x6E };
    static const uint8_t msg5[] = { 0x5F, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0xB4 };
    static const uint8_t msg6[] = { 0xA5, 0x00, 0x53, 0x54, 0x41, 0x54, 0x53, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x04, 0x00, 0x00, 0x00, 0x00, 0x1A };
    static const uint8_t msg7[] = { 0x9D, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x7F, 0x47, 0xA4 };
    static const uint8_t msg8[] = { 0x9D, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x42, 0xEA };
    static const uint8_t msg9[] = { 0x9D, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x7F, 0x47, 0xA6 };
    static const uint8_t msg10[] = { 0x95, 0x04, 0x4D, 0x49, 0x4E, 0x00, 0x24 };
    static const uint8_t msg11[] = { 0x8D, 0x05, 0x00, 0x00, 0x77 };
    static const uint8_t msg12[] = { 0x95, 0x80, 0x0E, 0x01, 0x05, 0x00, 0xE0 };
    static const uint8_t msg13[] = { 0xA4, 0x00, 0x43, 0x41, 0x4C, 0x49, 0x42, 0x00, 0x22, 0x40, 0x00, 0x00, 0x05, 0x04, 0x00, 0x00, 0x00, 0x00, 0x7D };
    static const uint8_t msg14[] = { 0x9C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x61, 0x45, 0x46 };
    static const uint8_t msg15[] = { 0x9C, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x42, 0xEB };
    static const uint8_t msg16[] = { 0x9C, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x61, 0x45, 0x44 };
    static const uint8_t msg17[] = { 0x94, 0x04, 0x43, 0x41, 0x4C, 0x00, 0x21 };
    static const uint8_t msg18[] = { 0x8C, 0x05, 0x00, 0x00, 0x76 };
    static const uint8_t msg19[] = { 0x94, 0x80, 0x02, 0x01, 0x05, 0x00, 0xED };
    static const uint8_t msg20[] = { 0xA3, 0x00, 0x41, 0x50, 0x4F, 0x53, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00, 0x05, 0x04, 0x00, 0x00, 0x00, 0x00, 0x72 };
    static const uint8_t msg21[] = { 0x9B, 0x01, 0x00, 0x00, 0x34, 0xC3, 0x00, 0x00, 0x33, 0x43, 0xE2 };
    static const uint8_t msg22[] = { 0x9B, 0x02, 0x00, 0x00, 0x48, 0xC3, 0x00, 0x00, 0x48, 0x43, 0xE6 };
    static const uint8_t msg23[] = { 0x9B, 0x03, 0x00, 0x00, 0x34, 0xC3, 0x00, 0x00, 0x33, 0x43, 0xE0 };
    static const uint8_t msg24[] = { 0x93, 0x04, 0x44, 0x45, 0x47, 0x00, 0x2E };
    static const uint8_t msg25[] = { 0x8B, 0x05, 0x32, 0x32, 0x71 };
    static const uint8_t msg26[] = { 0x93, 0x80, 0x01, 0x01, 0x03, 0x00, 0xEF };
    static const uint8_t msg27[] = { 0xA2, 0x00, 0x50, 0x4F, 0x53, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x05, 0x04, 0x00, 0x00, 0x00, 0x00, 0x34 };
    static const uint8_t msg28[] = { 0x9A, 0x01, 0x00, 0x00, 0xB4, 0xC3, 0x00, 0x00, 0xB4, 0x43, 0xE4 };
    static const uint8_t msg29[] = { 0x9A, 0x02, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE7 };
    static const uint8_t msg30[] = { 0x9A, 0x03, 0x00, 0x00, 0xB4, 0xC3, 0x00, 0x00, 0xB4, 0x43, 0xE6 };
    static const uint8_t msg31[] = { 0x92, 0x04, 0x44, 0x45, 0x47, 0x00, 0x2F };
    static const uint8_t msg32[] = { 0x8A, 0x05, 0x28, 0x68, 0x30 };
    static const uint8_t msg33[] = { 0x92, 0x80, 0x01, 0x02, 0x0B, 0x00, 0xE5 };
    static const uint8_t msg34[] = { 0xA1, 0x00, 0x53, 0x50, 0x45, 0x45, 0x44, 0x00, 0x21, 0x00, 0x00, 0x00, 0x05, 0x04, 0x00, 0x00, 0x00, 0x00, 0x39 };
    static const uint8_t msg35[] = { 0x99, 0x01, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE7 };
    static const uint8_t msg36[] = { 0x99, 0x02, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE4 };
    static const uint8_t msg37[] = { 0x99, 0x03, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE5 };
    static const uint8_t msg38[] = { 0x91, 0x04, 0x50, 0x43, 0x54, 0x00, 0x2D };
    static const uint8_t msg39[] = { 0x89, 0x05, 0x30, 0x70, 0x33 };
    static const uint8_t msg40[] = { 0x91, 0x80, 0x01, 0x00, 0x04, 0x00, 0xEB };
    static const uint8_t msg41[] = { 0xA0, 0x00, 0x50, 0x4F, 0x57, 0x45, 0x52, 0x00, 0x30, 0x00, 0x00, 0x00, 0x05, 0x04, 0x00, 0x00, 0x00, 0x00, 0x31 };
    static const uint8_t msg42[] = { 0x98, 0x01, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE6 };
    static const uint8_t msg43[] = { 0x98, 0x02, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE5 };
    static const uint8_t msg44[] = { 0x98, 0x03, 0x00, 0x00, 0xC8, 0xC2, 0x00, 0x00, 0xC8, 0x42, 0xE4 };
    static const uint8_t msg45[] = { 0x90, 0x04, 0x50, 0x43, 0x54, 0x00, 0x2C };
    static const uint8_t msg46[] = { 0x88, 0x05, 0x00, 0x50, 0x22 };
    static const uint8_t msg47[] = { 0x90, 0x80, 0x01, 0x00, 0x04, 0x00, 0xEA };
    static const uint8_t msg48[] = { 0x88, 0x06, 0x0E, 0x00, 0x7F };
    static const uint8_t msg49[] = { 0xA0, 0x08, 0x80, 0x21, 0x00, 0x1C, 0x16, 0x47, 0x38, 0x34, 0x34, 0x38, 0x35, 0x32, 0x00, 0x00, 0x00, 0x00, 0xBC };
    static const uint8_t msg50[] = { 0xA0, 0x09, 0x28, 0x23, 0x00, 0x00, 0xFA, 0x00, 0x00, 0x00, 0x28, 0x23, 0x00, 0x00, 0xF5, 0x05, 0x00, 0x00, 0x5C };
    static const uint8_t msg51[] = { 0xA0, 0x0A, 0xF8, 0x2A, 0x00, 0x00, 0x78, 0x00, 0x00, 0x00, 0xC8, 0xAF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x98 };
    static const uint8_t msg52[] = { 0x98, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6C };
    static const uint8_t msg53[] = { 0x90, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x63 };
    static const uint8_t msg54[] = { 0x04 };

    static const uint8_t msg55[] = { 0x04 }; // ACK

    static const uint8_t msg56[] = { 0x43, 0x04, 0xB8 }; // set default mode

    static const uint8_t msg57[] = { 0xC0 | 0x10 | 0x04, 0x00, 0x00, 0x00, 0x00, 0x2B }; // mode 4, data 0, 0

    static const uint8_t msg58[] = { 0x02 }; // NACK

    // used in SIMULATE_RX/TX_MSG macros
    static pbio_os_state_t child;

    static pbio_port_t *port;
    static pbio_port_lump_dev_t *lump_dev;
    static lego_device_type_id_t type_id;
    static pbio_port_lump_mode_info_t *mode_info;
    static uint8_t current_mode;
    static uint8_t num_modes;
    static pbio_error_t err;

    PBIO_OS_ASYNC_BEGIN(state);


    // Expect no device at first.
    static lego_device_type_id_t expected_id = LEGO_DEVICE_TYPE_ID_TECHNIC_XL_MOTOR;
    tt_uint_op(pbio_port_get_port(PBIO_PORT_ID_D, &port), ==, PBIO_SUCCESS);

    // baud rate for sync messages
    PBIO_OS_AWAIT_UNTIL(state, ({
        pbio_test_clock_tick(1);
        test_uart.baud == 115200;
    }));

    // this device supports syncing at 115200
    SIMULATE_TX_MSG(msg_speed_115200);
    SIMULATE_RX_MSG(msg_ack);

    // send Technic XL Motor info
    SIMULATE_RX_MSG(msg2);
    SIMULATE_RX_MSG(msg3);
    SIMULATE_RX_MSG(msg4);
    SIMULATE_RX_MSG(msg5);
    SIMULATE_RX_MSG(msg6);
    SIMULATE_RX_MSG(msg7);
    SIMULATE_RX_MSG(msg8);
    SIMULATE_RX_MSG(msg9);
    SIMULATE_RX_MSG(msg10);
    SIMULATE_RX_MSG(msg11);
    SIMULATE_RX_MSG(msg12);
    SIMULATE_RX_MSG(msg13);
    SIMULATE_RX_MSG(msg14);
    SIMULATE_RX_MSG(msg15);
    SIMULATE_RX_MSG(msg16);
    SIMULATE_RX_MSG(msg17);
    SIMULATE_RX_MSG(msg18);
    SIMULATE_RX_MSG(msg19);
    SIMULATE_RX_MSG(msg20);
    SIMULATE_RX_MSG(msg21);
    SIMULATE_RX_MSG(msg22);
    SIMULATE_RX_MSG(msg23);
    SIMULATE_RX_MSG(msg24);
    SIMULATE_RX_MSG(msg25);
    SIMULATE_RX_MSG(msg26);
    SIMULATE_RX_MSG(msg27);
    SIMULATE_RX_MSG(msg28);
    SIMULATE_RX_MSG(msg29);
    SIMULATE_RX_MSG(msg30);
    SIMULATE_RX_MSG(msg31);
    SIMULATE_RX_MSG(msg32);
    SIMULATE_RX_MSG(msg33);
    SIMULATE_RX_MSG(msg34);
    SIMULATE_RX_MSG(msg35);
    SIMULATE_RX_MSG(msg36);
    SIMULATE_RX_MSG(msg37);
    SIMULATE_RX_MSG(msg38);
    SIMULATE_RX_MSG(msg39);
    SIMULATE_RX_MSG(msg40);
    SIMULATE_RX_MSG(msg41);
    SIMULATE_RX_MSG(msg42);
    SIMULATE_RX_MSG(msg43);
    SIMULATE_RX_MSG(msg44);
    SIMULATE_RX_MSG(msg45);
    SIMULATE_RX_MSG(msg46);
    SIMULATE_RX_MSG(msg47);
    SIMULATE_RX_MSG(msg48);
    SIMULATE_RX_MSG(msg49);
    SIMULATE_RX_MSG(msg50);
    SIMULATE_RX_MSG(msg51);
    SIMULATE_RX_MSG(msg52);
    SIMULATE_RX_MSG(msg53);
    SIMULATE_RX_MSG(msg54);

    // ensure that baud rate didn't change during sync
    tt_want_uint_op(test_uart.baud, ==, 115200);

    // wait for ACK
    SIMULATE_TX_MSG(msg55);


    // Simulate setting default mode
    SIMULATE_TX_MSG(msg56);

    // should be synced now are receive regular pings
    static int i;
    for (i = 0; i < 10; i++) {
        // receive data
        SIMULATE_RX_MSG(msg57);

        // wait for NACK
        SIMULATE_TX_MSG(msg58);
    }

    PBIO_OS_AWAIT_WHILE(state, ({
        pbio_test_clock_tick(1);
        (err = pbio_port_get_lump_device(port, &expected_id, &lump_dev)) == PBIO_ERROR_AGAIN;
    }));
    tt_uint_op(err, ==, PBIO_SUCCESS);

    type_id = LEGO_DEVICE_TYPE_ID_ANY_LUMP_UART;
    tt_uint_op(pbio_port_lump_assert_type_id(lump_dev, &type_id), ==, PBIO_SUCCESS);
    tt_uint_op(pbio_port_lump_get_info(lump_dev, &num_modes, &current_mode, &mode_info), ==, PBIO_SUCCESS);

    tt_want_uint_op(type_id, ==, LEGO_DEVICE_TYPE_ID_TECHNIC_XL_MOTOR);
    tt_want_uint_op(num_modes, ==, 6);
    tt_want_uint_op(current_mode, ==, LEGO_DEVICE_MODE_PUP_ABS_MOTOR__CALIB);

    tt_want_uint_op(mode_info[0].num_values, ==, 1);
    tt_want_uint_op(mode_info[0].data_type, ==, LUMP_DATA_TYPE_DATA8);
    tt_want_uint_op(mode_info[0].writable, ==, 1);

    tt_want_uint_op(mode_info[1].num_values, ==, 1);
    tt_want_uint_op(mode_info[1].data_type, ==, LUMP_DATA_TYPE_DATA8);
    tt_want_uint_op(mode_info[1].writable, ==, 1);

    tt_want_uint_op(mode_info[2].num_values, ==, 1);
    tt_want_uint_op(mode_info[2].data_type, ==, LUMP_DATA_TYPE_DATA32);
    tt_want_uint_op(mode_info[2].writable, ==, 1);

    tt_want_uint_op(mode_info[3].num_values, ==, 1);
    tt_want_uint_op(mode_info[3].data_type, ==, LUMP_DATA_TYPE_DATA16);
    tt_want_uint_op(mode_info[3].writable, ==, 1);

    tt_want_uint_op(mode_info[4].num_values, ==, 2);
    tt_want_uint_op(mode_info[4].data_type, ==, LUMP_DATA_TYPE_DATA16);
    tt_want_uint_op(mode_info[4].writable, ==, 0);

    tt_want_uint_op(mode_info[5].num_values, ==, 14);
    tt_want_uint_op(mode_info[5].data_type, ==, LUMP_DATA_TYPE_DATA16);
    tt_want_uint_op(mode_info[5].writable, ==, 0);



end:
    PBIO_OS_ASYNC_END(PBIO_SUCCESS);
}

struct testcase_t pbio_port_lump_tests[] = {
    PBIO_PT_THREAD_TEST_WITH_PBIO_OS(test_boost_color_distance_sensor),
    PBIO_PT_THREAD_TEST_WITH_PBIO_OS(test_boost_interactive_motor),
    PBIO_PT_THREAD_TEST_WITH_PBIO_OS(test_technic_large_motor),
    PBIO_PT_THREAD_TEST_WITH_PBIO_OS(test_technic_xl_motor),
    END_OF_TESTCASES
};

pbio_error_t pbdrv_uart_get_instance(uint8_t id, pbdrv_uart_dev_t **uart_dev) {
    if (id != 0) {
        return PBIO_ERROR_NO_DEV;
    }
    *uart_dev = &test_uart;
    return PBIO_SUCCESS;
}

void pbdrv_uart_set_baud_rate(pbdrv_uart_dev_t *uart_dev, uint32_t baud) {
    uart_dev->baud = baud;
}

void pbdrv_uart_flush(pbdrv_uart_dev_t *uart_dev) {
}

extern bool pbio_lump_dev_test_process_auto_start;

void pbdrv_uart_init(void) {
}

void pbdrv_uart_stop(pbdrv_uart_dev_t *uart_dev) {
}

pbio_error_t pbdrv_uart_read(pbio_os_state_t *state, pbdrv_uart_dev_t *uart_dev, uint8_t *msg, uint8_t length, uint32_t timeout) {

    PBIO_OS_ASYNC_BEGIN(state);

    PBIO_OS_AWAIT_WHILE(state, uart_dev->rx_msg);

    uart_dev->rx_msg = msg;
    uart_dev->rx_msg_length = length;
    uart_dev->rx_msg_result = PBIO_ERROR_AGAIN;
    pbio_os_timer_set(&uart_dev->rx_timer, timeout);

    // If read_pos is less that read_length then we have not read everything yet
    PBIO_OS_AWAIT_WHILE(state, uart_dev->rx_msg_result == PBIO_ERROR_AGAIN && !pbio_os_timer_is_expired(&uart_dev->rx_timer));
    if (pbio_os_timer_is_expired(&uart_dev->rx_timer)) {
        uart_dev->rx_msg_result = PBIO_ERROR_TIMEDOUT;
    }

    if (uart_dev->rx_msg_result != PBIO_ERROR_AGAIN) {
        uart_dev->rx_msg = NULL;
    }

    PBIO_OS_ASYNC_END(uart_dev->rx_msg_result);
}

pbio_error_t pbdrv_uart_write(pbio_os_state_t *state, pbdrv_uart_dev_t *uart_dev, uint8_t *msg, uint8_t length, uint32_t timeout) {

    PBIO_OS_ASYNC_BEGIN(state);

    // Wait while other write operation already in progress.
    PBIO_OS_AWAIT_WHILE(state, uart_dev->tx_msg);

    uart_dev->tx_msg = msg;
    uart_dev->tx_msg_length = length;
    uart_dev->tx_msg_result = PBIO_ERROR_AGAIN;
    pbio_os_timer_set(&uart_dev->tx_timer, timeout);

    PBIO_OS_AWAIT_WHILE(state, uart_dev->tx_msg_result == PBIO_ERROR_AGAIN && !pbio_os_timer_is_expired(&uart_dev->tx_timer));
    if (pbio_os_timer_is_expired(&uart_dev->tx_timer)) {
        uart_dev->tx_msg_result = PBIO_ERROR_TIMEDOUT;
    }

    if (uart_dev->tx_msg_result != PBIO_ERROR_AGAIN) {
        uart_dev->tx_msg = NULL;
    }

    PBIO_OS_ASYNC_END(uart_dev->tx_msg_result);
}
