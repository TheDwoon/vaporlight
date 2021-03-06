#include "config.h"

#include "console.h"
#include "debug.h"
#include "flash.h"

#define REPEAT(value,length) { [0 ... (length - 1)] = value }

/*
 * Configuration values in RAM for access by other modules.
 */

config_entry_t config = {
	.my_address = 0x00fd,
	.heat_limit = REPEAT(0xffff, HEAT_SENSOR_LEN),
	.led_infos = {
		{
			// Example values for sRGB
			.color_matrix = {
				FIXINIT(2.409638554216868),  FIXINIT(-0.6693440428380186), FIXINIT(-0.321285140562249),
				FIXINIT(-1.204819277108434), FIXINIT(2.186523873270861),   FIXINIT(0.0495314591700134),
				FIXINIT(-1.204819277108434), FIXINIT(-1.517179830432843),  FIXINIT(1.271753681392236)
			},
			.peak_Y = {
				FIXINIT(1000),
				FIXINIT(1000),
				FIXINIT(1000)
			},
			.channels = {
				0,
				1,
				2
			}
		},
		{
			// Example values for sRGB
			.color_matrix = {
				FIXINIT(2.409638554216868),  FIXINIT(-0.6693440428380186), FIXINIT(-0.321285140562249),
				FIXINIT(-1.204819277108434), FIXINIT(2.186523873270861),   FIXINIT(0.0495314591700134),
				FIXINIT(-1.204819277108434), FIXINIT(-1.517179830432843),  FIXINIT(1.271753681392236)
			},
			.peak_Y = {
				FIXINIT(1000),
				FIXINIT(1000),
				FIXINIT(1000)
			},
			.channels = {
				3,
				4,
				5
			}
		},
		{
			// Example values for sRGB
			.color_matrix = {
				FIXINIT(2.409638554216868),  FIXINIT(-0.6693440428380186), FIXINIT(-0.321285140562249),
				FIXINIT(-1.204819277108434), FIXINIT(2.186523873270861),   FIXINIT(0.0495314591700134),
				FIXINIT(-1.204819277108434), FIXINIT(-1.517179830432843),  FIXINIT(1.271753681392236)
			},
			.peak_Y = {
				FIXINIT(1000),
				FIXINIT(1000),
				FIXINIT(1000)
			},
			.channels = {
				6,
				7,
				8
			}
		},
		{
			// Example values for sRGB
			.color_matrix = {
				FIXINIT(2.409638554216868),  FIXINIT(-0.6693440428380186), FIXINIT(-0.321285140562249),
				FIXINIT(-1.204819277108434), FIXINIT(2.186523873270861),   FIXINIT(0.0495314591700134),
				FIXINIT(-1.204819277108434), FIXINIT(-1.517179830432843),  FIXINIT(1.271753681392236)
			},
			.peak_Y = {
				FIXINIT(1000),
				FIXINIT(1000),
				FIXINIT(1000)
			},
			.channels = {
				9,
				10,
				11
			}
		},
		{
			// Example values for sRGB
			.color_matrix = {
				FIXINIT(2.409638554216868),  FIXINIT(-0.6693440428380186), FIXINIT(-0.321285140562249),
				FIXINIT(-1.204819277108434), FIXINIT(2.186523873270861),   FIXINIT(0.0495314591700134),
				FIXINIT(-1.204819277108434), FIXINIT(-1.517179830432843),  FIXINIT(1.271753681392236)
			},
			.peak_Y = {
				FIXINIT(1000),
				FIXINIT(1000),
				FIXINIT(1000)
			},
			.channels = {
				12,
				13,
				14
			}
		}
	},
};

/*
 * The configuration page. This is laid out in the following way:
 * There is a number of configuration slots, used one after the other
 * for storing a configuration. The state of each slot is kept in
 * entry_status, where 0xffff (the flash default value) stands for a free
 * slot, 0x5555 for the slot currently in use and 0x0000 for an old slot.
 * When all slots are used, the config page is erased and the process
 * starts again.
 *
 * The entry count is derived in the following way:
 * Page size: 1024B
 * Entry size with status word: sizeof(config_entry_t) + sizeof(uint16_t)
 * Entry count = Page size / Entry size
 */
#define ENTRY_COUNT (FLASH_PAGE_SIZE * CONFIG_PAGES /             \
		     (sizeof(config_entry_t) + sizeof(uint16_t)))
typedef struct {
	uint16_t entry_status[ENTRY_COUNT];

	config_entry_t entries[ENTRY_COUNT];
} __attribute__ ((packed)) config_page_t;

config_page_t config_page __attribute__ ((section (".config"))) = {
	.entry_status = REPEAT(0xffff, ENTRY_COUNT),
	.entries = {
		[0 ... ENTRY_COUNT - 1] = {
			.my_address = 0xffff,
			.heat_limit = REPEAT(0xffff, HEAT_SENSOR_LEN),
			.led_infos = {
				[0 ... RGB_LED_COUNT - 1] = {
					.filler1 = REPEAT(0xff, 9 * sizeof(fixed_t)),
					.filler2 = REPEAT(0xff, 3 * sizeof(fixed_t)),
					.channels = REPEAT(0xff, 3)
				}
			},
			.backup_channel = 0xff
		}
	}
};

/*
 * Loads the configuration stored in flash. If no configuration is found,
 * an E_NOCONFIG is returned.
 *
 * Returns an error/success code.
 */
error_t load_config() {
	// Look for an entry currently in use.
	unsigned in_use = ENTRY_COUNT;
	for (unsigned int entry = 0; entry < ENTRY_COUNT; entry++) {
		if (config_page.entry_status[entry] == CONFIG_ENTRY_IN_USE) {
			in_use = entry;
			break;
		}
	}

#ifdef TRACE_FLASH
	debug_putchar((unsigned char) in_use);
#endif

	if (in_use == ENTRY_COUNT) {
		// No entry in use has been found.
		return E_NOCONFIG;
	}

	config = config_page.entries[in_use];

	return E_SUCCESS;
}

/*
 * Saves the configuration to flash.
 */
error_t save_config() {
#ifdef TRACE_FLASH
	debug_string("save");
#endif

	if (!config_valid()) {
		error(ER_BUG, STR_WITH_LEN("Trying to save invalid config"), EA_PANIC);
	}

	error_t error;

	// Look for the entry last in use and an entry not yet used.
	// Assuming there is only one entry in use.
	unsigned last_in_use = ENTRY_COUNT;
	unsigned unused = ENTRY_COUNT;

	for (unsigned entry = 0; entry < ENTRY_COUNT; entry++) {
		if (config_page.entry_status[entry] == CONFIG_ENTRY_IN_USE) {
			last_in_use = entry;
		}

		if (config_page.entry_status[entry] == CONFIG_ENTRY_EMPTY &&
		    unused == ENTRY_COUNT) {
			unused = entry;
		}
	}

#ifdef TRACE_FLASH
	debug_putchar((unsigned char) last_in_use);
	debug_putchar((unsigned char) unused);
#endif

	flash_unlock();

	// If no entries are free, erase config page and try again
	if (unused == ENTRY_COUNT) {
#ifdef TRACE_FLASH
		debug_string("erase");
#endif
		error = flash_erase_page(&config_page);
		if (error != E_SUCCESS) goto out;

		return save_config();
	}

	// Save the new configuration.
	_Static_assert(sizeof(config_entry_t) % sizeof(uint16_t) == 0,
		       "config_entry_t must be repadded!");

	error = flash_copy(&config_page.entries[unused], &config,
			   sizeof(config_entry_t) / sizeof(uint16_t));
	if (error != E_SUCCESS) goto out;

#ifdef TRACE_FLASH
	debug_string("copy done");
#endif

	// The configuration was written successfully. Now update the status words.
	error = flash_write_check(config_page.entry_status + last_in_use, CONFIG_ENTRY_OLD);
	if (error != E_SUCCESS) goto out;

	error = flash_write_check(config_page.entry_status + unused, CONFIG_ENTRY_IN_USE);
	if (error != E_SUCCESS) goto out;

#ifdef TRACE_FLASH
	debug_string("status updated");
#endif

out:
	flash_lock();
	return error;
}

/*
 * Strings used in config_valid.
 */
#define CRLF "\r\n"

static const char *ADDRESS_IS_INVALID =
	"The board's address is invalid." CRLF;

static const char *ADDRESS_IS_BROADCAST =
	"Warning: The board's address is the broadcast address." CRLF;

static const char *CHANNEL_ASSIGNMENT_IS_INVALID =
	"The PWM channel assignment of the LEDs is invalid at channel ";

/*
 * Checks if the configuration in config is valid.  Returns 1 if the
 * configuration is valid, 0 otherwise.  This function may print an
 * explanatory message to the debug USART if the configuration is
 * found to be invalid.
 */
int config_valid() {
	int valid = 1;

	// Check if the module has been given a valid address.
	// Warn if the address is the broadcast address.
	if (config.my_address > 0xfd) {
		console_write(ADDRESS_IS_INVALID);
		valid = 0;
	}
	if (config.my_address == 0xfd) {
		console_write(ADDRESS_IS_BROADCAST);
	}

	// Check that all the PWM channels assigned to LEDs are
	// distinct
	int led_seen[MODULE_LENGTH] = {}; // 0 initialization
	uint8_t backup_channel = MODULE_LENGTH;

	for (int l = 0; l < RGB_LED_COUNT; l++) {
		for (int c = 0; c < 3; c++) {
			led_seen[config.led_infos[l].channels[c]]++;
		}
	}

	for (unsigned i = 0; i < MODULE_LENGTH; i++) {
		if (led_seen[i] != 1) {
			if (backup_channel == MODULE_LENGTH) {
				backup_channel = i;
			} else {
				console_write(CHANNEL_ASSIGNMENT_IS_INVALID);
				console_uint_d(i);
				console_write(CRLF);
				valid = 0;
			}
		}
	}

	config.backup_channel = backup_channel;

	return valid;
}
