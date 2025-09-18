/* SPA Null Device Plugin */
/* SPDX-FileCopyrightText: Copyright © 2024 */
/* SPDX-License-Identifier: MIT */

/**
 * @file null.c
 * @brief SPA Null Plugin - Educational example demonstrating SPA plugin architecture
 *
 * This file implements a minimal SPA plugin that creates "null" audio devices.
 * A null device accepts audio buffers but discards them instead of playing them,
 * making it useful for testing, benchmarking, and understanding SPA fundamentals.
 *
 * SPA PLUGIN ARCHITECTURE OVERVIEW:
 * ================================
 *
 * 1. Plugin Entry Point:
 *    - spa_handle_factory_enum() - Called by PipeWire to discover available factories
 *    - Each factory creates different types of SPA handles (nodes, devices, monitors)
 *
 * 2. Factory Pattern:
 *    - Each factory implements spa_handle_factory interface
 *    - Factories create spa_handle objects that implement specific interfaces
 *    - Common interfaces: spa_node, spa_device, spa_monitor
 *
 * 3. Interface Implementation:
 *    - SPA uses object-oriented C with vtables (function pointers)
 *    - Objects implement multiple interfaces through spa_interface_info
 *    - Type-safe casting using spa_interface_call macros
 *
 * 4. Event-Driven Architecture:
 *    - Objects communicate through callbacks and events
 *    - spa_hook system for registering event listeners
 *    - Asynchronous operation with spa_result for delayed responses
 *
 * The null plugin provides a simple example of:
 * - Factory enumeration and registration
 * - Node interface implementation
 * - Buffer processing (drop instead of play)
 * - Format negotiation
 * - State management
 */



#include <errno.h>

#include <spa/support/plugin.h>
#include <spa/support/log.h>

#include "null.h"

/* External factory declarations for null components */
extern const struct spa_handle_factory spa_null_sink_factory;

/**
 * @brief Null plugin log topic definition
 *
 * All null plugin components use this logging topic with prefix "spa.null".
 * This allows users to control log verbosity specifically for null devices:
 *
 * export PIPEWIRE_DEBUG="*spa.null*:4"  # Enable debug logs for null plugin
 */
SPA_LOG_TOPIC_DEFINE(null_log_topic, "spa.null");

/**
 * @brief Register null log topic in SPA logging system
 *
 * This macro registers the log topic so it can be dynamically controlled
 * through PipeWire's logging infrastructure.
 */
SPA_LOG_TOPIC_ENUM_DEFINE_REGISTERED;

/**
 * @brief Enumerate available SPA handle factories for null plugin
 *
 * This is the main entry point that PipeWire calls to discover plugin capabilities.
 * It follows the SPA factory enumeration pattern used by all SPA plugins.
 *
 * SPA FACTORY ENUMERATION SPECIFICATION:
 * =====================================
 *
 * 1. Function Signature:
 *    int spa_handle_factory_enum(const struct spa_handle_factory **factory, uint32_t *index)
 *
 * 2. Parameters:
 *    - factory: Output parameter - pointer to store factory interface
 *    - index: Input/Output - current enumeration index, incremented on success
 *
 * 3. Return Values:
 *    - 1: Factory returned successfully, more factories may be available
 *    - 0: No more factories available (end of enumeration)
 *    - <0: Error code (e.g., -EINVAL for invalid parameters)
 *
 * 4. Enumeration Protocol:
 *    - PipeWire calls with index starting at 0
 *    - Plugin returns factories in order, incrementing index
 *    - When no more factories exist, return 0 to end enumeration
 *    - PipeWire registers all returned factories with the core
 *
 * 5. Factory Responsibilities:
 *    - Each factory must have unique name (spa_handle_factory.name)
 *    - Factory creates spa_handle objects implementing specific interfaces
 *    - Factory defines supported properties and interface types
 *
 * Example enumeration sequence:
 * @code
 * // PipeWire core discovery:
 * uint32_t index = 0;
 * const struct spa_handle_factory *factory;
 *
 * // Call 0: index=0 -> returns spa_null_sink_factory, index becomes 1
 * spa_handle_factory_enum(&factory, &index);  // returns 1
 *
 * // Call 1: index=1 -> no more factories, returns 0
 * spa_handle_factory_enum(&factory, &index);  // returns 0, enumeration ends
 * @endcode
 *
 * @param factory Pointer to store the factory interface pointer
 * @param index   Pointer to factory index (input/output parameter)
 *                On input: index of factory to retrieve (0, 1, 2, ...)
 *                On output: incremented to next index
 *
 * @return 1 if factory was returned successfully
 * @return 0 if no more factories available (index beyond range)
 * @return -EINVAL if factory or index pointers are NULL
 *
 * @note This function is the plugin's main entry point and must be exported
 * @note Function increments *index before returning for PipeWire convenience
 * @note Plugin should validate parameters before processing
 */
SPA_EXPORT
int spa_handle_factory_enum(const struct spa_handle_factory **factory, uint32_t *index)
{
	/*
	 * SPA PARAMETER VALIDATION PATTERN:
	 * ================================
	 * All SPA functions should validate input parameters using spa_return_val_if_fail.
	 * This macro provides consistent error handling and debug logging.
	 */
	spa_return_val_if_fail(factory != NULL, -EINVAL);
	spa_return_val_if_fail(index != NULL, -EINVAL);

	/*
	 * FACTORY ENUMERATION IMPLEMENTATION:
	 * ==================================
	 * Switch on index to return appropriate factory. The order matters as it
	 * determines the registration order in PipeWire core.
	 *
	 * For this educational null plugin, we only provide one factory:
	 * - Index 0: spa_null_sink_factory (creates null audio sink nodes)
	 *
	 * More complex plugins would have multiple factories:
	 * - Index 0: Source factory (input nodes)
	 * - Index 1: Sink factory (output nodes)
	 * - Index 2: Device factory (device management)
	 * - Index 3: Monitor factory (device discovery)
	 */
	switch (*index) {
	case 0:
		/*
		 * NULL SINK FACTORY:
		 * ==================
		 * Creates spa_node objects that implement audio sink interface.
		 * The null sink accepts audio buffers but discards them instead
		 * of sending to hardware, making it useful for:
		 * - Testing audio pipelines without hardware
		 * - Measuring processing performance
		 * - Debugging audio routing issues
		 * - Silent audio consumption
		 */
		*factory = &spa_null_sink_factory;
		break;
	default:
		/*
		 * END OF ENUMERATION:
		 * ==================
		 * When index exceeds available factories, return 0 to indicate
		 * end of enumeration. PipeWire will stop calling this function.
		 */
		return 0;
	}

	/*
	 * INCREMENT INDEX AND RETURN SUCCESS:
	 * ===================================
	 * Increment index for next call and return 1 to indicate success.
	 * This pattern allows PipeWire to call the function in a simple loop:
	 *
	 * while (spa_handle_factory_enum(&factory, &index)) {
	 *     register_factory(factory);
	 * }
	 */
	(*index)++;
	return 1;
}