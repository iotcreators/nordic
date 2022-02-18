/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr.h>
#include <stdio.h>
#include <modem/lte_lc.h> 		// LTE link control
#include <modem/at_cmd.h> 		// includes AT command handling
#include <modem/modem_info.h> 	// includes modem info module
#include <net/socket.h>			// includes TCP/IP socket handling
#include <device.h> 			// Zephyr device API
#include <drivers/sensor.h> 	// Zephyr sensor driver API

/* UI drivers for Thingy:91 */
#include "buzzer.h"
#include "ui.h"
#include "led_pwm.h"

/* Defines */
#define MODEM_APN "cdp.iot.t-mobile.nl"
#define TEMP_CALIBRATION_OFFSET 3

/* Sensor data */
const struct device *bme_680;
struct sensor_value temp, press, humidity, gas_res;
float pressure;

/* UDP Socket */
static int client_fd;
static bool socket_open = false;
static struct sockaddr_storage host_addr;

/* Workqueues */
static struct k_work_delayable server_transmission_work;
static struct k_work_delayable data_fetch_work;

K_SEM_DEFINE(lte_connected, 0, 1);
struct modem_param_info modem_param;

/* 
 * *** Static functions of main.c ***
 */
static void fetch_sensor_data(void)
{
	sensor_sample_fetch(bme_680);
	sensor_channel_get(bme_680, SENSOR_CHAN_AMBIENT_TEMP, &temp);
	sensor_channel_get(bme_680, SENSOR_CHAN_PRESS, &press);
	sensor_channel_get(bme_680, SENSOR_CHAN_HUMIDITY, &humidity);
	sensor_channel_get(bme_680, SENSOR_CHAN_GAS_RES, &gas_res);
	
	/* apply calibration offsets */
	temp.val1 = temp.val1 - TEMP_CALIBRATION_OFFSET;

	/* Debug print raw sensor data from Zephyr API */
	// printk("[DEBUG] T: %d.%06d; P: %d.%06d; H: %d.%06d; G: %d.%06d\n",
	// 	temp.val1, temp.val2, press.val1, press.val2,
	// 	humidity.val1, humidity.val2, gas_res.val1,
	// 	gas_res.val2);

	/* format data, shrink factional part length of sensor_value to 2 digits */
	temp.val2 = temp.val2/10000;
	humidity.val2 = humidity.val2/10000;

	/* convert pressure in Bar - sensor_value struct stores 2 integers (dec + fraction) */
	pressure = (float)press.val1 + (float)press.val2/1000000; //reformat to float for conversion
	pressure = pressure/100; //convert in Bar, but skip storing back into integer parts
}

static void transmit_udp_data(char *data, size_t len)
{
	int err;
	if (data != NULL){
		printk("Sending UDP payload, length: %u, data: %s\n", len, data);
		err = send(client_fd, data, len, 0);
		if (err < 0) {
			printk("Failed to transmit UDP packet, %d\n", errno);
		}
	}
}

/* Event Handler - used when pressing the button */
static void ui_evt_handler(struct ui_evt evt)
{
	if (evt.type == 1){
		if (socket_open){
			char data_output[128];
			char imei[16];
			modem_info_string_get(MODEM_INFO_IMEI, imei, sizeof(imei));
			//sprintf(data_output, "{\"Msg\":\"Hello MWC 2022 from Thingy:91 IMEI %s\"}",imei);
			sprintf(data_output, "{\"Msg\":\"Event: Thingy:91 button pressed, %s\"}",imei);
			transmit_udp_data(data_output, strlen(data_output));
		}
	}
}

static void data_fetch_work_fn(struct k_work *work)
{
	/* Update sensor + modem data */
	fetch_sensor_data();
	modem_info_params_get(&modem_param);

	/* Reschedule work task */
	k_work_schedule(&data_fetch_work, K_SECONDS(CONFIG_UDP_DATA_UPLOAD_FREQUENCY_SECONDS));
}

static void initial_data_transmission(void)
{
	/* Get current modem parameters */
	modem_info_params_get(&modem_param);
	char data_output[128];
	char imei[16];
	char operator[8];
	modem_info_string_get(MODEM_INFO_IMEI, imei, sizeof(imei));
	modem_info_string_get(MODEM_INFO_OPERATOR, operator, sizeof(operator));

	/* format to JSON */
	sprintf(data_output, "{\"Msg\":\"Event: Thingy:91 reconnected, %s, %s, %d\",\"Oper\":\"%s\",\"Bd\":%d}",
		imei, operator, modem_param.network.current_band.value, operator, modem_param.network.current_band.value);
	/* Transmit data via UDP Socket */
	transmit_udp_data(data_output, strlen(data_output));
}

static void server_transmission_work_fn(struct k_work *work)
{
	/* Format sensor data to JSON */
	char data_output[128];

	/* format to JSON */
	sprintf(data_output,"{\"Temp\":%d.%02d,\"Press\":%.4f,\"Humid\":%d.%02d,\"Gas\":%d,\"Vbat\":%d}",
		temp.val1, temp.val2, pressure,
		humidity.val1, humidity.val2, gas_res.val1, modem_param.device.battery.value);

	/* Transmit data via UDP Socket */
	transmit_udp_data(data_output, strlen(data_output));

	/* Reschedule work task */
	k_work_schedule(&server_transmission_work, K_SECONDS(CONFIG_UDP_DATA_UPLOAD_FREQUENCY_SECONDS));
}

static void work_init(void)
{
	k_work_init_delayable(&server_transmission_work, server_transmission_work_fn);
	k_work_init_delayable(&data_fetch_work, data_fetch_work_fn);
}

#if defined(CONFIG_NRF_MODEM_LIB)
static void lte_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
		     (evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
			socket_open=false;
			ui_led_set_effect(UI_LTE_CONNECTING);
			break;
		}
		printk("[LTE] Network registration status: %s\n",
			evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ?
			"Connected - home network" : "Connected - roaming");
		k_sem_give(&lte_connected);
		ui_led_set_effect(UI_LTE_CONNECTED);
		break;
	case LTE_LC_EVT_PSM_UPDATE:
		printk("[LTE] PSM parameter update: TAU: %d, Active time: %d\n",
			evt->psm_cfg.tau, evt->psm_cfg.active_time);
		break;
	case LTE_LC_EVT_EDRX_UPDATE: {
		char log_buf[60];
		ssize_t len;

		len = snprintf(log_buf, sizeof(log_buf),
			       "[LTE] eDRX parameter update: eDRX: %f, PTW: %f\n",
			       evt->edrx_cfg.edrx, evt->edrx_cfg.ptw);
		if (len > 0) {
			printk("%s\n", log_buf);
		}
		break;
	}
	case LTE_LC_EVT_RRC_UPDATE:
		printk("[LTE] RRC mode: %s\n",
			evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ?
			"Connected" : "Idle");
		break;
	case LTE_LC_EVT_CELL_UPDATE:
		printk("[LTE] LTE cell changed: Cell ID: %d, Tracking area: %d\n",
		       evt->cell.id, evt->cell.tac);
		break;
	default:
		break;
	}
}

static int configure_low_power(void)
{
	int err;

#if defined(CONFIG_UDP_PSM_ENABLE)
	/** Power Saving Mode */
	err = lte_lc_psm_req(true);
	if (err) {
		printk("lte_lc_psm_req, error: %d\n", err);
	}
#else
	err = lte_lc_psm_req(false);
	if (err) {
		printk("lte_lc_psm_req, error: %d\n", err);
	}
#endif

#if defined(CONFIG_UDP_EDRX_ENABLE)
	/** enhanced Discontinuous Reception */
	err = lte_lc_edrx_req(true);
	if (err) {
		printk("lte_lc_edrx_req, error: %d\n", err);
	}
#else
	err = lte_lc_edrx_req(false);
	if (err) {
		printk("lte_lc_edrx_req, error: %d\n", err);
	}
#endif

#if defined(CONFIG_UDP_RAI_ENABLE)
	/** Release Assistance Indication  */
	err = lte_lc_rai_req(true);
	if (err) {
		printk("lte_lc_rai_req, error: %d\n", err);
	}
#endif

	return err;
}

static void modem_init(void)
{
	int err;
	char response[128];

	if (IS_ENABLED(CONFIG_LTE_AUTO_INIT_AND_CONNECT)) {
		/* Do nothing, modem is already configured and LTE connected. */
	} else {
		err = lte_lc_init();
		if (err) {
			printk("Modem initialization failed, error: %d\n", err);
			return;
		}

		/* Enable 3GGP CME error codes*/
		at_cmd_write("AT+CMEE=1", NULL, 0, NULL);
		/* Read out Modem IMEI */
		printk("[INFO] Read Modem IMEI\n");
		at_cmd_write("AT+CGSN=1", response, sizeof(response), NULL);
		if (err) {
			printk("Read Modem IMEI failed, err %d\n", err);
			return;
		}else{
			printk("[AT] %s", response);
		}

		/* Setup APN for the PDP Context */
		printk("[INFO] Setting up the APN\n");
		char *apn_stat = "AT%XAPNSTATUS=1,\"" MODEM_APN "\"";
		char *at_cgdcont = "AT+CGDCONT=0,\"IPV4V6\",\"" MODEM_APN "\"";
		at_cmd_write(apn_stat, NULL, 0, NULL); // allow use of APN
		err = at_cmd_write(at_cgdcont, NULL, 0, NULL); // use APN for PDP context 0 (default bearer)
		if (err) {
			printk("AT+CGDCONT set cmd failed, err %d\n", err);
			return;
		}
		err = at_cmd_write("AT+CGDCONT?", response, sizeof(response), NULL);
		if (err) {
			printk("APN check failed, err %d\n", err);
			return;
		}else{
			printk("[AT] %s", response);
		}
		/* Init modem info module */
		modem_info_init();
		modem_info_params_init(&modem_param);
	}
}

static void modem_connect(void)
{
	int err;

	if (IS_ENABLED(CONFIG_LTE_AUTO_INIT_AND_CONNECT)) {
		/* Do nothing, modem is already configured and LTE connected. */
	} else {
		err = lte_lc_connect_async(lte_handler);
		if (err) {
			printk("Connecting to LTE network failed, error: %d\n",
			       err);
			return;
		}
	}
}
#endif

static void server_disconnect(void)
{
	(void)close(client_fd);
}

static int server_init(void)
{
	struct sockaddr_in *server4 = ((struct sockaddr_in *)&host_addr);

	server4->sin_family = AF_INET;
	server4->sin_port = htons(CONFIG_UDP_SERVER_PORT);

	inet_pton(AF_INET, CONFIG_UDP_SERVER_ADDRESS_STATIC,
		  &server4->sin_addr);

	return 0;
}

static int server_connect(void)
{
	int err;

	client_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (client_fd < 0) {
		printk("Failed to create UDP socket: %d\n", errno);
		err = -errno;
		goto error;
	}

	err = connect(client_fd, (struct sockaddr *)&host_addr,
		      sizeof(struct sockaddr_in));
	if (err < 0) {
		printk("Connect failed : %d\n", errno);
		goto error;
	}
	socket_open = true;
	return 0;

error:
	server_disconnect();

	return err;
}

/* 
 * *** Main Applicatin Entry ***
 */
void main(void)
{
	int err;

	printk("\n*** IoT Creators Demo Application started ***\n");

	/* Init routines */
	ui_init(ui_evt_handler);
	ui_led_set_effect(UI_LTE_CONNECTING);
	bme_680 = device_get_binding(DT_LABEL(DT_INST(0, bosch_bme680)));
	work_init();

	/* Initialize the modem before calling configure_low_power(). This is
	 * because the enabling of RAI is dependent on the
	 * configured network mode which is set during modem initialization.
	 */
	modem_init();
	err = configure_low_power();
	if (err) {
		printk("Unable to set low power configuration, error: %d\n",err);
	}

	modem_connect();
	k_sem_take(&lte_connected, K_FOREVER);
	k_msleep(500);

	/* Init UDP Socket */
	err = server_init();
	if (err) {
		printk("Not able to initialize UDP server connection\n");
		return;
	}

	/* Connect UDP Socket */
	err = server_connect();
	if (err) {
		printk("Not able to connect to UDP server\n");
		return;
	}

	/* Perform initial data transmission & schedule periodic tasks */
	initial_data_transmission();
	k_work_schedule(&data_fetch_work, K_NO_WAIT);
	k_work_schedule(&server_transmission_work, K_SECONDS(2));	
}
