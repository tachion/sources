#include "user_config.h"
#if MOD_EMTR_ENABLE

#include "ets_sys.h"
#include "osapi.h"
#include "mem.h"
#include "stdout.h"

#include "user_misc.h"
#include "user_devices.h"
#include "user_json.h"

#include "user_relay.h"
#include "user_switch1.h"
#include "user_switch2.h"

#include "driver/uart.h"
#include "mod_emtr.h"

LOCAL emtr_packet *emtr_receive_packet = NULL;

LOCAL bool emtr_ask_only = false;

LOCAL emtr_callback emtr_command_done     = NULL;
LOCAL emtr_callback emtr_single_wire_done = NULL;
LOCAL void_func     emtr_command_timeout  = NULL;
LOCAL emtr_callback emtr_user_callback    = NULL;

LOCAL uint32 emtr_timeout = 0;

LOCAL emtr_sys_params emtr_params = {
	.address = 0,
	.counter_active = 0,
	.counter_apparent = 0,
	.apparent_divisor = 1,
	.relays = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

void ICACHE_FLASH_ATTR emtr_clear_timeout() {
	emtr_timeout = 0;
}

void ICACHE_FLASH_ATTR emtr_set_timeout_callback(void_func command_timeout) {
	emtr_command_timeout = command_timeout;
}

LOCAL uint8 ICACHE_FLASH_ATTR emtr_check_sum(emtr_packet *packet) {
	if (packet->len == 0 || (packet->ask_only && packet->header != EMTR_START_FRAME)) {
		return 0;
	}
	
	uint8 check_sum = 0;
	if (packet->header == EMTR_SINGLE_WIRE_01) {
		check_sum = 0;
	} else {
		check_sum = packet->header + packet->len;
	}
	
	uint8 i;
	for (i=0; i<packet->len-3; i++) {
		check_sum += packet->data[i];
	}
	return (packet->header == EMTR_SINGLE_WIRE_01 ? 
		~check_sum 
		:
		check_sum
	);
}

LOCAL void ICACHE_FLASH_ATTR emtr_packet_init(emtr_packet **packet, uint8 header, uint8 len) {
	if (*packet == NULL) {
		*packet = (emtr_packet *)os_zalloc(sizeof(emtr_packet));
	}
	
	(*packet)->header = header;
	(*packet)->len = len;
	(*packet)->ask_only = false;
	(*packet)->check_sum = 0;
	
	if ((*packet)->data != NULL) {
		os_free((*packet)->data);
	}
	
	(*packet)->data = (len == 0 ?
		NULL
		:
		(uint8 *)os_zalloc(len-2)
	);
}

LOCAL void ICACHE_FLASH_ATTR emtr_receive(emtr_packet *packet) {
	if (packet == NULL) {
		debug("EMTR: Not initialized\n");
		goto clear;
	}
	
	if (packet->header == EMTR_ERROR_NAK) {
		debug("EMTR: Not acknowledged\n");
		goto clear;
	}
	
	if (packet->header == EMTR_ERROR_CRC) {
		debug("EMTR: Wrong checksum\n");
		goto clear;
	}
	
	if (packet->check_sum != emtr_check_sum(packet)) {
		debug("EMTR: Checksum not match [0x%02X] [0x%02X]\n", packet->check_sum, emtr_check_sum(packet));
#if EMTR_DEBUG
#if EMTR_VERBOSE_OUTPUT
		debug("Header: 0x%02X\n", packet->header);
		debug("Len: 0x%02X\n", packet->len);
		debug("ASK only: 0x%02X\n", packet->ask_only);
#endif
#endif
		goto clear;
	}
	
	device_set_uart(UART_EMTR);
	
#if EMTR_DEBUG
#if EMTR_VERBOSE_OUTPUT
	debug("\nEMTR: Received packet\n");
	debug("Header: 0x%02X\n", packet->header);
	debug("Len: 0x%02X\n", packet->len);
	debug("ASK only: 0x%02X\n", packet->ask_only);
	if (packet->len > 0) {
		debug("Data: ");
		uint16 j;
		for (j=0; j<packet->len-2; j++) {
			debug("0x%02X ", packet->data[j]);
		}
	}
	debug("\n\n");
#endif
#endif
	
	if (emtr_command_done != NULL) {
		(*emtr_command_done)(packet);
	}
#if EMTR_DEBUG
	else {
		debug("EMTR: ASK\n");
	}
#endif
	
clear:	
	clearTimeout(emtr_timeout);
	emtr_clear_timeout();
}

LOCAL void ICACHE_FLASH_ATTR emtr_single_wire(emtr_packet *packet) {
	if (packet->check_sum != emtr_check_sum(packet)) {
		debug("EMTR: Single wire checksum not match [0x%02X] [0x%02X]\n", packet->check_sum, emtr_check_sum(packet));
#if EMTR_DEBUG
#if EMTR_VERBOSE_OUTPUT
		debug("\nEMTR: Received packet\n");
		debug("Header: 0x%02X\n", packet->header);
		debug("Len: 0x%02X\n", packet->len);
		debug("ASK only: 0x%02X\n", packet->ask_only);
		if (packet->len > 0) {
			debug("Data: ");
			uint16 j;
			for (j=0; j<packet->len-2; j++) {
				debug("0x%02X ", packet->data[j]);
			}
		}
		debug("\n\n");
#endif
#endif
		return;
	}
	if (emtr_single_wire_done != NULL) {
		(*emtr_single_wire_done)(packet);
	}
}

LOCAL void ICACHE_FLASH_ATTR emtr_char_in(char c) {
LOCAL emtr_state state = EMTR_STATE_IDLE;
LOCAL uint8 count = 0;
LOCAL bool  single_wire = false;
	
	if (state == EMTR_STATE_IDLE || state == EMTR_STATE_ERROR) {
		single_wire = false;
		emtr_packet_init(&emtr_receive_packet, EMTR_ACK_FRAME, 0);
		
		if (c == EMTR_SINGLE_WIRE_01) {
			count = 0;
			single_wire = true;
			emtr_receive_packet->header = c;
			state = EMTR_STATE_SINGLE_WIRE;
			return;
		}
		
		if (c == EMTR_ACK_FRAME) {
			count = 1;
			if (emtr_ask_only) {
				state = EMTR_STATE_IDLE;
				emtr_receive_packet->ask_only = true;
				emtr_receive(emtr_receive_packet);
			} else {
				state = EMTR_STATE_LEN;
			}
			return;
		}
		
		if (c == EMTR_ERROR_CRC || c == EMTR_ERROR_NAK) {
			emtr_receive_packet->header = c;
			emtr_receive_packet->ask_only = true;
			emtr_receive(emtr_receive_packet);
		}
		
		count = 0;
		state = EMTR_STATE_ERROR;
		return;
	}
	
	if (state == EMTR_STATE_SINGLE_WIRE) {
		count++;
		
		if (c == EMTR_SINGLE_WIRE_02) {
			return;
		}
		
		if (c == EMTR_SINGLE_WIRE_03) {
			emtr_receive_packet->len = 15;
			state = EMTR_STATE_DATA;
			return;
		}
	}
	
	if (state == EMTR_STATE_LEN) {
		count++;
		emtr_receive_packet->len = (uint8)c;
		state = EMTR_STATE_DATA;
		return;
	}
	
	if (state == EMTR_STATE_DATA) {
		count++;
		
		if (count == 3) {
			if (emtr_receive_packet->data != NULL) {
				os_free(emtr_receive_packet->data);
			}
			emtr_receive_packet->data = (uint8 *)os_zalloc(emtr_receive_packet->len-2);
		}
		
		emtr_receive_packet->data[count-3] = (uint8)c;
		
		if (count == emtr_receive_packet->len) {
			emtr_receive_packet->check_sum = (uint8)c;
			state = EMTR_STATE_IDLE;
			count = 0;
			if (single_wire) {
				emtr_single_wire(emtr_receive_packet);
			} else {
				emtr_receive(emtr_receive_packet);
			}
		}
		return;
	}
}

LOCAL void ICACHE_FLASH_ATTR emtr_send(emtr_packet *packet) {
	if (emtr_timeout != 0) {
		setTimeout((os_timer_func_t *)emtr_send, packet, 100);
		return;
	}
	
	emtr_ask_only = packet->ask_only;
	emtr_command_done = packet->callback;
	packet->check_sum = emtr_check_sum(packet);
	packet->data[packet->len-3] = packet->check_sum;
	
#if EMTR_DEBUG
#if EMTR_VERBOSE_OUTPUT
	debug("\nEMTR: Sending packet%s...\n", packet->ask_only ? " [ASK-ONLY]" : "");
	debug("Header: 0x%02X\n", packet->header);
	debug("Len: 0x%02X\n", packet->len);
	debug("Checksum: 0x%02X\n", packet->check_sum);
	debug("Data: ");
	uint16 j;
	for (j=0; j<packet->len-2; j++) {
		debug("0x%02X ", packet->data[j]);
	}
	debug("\n\n");
#endif
#endif
	
	if (emtr_command_timeout == NULL) {
		emtr_set_timeout_callback(emtr_clear_timeout);
	}
	emtr_timeout = setTimeout((os_timer_func_t *)emtr_command_timeout, NULL, EMTR_TIMEOUT);
	
	// Start
	uart_write_byte(packet->header);
	
	// Len
	uart_write_byte(packet->len);
	
	// Data
	uart_write_buff(packet->data, packet->len-2);
	
	// Free allocated memory
	if (packet->data != NULL) {
		os_free(packet->data);
	}
	os_free(packet);
}

LOCAL _uint64_ emtr_extract_int(uint8 *data, uint8 offset, uint8 size) {
	uint8 i;
	_uint64_ pow = 1;
	_uint64_ result = 0;
	for (i=0; i<size; i++) {
		result = result + data[offset+i]*pow;
		pow = pow * 256;
	}
	return result;
}

LOCAL void ICACHE_FLASH_ATTR emtr_set_int(_uint64_ value, uint8 *data, uint8 offset, uint8 size) {
	uint8 i;
	for (i=0; i<size; i++) {
		data[offset+i] = ((value >> (i * 8)) & 0xFF);
	}
}

void ICACHE_FLASH_ATTR emtr_parse_single_wire(emtr_packet *packet, emtr_output_registers *registers) {
#if EMTR_DEBUG
	debug("emtr_parse_single_wire()\n");
#endif
	if (packet->len != 15) {
		os_memset(registers, 0, sizeof(emtr_output_registers));
#if EMTR_DEBUG
		debug("EMTR: clear output registers\n");
#endif
		return;
	}
	
	registers->current_rms    = emtr_extract_int(packet->data,  0, 4);
	registers->voltage_rms    = emtr_extract_int(packet->data,  4, 2);
	registers->active_power   = emtr_extract_int(packet->data,  6, 4);
	registers->line_frequency = emtr_extract_int(packet->data, 10, 2);
}

void ICACHE_FLASH_ATTR emtr_parse_output(emtr_packet *packet, emtr_output_registers *registers, bool calculate) {
#if EMTR_DEBUG
	debug("emtr_parse_output()\n");
#endif
	if (packet->len != EMTR_OUT_LEN+3) {
#if EMTR_DEBUG
		debug("EMTR: clear output registers\n");
#endif
		os_memset(registers, 0, sizeof(emtr_output_registers));
		return;
	}
	registers->current_rms         = emtr_extract_int(packet->data,  0, 4);
	registers->voltage_rms         = emtr_extract_int(packet->data,  4, 2);
	registers->active_power        = emtr_extract_int(packet->data,  6, 4);
	registers->reactive_power      = emtr_extract_int(packet->data, 10, 4);
	registers->apparent_power      = emtr_extract_int(packet->data, 14, 4);
	registers->power_factor        = emtr_extract_int(packet->data, 18, 2);
	registers->line_frequency      = emtr_extract_int(packet->data, 20, 2);
	registers->thermistor_voltage  = emtr_extract_int(packet->data, 22, 2);
	registers->event_flag          = emtr_extract_int(packet->data, 24, 2);
	registers->system_status       = emtr_extract_int(packet->data, 26, 2);
	
	if (emtr_relay(EMTR_RELAYS_COUNT - 1)) {
		if (registers->power_factor < 0x7FFF) {
			registers->power_factor = -registers->power_factor;
		}
	}
	
	if (calculate) {
		uint32 apparent_power = registers->current_rms * registers->voltage_rms;
		registers->apparent_power = apparent_power / emtr_params.apparent_divisor;
		
		if (registers->apparent_power < registers->active_power) {
			// theoretically impossible 
			registers->power_factor = 0x7FFF;
			registers->active_power = registers->apparent_power;
			registers->reactive_power = 0;
			return;
		}
		
		uint32 diff = registers->apparent_power - registers->active_power;
		if (
			registers->current_rms > 5
			&&
			(diff > 15 || diff > registers->apparent_power / 100)
		) {
			// calculating power factor and reactive power only if
			// current is greater than 0.005A and
			// difference between active and apparent power is greater than 1.5W or 1%
			
			double power_factor = (registers->power_factor < 0 ? -1.0 : 1.0) * registers->active_power / registers->apparent_power;
			registers->power_factor = power_factor * 0x7FFF;
			
			registers->reactive_power = round_sqrt_int(
				registers->apparent_power * registers->apparent_power
				-
				registers->active_power * registers->active_power
			);
		} else {
			registers->power_factor = 0x7FFF;
			registers->active_power = registers->apparent_power;
			registers->reactive_power = 0;
		}
	}
}

void ICACHE_FLASH_ATTR emtr_get_output(emtr_callback command_done) {
#if EMTR_DEBUG
	debug("emtr_get_output()\n");
#endif
	emtr_packet *packet = NULL;
	emtr_packet_init(&packet, EMTR_START_FRAME, 8);
	
	packet->data[0] = EMTR_SET_ADDRESS;            // Set address pointer
	packet->data[1] = (EMTR_OUT_BASE >> 8) & 0xFF; // address
	packet->data[2] = (EMTR_OUT_BASE >> 0) & 0xFF; // address
	
	packet->data[3] = EMTR_READ;                   // Read N bytes
	packet->data[4] = EMTR_OUT_LEN;
	
	packet->ask_only = false;
	packet->callback = command_done;
	
	emtr_send(packet);
}

void ICACHE_FLASH_ATTR emtr_parse_calibration(emtr_packet *packet, emtr_calibration_registers *registers) {
#if EMTR_DEBUG
	debug("emtr_parse_calibration()\n");
#endif
	
	if (packet->len != EMTR_CALIBRATION_LEN+3) {
		os_memset(registers, 0, sizeof(emtr_calibration_registers));
		return;
	}
	
	registers->gain_current_rms           = emtr_extract_int(packet->data,  0, 2);
	registers->gain_voltage_rms           = emtr_extract_int(packet->data,  2, 2);
	registers->gain_active_power          = emtr_extract_int(packet->data,  4, 2);
	registers->gain_reactive_power        = emtr_extract_int(packet->data,  6, 2);
	registers->offset_current_rms         = emtr_extract_int(packet->data,  8, 4);
	registers->offset_active_power        = emtr_extract_int(packet->data, 12, 4);
	registers->offset_reactive_power      = emtr_extract_int(packet->data, 16, 4);
	registers->dc_offset_current          = emtr_extract_int(packet->data, 20, 2);
	registers->phase_compensation         = emtr_extract_int(packet->data, 22, 2);
	registers->apparent_power_divisor     = emtr_extract_int(packet->data, 24, 2);
	registers->system_configuration       = emtr_extract_int(packet->data, 26, 4);
	registers->dio_configuration          = emtr_extract_int(packet->data, 30, 2);
	registers->range                      = emtr_extract_int(packet->data, 32, 4);

	registers->calibration_current        = emtr_extract_int(packet->data, 36, 4);
	registers->calibration_voltage        = emtr_extract_int(packet->data, 40, 2);
	registers->calibration_active_power   = emtr_extract_int(packet->data, 42, 4);
	registers->calibration_reactive_power = emtr_extract_int(packet->data, 46, 4);
	registers->accumulation_interval      = emtr_extract_int(packet->data, 50, 2);
	
	emtr_params.apparent_divisor = pow_int(10, registers->apparent_power_divisor);
}

void ICACHE_FLASH_ATTR emtr_get_calibration(emtr_callback command_done) {
#if EMTR_DEBUG
	debug("emtr_get_calibration()\n");
#endif
	emtr_packet *packet = NULL;
	emtr_packet_init(&packet, EMTR_START_FRAME, 8);
	
	packet->data[0] = EMTR_SET_ADDRESS;                    // Set address pointer
	packet->data[1] = (EMTR_CALIBRATION_BASE >> 8) & 0xFF; // address
	packet->data[2] = (EMTR_CALIBRATION_BASE >> 0) & 0xFF; // address
	
	packet->data[3] = EMTR_READ;                           // Read N bytes
	packet->data[4] = EMTR_CALIBRATION_LEN;
	
	packet->ask_only = false;
	packet->callback = command_done;
	
	emtr_send(packet);
}

void ICACHE_FLASH_ATTR emtr_set_calibration(emtr_calibration_registers *registers, emtr_callback command_done) {
#if EMTR_DEBUG
	debug("emtr_set_calibration()\n");
#endif
	emtr_params.apparent_divisor = pow_int(10, registers->apparent_power_divisor);
	
	emtr_packet *packet = NULL;
	emtr_packet_init(&packet, EMTR_START_FRAME, 2+5+EMTR_CALIBRATION_LEN+2+1);
	
	packet->data[0] = EMTR_SET_ADDRESS;                    // Set address pointer
	packet->data[1] = (EMTR_CALIBRATION_BASE >> 8) & 0xFF; // address
	packet->data[2] = (EMTR_CALIBRATION_BASE >> 0) & 0xFF; // address
	
	packet->data[3] = EMTR_WRITE;                          // Write N bytes
	packet->data[4] = EMTR_CALIBRATION_LEN;
	
	emtr_set_int(registers->gain_current_rms,            packet->data,  0+5, 2);
	emtr_set_int(registers->gain_voltage_rms,            packet->data,  2+5, 2);
	emtr_set_int(registers->gain_active_power,           packet->data,  4+5, 2);
	emtr_set_int(registers->gain_reactive_power,         packet->data,  6+5, 2);
	emtr_set_int(registers->offset_current_rms,          packet->data,  8+5, 4);
	emtr_set_int(registers->offset_active_power,         packet->data, 12+5, 4);
	emtr_set_int(registers->offset_reactive_power,       packet->data, 16+5, 4);
	emtr_set_int(registers->dc_offset_current,           packet->data, 20+5, 2);
	emtr_set_int(registers->phase_compensation,          packet->data, 22+5, 2);
	emtr_set_int(registers->apparent_power_divisor,      packet->data, 24+5, 2);
	
	emtr_set_int(registers->system_configuration,        packet->data, 26+5, 4);
	emtr_set_int(registers->dio_configuration,           packet->data, 30+5, 2);
	emtr_set_int(registers->range,                       packet->data, 32+5, 4);
	
	emtr_set_int(registers->calibration_current,         packet->data, 36+5, 4);
	emtr_set_int(registers->calibration_voltage,         packet->data, 40+5, 2);
	emtr_set_int(registers->calibration_active_power,    packet->data, 42+5, 4);
	emtr_set_int(registers->calibration_reactive_power,  packet->data, 46+5, 4);
	emtr_set_int(registers->accumulation_interval,       packet->data, 50+5, 2);
	
	packet->data[EMTR_CALIBRATION_LEN+5] = EMTR_SAVE_REGISTERS;  // Save registers to flash
	packet->data[EMTR_CALIBRATION_LEN+6] = emtr_address();       // Device address
	
	packet->ask_only = true;
	packet->callback = command_done;
	
	emtr_send(packet);
}

LOCAL uint32 ICACHE_FLASH_ATTR emtr_get_range(emtr_calibration_registers *registers, uint8 shift) {
	return (registers->range >> shift) & 0xFF;
}

LOCAL void ICACHE_FLASH_ATTR emtr_set_range(emtr_calibration_registers *registers, uint8 shift, uint32 range) {
	uint32 old_range = emtr_get_range(registers, shift);
	registers->range = registers->range ^ (old_range << shift);
	registers->range = registers->range | (range << shift);
}

void ICACHE_FLASH_ATTR emtr_calibration_calc(
	emtr_output_registers      *output,
	emtr_calibration_registers *calibration, 
	uint8 range_shift
) {
	uint32 measured;
	uint32 expected;
	uint16 *gain;
	uint32 new_gain;
	
	if (range_shift == 0) {
		measured = output->voltage_rms;
		expected = calibration->calibration_voltage;
		gain = &(calibration->gain_voltage_rms);
	} else if (range_shift == 8) {
		measured = output->current_rms;
		expected = calibration->calibration_current;
		gain = &(calibration->gain_current_rms);
	} else if (range_shift == 16) {
		measured = output->active_power;
		expected = calibration->calibration_active_power;
		gain = &(calibration->gain_active_power);
	} else {
		return;
	}
	
	if (measured == 0) {
		return;
	}
	
	uint32 range = emtr_get_range(calibration, range_shift);
	
calc:
	new_gain = (*gain) * expected / measured;
	
	if (new_gain < 25000) {
		range++;
		if (measured > 6) {
			measured = measured / 2;
			goto calc;
		}
	}
	
	if (new_gain > 55000) {
		range--;
		measured = measured * 2;
		goto calc;
	}
	
	*gain = new_gain;
	emtr_set_range(calibration, range_shift, range);
}

void ICACHE_FLASH_ATTR emtr_calibration_reactive_power(
	emtr_output_registers      *output,
	emtr_calibration_registers *calibration
) {
	calibration->gain_reactive_power = 
		calibration->gain_reactive_power 
		* 
		calibration->calibration_reactive_power
		/ 
		output->reactive_power 
	;
}

void ICACHE_FLASH_ATTR emtr_calibration_line_freqensy(
	emtr_output_registers    *output,
	emtr_frequency_registers *calibration
) {
	calibration->gain_line_frequency = 
		calibration->gain_line_frequency 
		* 
		calibration->line_frequency_ref
		/ 
		output->line_frequency 
	;
}

void ICACHE_FLASH_ATTR emtr_parse_event(emtr_packet *packet, emtr_event_registers *registers) {
#if EMTR_DEBUG
	debug("emtr_parse_event()\n");
#endif
	
	if (packet->len != EMTR_EVENTS_LEN+3) {
		os_memset(registers, 0, sizeof(emtr_event_registers));
		return;
	}
	
	registers->over_current_limit         = emtr_extract_int(packet->data,  0, 4);
	registers->reserved_01                = emtr_extract_int(packet->data,  4, 2);
	registers->over_power_limit           = emtr_extract_int(packet->data,  6, 4);
	registers->reserved_02                = emtr_extract_int(packet->data, 10, 2);
	registers->over_frequency_limit       = emtr_extract_int(packet->data, 12, 2);
	registers->under_frequency_limit      = emtr_extract_int(packet->data, 14, 2);
	registers->over_temperature_limit     = emtr_extract_int(packet->data, 16, 2);
	registers->under_temperature_limit    = emtr_extract_int(packet->data, 18, 2);
	registers->voltage_sag_limit          = emtr_extract_int(packet->data, 20, 2);
	registers->voltage_surge_limit        = emtr_extract_int(packet->data, 22, 2);
	registers->over_current_hold          = emtr_extract_int(packet->data, 24, 2);
	registers->reserved_03                = emtr_extract_int(packet->data, 26, 2);
	registers->over_power_hold            = emtr_extract_int(packet->data, 28, 2);
	registers->reserved_04                = emtr_extract_int(packet->data, 30, 2);
	registers->over_frequency_hold        = emtr_extract_int(packet->data, 32, 2);
	registers->under_frequency_hold       = emtr_extract_int(packet->data, 34, 2);
	registers->over_temperature_hold      = emtr_extract_int(packet->data, 36, 2);
	registers->under_temperature_hold     = emtr_extract_int(packet->data, 38, 2);
	registers->reserved_05                = emtr_extract_int(packet->data, 40, 2);
	registers->reserved_06                = emtr_extract_int(packet->data, 42, 2);
	registers->event_enable               = emtr_extract_int(packet->data, 44, 2);
	registers->event_mask_critical        = emtr_extract_int(packet->data, 46, 2);
	registers->event_mask_standard        = emtr_extract_int(packet->data, 48, 2);
	registers->event_test                 = emtr_extract_int(packet->data, 50, 2);
	registers->event_clear                = emtr_extract_int(packet->data, 52, 2);
}

void ICACHE_FLASH_ATTR emtr_get_event(emtr_callback command_done) {
#if EMTR_DEBUG
	debug("emtr_get_event()\n");
#endif
	emtr_packet *packet = NULL;
	emtr_packet_init(&packet, EMTR_START_FRAME, 8);
	
	packet->data[0] = EMTR_SET_ADDRESS;               // Set address pointer
	packet->data[1] = (EMTR_EVENTS_BASE >> 8) & 0xFF; // address
	packet->data[2] = (EMTR_EVENTS_BASE >> 0) & 0xFF; // address
	
	packet->data[3] = EMTR_READ;                      // Read N bytes
	packet->data[4] = EMTR_EVENTS_LEN;
	
	packet->ask_only = false;
	packet->callback = command_done;
	
	emtr_send(packet);
}

void ICACHE_FLASH_ATTR emtr_set_event(emtr_event_registers *registers, emtr_callback command_done) {
#if EMTR_DEBUG
	debug("emtr_set_event()\n");
#endif
	emtr_packet *packet = NULL;
	emtr_packet_init(&packet, EMTR_START_FRAME, 2+5+EMTR_EVENTS_LEN+2+1);
	
	packet->data[0] = EMTR_SET_ADDRESS;               // Set address pointer
	packet->data[1] = (EMTR_EVENTS_BASE >> 8) & 0xFF; // address
	packet->data[2] = (EMTR_EVENTS_BASE >> 0) & 0xFF; // address
	
	packet->data[3] = EMTR_WRITE;                     // Write N bytes
	packet->data[4] = EMTR_EVENTS_LEN;
	
	emtr_set_int(registers->over_current_limit,         packet->data,  0+5, 4);
	emtr_set_int(registers->reserved_01,                packet->data,  4+5, 2);
	emtr_set_int(registers->over_power_limit,           packet->data,  6+5, 4);
	emtr_set_int(registers->reserved_02,                packet->data, 10+5, 2);
	emtr_set_int(registers->over_frequency_limit,       packet->data, 12+5, 2);
	emtr_set_int(registers->under_frequency_limit,      packet->data, 14+5, 2);
	emtr_set_int(registers->over_temperature_limit,     packet->data, 16+5, 2);
	emtr_set_int(registers->under_temperature_limit,    packet->data, 18+5, 2);
	emtr_set_int(registers->voltage_sag_limit,          packet->data, 20+5, 2);
	emtr_set_int(registers->voltage_surge_limit,        packet->data, 22+5, 2);
	emtr_set_int(registers->over_current_hold,          packet->data, 24+5, 2);
	emtr_set_int(registers->reserved_03,                packet->data, 26+5, 2);
	emtr_set_int(registers->over_power_hold,            packet->data, 28+5, 2);
	emtr_set_int(registers->reserved_04,                packet->data, 30+5, 2);
	emtr_set_int(registers->over_frequency_hold,        packet->data, 32+5, 2);
	emtr_set_int(registers->under_frequency_hold,       packet->data, 34+5, 2);
	emtr_set_int(registers->over_temperature_hold,      packet->data, 36+5, 2);
	emtr_set_int(registers->under_temperature_hold,     packet->data, 38+5, 2);
	emtr_set_int(registers->reserved_05,                packet->data, 40+5, 2);
	emtr_set_int(registers->reserved_06,                packet->data, 42+5, 2);
	emtr_set_int(registers->event_enable,               packet->data, 44+5, 2);
	emtr_set_int(registers->event_mask_critical,        packet->data, 46+5, 2);
	emtr_set_int(registers->event_mask_standard,        packet->data, 48+5, 2);
	emtr_set_int(registers->event_test,                 packet->data, 50+5, 2);
	emtr_set_int(registers->event_clear,                packet->data, 52+5, 2);
	
	packet->data[EMTR_EVENTS_LEN+5] = EMTR_SAVE_REGISTERS;  // Save registers to flash
	packet->data[EMTR_EVENTS_LEN+6] = emtr_address();       // Device address
	
	packet->ask_only = true;
	packet->callback = command_done;
	
	emtr_send(packet);
}

void ICACHE_FLASH_ATTR emtr_parse_frequency(emtr_packet *packet, emtr_frequency_registers *registers) {
#if EMTR_DEBUG
	debug("emtr_parse_frequency()\n");
#endif
	
	if (packet->len != EMTR_FREQUENCY_LEN+3) {
		os_memset(registers, 0, sizeof(emtr_frequency_registers));
		return;
	}
	
	registers->line_frequency_ref  = packet->data[0] * 256 + packet->data[1];
	registers->gain_line_frequency = packet->data[2] * 256 + packet->data[3];
}

void ICACHE_FLASH_ATTR emtr_get_frequency(emtr_callback command_done) {
#if EMTR_DEBUG
	debug("emtr_get_event()\n");
#endif
	emtr_packet *packet = NULL;
	emtr_packet_init(&packet, EMTR_START_FRAME, 11);
	
	packet->data[0] = EMTR_SET_ADDRESS;                      // Set address pointer
	packet->data[1] = (EMTR_FREQUENCY_REF_BASE >> 8) & 0xFF; // address
	packet->data[2] = (EMTR_FREQUENCY_REF_BASE >> 0) & 0xFF; // address
	
	packet->data[3] = EMTR_READ_16;                          // Read register
	
	packet->data[4] = EMTR_SET_ADDRESS;                       // Set address pointer
	packet->data[5] = (EMTR_FREQUENCY_GAIN_BASE >> 8) & 0xFF; // address
	packet->data[6] = (EMTR_FREQUENCY_GAIN_BASE >> 0) & 0xFF; // address
	
	packet->data[7] = EMTR_READ_16;                           // Read register
	
	packet->ask_only = false;
	packet->callback = command_done;
	
	emtr_send(packet);
}

void ICACHE_FLASH_ATTR emtr_set_frequency(emtr_frequency_registers *registers, emtr_callback command_done) {
#if EMTR_DEBUG
	debug("emtr_set_frequency()\n");
#endif
	emtr_packet *packet = NULL;
	emtr_packet_init(&packet, EMTR_START_FRAME, 17);
	
	packet->data[ 0] = EMTR_SET_ADDRESS;                             // Set address pointer
	packet->data[ 1] = (EMTR_FREQUENCY_REF_BASE >> 8) & 0xFF;        // address
	packet->data[ 2] = (EMTR_FREQUENCY_REF_BASE >> 0) & 0xFF;        // address
	
	packet->data[ 3] = EMTR_WRITE_16;                                // Write register
	packet->data[ 4] = (registers->line_frequency_ref >> 8) & 0xFF;  // line_frequency_ref high
	packet->data[ 5] = (registers->line_frequency_ref >> 0) & 0xFF;  // line_frequency_ref low
	
	packet->data[ 6] = EMTR_SET_ADDRESS;                             // Set address pointer
	packet->data[ 7] = (EMTR_FREQUENCY_GAIN_BASE >> 8) & 0xFF;       // address
	packet->data[ 8] = (EMTR_FREQUENCY_GAIN_BASE >> 0) & 0xFF;       // address
	
	packet->data[ 9] = EMTR_WRITE_16;                                // Write register
	packet->data[10] = (registers->gain_line_frequency >> 8) & 0xFF; // gain_line_frequency high
	packet->data[11] = (registers->gain_line_frequency >> 0) & 0xFF; // gain_line_frequency low
	
	packet->data[12] = EMTR_SAVE_REGISTERS;  // Save registers to flash
	packet->data[13] = emtr_address();       // Device address
	
	packet->ask_only = true;
	packet->callback = command_done;
	
	emtr_send(packet);
}

uint16 ICACHE_FLASH_ATTR emtr_address() {
	return emtr_params.address;
}

_uint64_ ICACHE_FLASH_ATTR emtr_counter_active() {
	return emtr_params.counter_active;
}

_uint64_ ICACHE_FLASH_ATTR emtr_counter_apparent() {
	return emtr_params.counter_apparent;
}

void ICACHE_FLASH_ATTR emtr_counter_add(_uint64_ active, _uint64_ apparent) {
	emtr_set_counter(
		emtr_params.counter_active + active, 
		emtr_params.counter_apparent + apparent, 
		NULL
	);
}

uint8 ICACHE_FLASH_ATTR emtr_relay(uint8 index) {
	if (device_get_uart() != UART_EMTR) {
		return 0;
	}
	if (index >= EMTR_RELAYS_COUNT) {
		return 0;
	}
	return emtr_params.relays[index];
}

void ICACHE_FLASH_ATTR emtr_relay_set(uint8 index, uint8 state) {
	if (device_get_uart() != UART_EMTR) {
		return;
	}
	if (index >= EMTR_RELAYS_COUNT) {
		return;
	}
	
	if (emtr_params.relays[index] != state) {
		emtr_params.relays[index] = state;
		emtr_set_relays(NULL);
	}
}

LOCAL void ICACHE_FLASH_ATTR emtr_relays_receive(emtr_packet *packet) {
	if (packet->len != 19) {
		os_memset(emtr_params.relays, 0, EMTR_RELAYS_COUNT);
	} else {
		os_memcpy(emtr_params.relays, packet->data, EMTR_RELAYS_COUNT);
		uint8 i;
		for (i=0; i<EMTR_RELAYS_COUNT; i++) {
			if (emtr_params.relays[i] > 1) {
				emtr_params.relays[i] = 0;
			}
		}
	}
#if DEVICE == PLUG
	user_relay_set(emtr_params.relays[0]);
#endif
#if DEVICE == SWITCH1
	user_switch1_set(0, emtr_params.relays[0]);
#endif
#if DEVICE == SWITCH2
	user_switch2_set(0, emtr_params.relays[0]);
	user_switch2_set(1, emtr_params.relays[1]);
#endif
}

LOCAL void ICACHE_FLASH_ATTR emtr_counter_receive(emtr_packet *packet) {
	if (packet->len != 19) {
		emtr_params.counter_active = 0;
		emtr_params.counter_apparent = 0;
	} else {
		emtr_params.counter_active   = emtr_extract_int(packet->data, 0, 8);
		emtr_params.counter_apparent = emtr_extract_int(packet->data, 8, 8);
	}
	emtr_get_relays(emtr_relays_receive);
}

LOCAL void ICACHE_FLASH_ATTR emtr_address_receive(emtr_packet *packet) {
	emtr_params.address = packet->data[0] * 256 + packet->data[1];
	emtr_get_counter(emtr_counter_receive);
}

void ICACHE_FLASH_ATTR emtr_get_address(emtr_callback command_done) {
#if EMTR_DEBUG
	debug("emtr_get_address()\n");
#endif
	emtr_packet *packet = NULL;
	emtr_packet_init(&packet, EMTR_START_FRAME, 7);
	
	packet->data[0] = EMTR_SET_ADDRESS; // Set address pointer
	packet->data[1] = 0x00;             // address
	packet->data[2] = 0x26;             // address
	packet->data[3] = EMTR_READ_16;     // Read 2 bytes
	
	packet->ask_only = false;
	packet->callback = command_done;
	
	emtr_send(packet);
}

void ICACHE_FLASH_ATTR emtr_get_counter(emtr_callback command_done) {
#if EMTR_DEBUG
	debug("emtr_get_counter()\n");
#endif
	emtr_packet *packet = NULL;
	emtr_packet_init(&packet, EMTR_START_FRAME, 5);
	
	packet->data[0] = EMTR_FLASH_READ;  // Command
	packet->data[1] = 0x00;             // page
	
	packet->ask_only = false;
	packet->callback = command_done;
	
	emtr_send(packet);
}

void ICACHE_FLASH_ATTR emtr_set_counter(_uint64_ active, _uint64_ apparent, emtr_callback command_done) {
#if EMTR_DEBUG
	debug("emtr_set_counter()\n");
#endif
	emtr_params.counter_active = active;
	emtr_params.counter_apparent = apparent;
	
	emtr_packet *packet = NULL;
	emtr_packet_init(&packet, EMTR_START_FRAME, 21);
	
	packet->data[0] = EMTR_FLASH_WRITE;  // Command
	packet->data[1] = 0x00;              // page
	
	emtr_set_int(emtr_params.counter_active,   packet->data,  2, 8);
	emtr_set_int(emtr_params.counter_apparent, packet->data, 10, 8);
	
	packet->ask_only = true;
	packet->callback = command_done;
	
	emtr_send(packet);
}

void ICACHE_FLASH_ATTR emtr_get_relays(emtr_callback command_done) {
#if EMTR_DEBUG
	debug("emtr_get_relays()\n");
#endif
	emtr_packet *packet = NULL;
	emtr_packet_init(&packet, EMTR_START_FRAME, 5);
	
	packet->data[0] = EMTR_FLASH_READ;  // Command
	packet->data[1] = 0x01;             // page
	
	packet->ask_only = false;
	packet->callback = command_done;
	
	emtr_send(packet);
}

void ICACHE_FLASH_ATTR emtr_set_relays(emtr_callback command_done) {
#if EMTR_DEBUG
	debug("emtr_set_relays()\n");
#endif
	emtr_packet *packet = NULL;
	emtr_packet_init(&packet, EMTR_START_FRAME, 21);
	
	packet->data[0] = EMTR_FLASH_WRITE;  // Command
	packet->data[1] = 0x01;              // page
	
	os_memcpy(packet->data + 2, emtr_params.relays, EMTR_RELAYS_COUNT);
	
	packet->ask_only = true;
	packet->callback = command_done;
	
	emtr_send(packet);
}

void ICACHE_FLASH_ATTR emtr_clear_event(uint16 event, emtr_callback command_done) {
#if EMTR_DEBUG
	debug("emtr_clear_event()\n");
#endif
	emtr_packet *packet = NULL;
	emtr_packet_init(&packet, EMTR_START_FRAME, 9);
	
	packet->data[0] = EMTR_SET_ADDRESS;    // Set address pointer
	packet->data[1] = 0x00;                // address
	packet->data[2] = 0x92;                // address
	packet->data[3] = EMTR_WRITE_16;       // Write 2 bytes
	packet->data[4] = (event >> 8) & 0xFF; // event high
	packet->data[5] = (event >> 0) & 0xFF; // event low
	
	packet->ask_only = true;
	packet->callback = command_done;
	
	emtr_send(packet);
}

void ICACHE_FLASH_ATTR emtr_set_system_configuration(uint32 system_configuration, uint16 interval, emtr_callback command_done) {
#if EMTR_DEBUG
	debug("emtr_set_system_configuration()\n");
#endif
	emtr_packet *packet = NULL;
	emtr_packet_init(&packet, EMTR_START_FRAME, 17);
	
	packet->data[ 0] = EMTR_SET_ADDRESS;    // Set address pointer
	packet->data[ 1] = 0x00;                // address
	packet->data[ 2] = 0x42;                // address
	packet->data[ 3] = EMTR_WRITE_32;       // Write 4 bytes
	packet->data[ 4] = (system_configuration >> 24) & 0xFF; // system_configuration
	packet->data[ 5] = (system_configuration >> 16) & 0xFF; // system_configuration
	packet->data[ 6] = (system_configuration >>  8) & 0xFF; // system_configuration
	packet->data[ 7] = (system_configuration >>  0) & 0xFF; // system_configuration
	
	packet->data[ 8] = EMTR_SET_ADDRESS;    // Set address pointer
	packet->data[ 9] = 0x00;                // address
	packet->data[10] = 0x5A;                // address
	packet->data[11] = EMTR_WRITE_16;       // Write 2 bytes
	packet->data[12] = (interval >>  8) & 0xFF; // interval
	packet->data[13] = (interval >>  0) & 0xFF; // interval
	
	packet->ask_only = true;
	packet->callback = command_done;
	
	emtr_send(packet);
}

void ICACHE_FLASH_ATTR emtr_single_wire_start(uint32 system_configuration, emtr_callback command_done) {
#if EMTR_DEBUG
	debug("emtr_single_wire_start()\n");
#endif
	
	emtr_single_wire_done = command_done;
	if ((system_configuration & (1 << 8)) != 0) {
		return;
	}
	
	system_configuration = system_configuration | (1 << 8);
	emtr_set_system_configuration(system_configuration, 6, NULL);
}

void ICACHE_FLASH_ATTR emtr_single_wire_stop(uint32 system_configuration) {
#if EMTR_DEBUG
	debug("emtr_single_wire_stop()\n");
#endif
	
	emtr_single_wire_done = NULL;
	if ((system_configuration & (1 << 8)) == 0) {
		return;
	}
	
	system_configuration = system_configuration & (~(1 << 8));
	emtr_set_system_configuration(system_configuration, 2, NULL);
}

void ICACHE_FLASH_ATTR emtr_init() {
	if (device_get_uart() != UART_NONE) {
		return;
	}
	
	// stdout_disable();
	uart_char_in_set(emtr_char_in);
	uart_init(UART0, BIT_RATE_4800, EIGHT_BITS, NONE_BITS, ONE_STOP_BIT);
	
	// Get address
	setTimeout((os_timer_func_t *)emtr_get_address, emtr_address_receive, 100);
}

void ICACHE_FLASH_ATTR emtr_down() {
	if (device_get_uart() != UART_EMTR) {
		return;
	}
	// Start
	uart_write_byte(EMTR_START_FRAME);
	
	// Len
	uart_write_byte(10);
}
#endif
