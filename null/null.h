/* SPA Null Device Plugin Header */
/* SPDX-FileCopyrightText: Copyright © 2024 */
/* SPDX-License-Identifier: MIT */

/**
 * @file null.h
 * @brief SPA Null Plugin - Core data structures and interface definitions
 *
 * This header defines the fundamental data structures and interfaces used by
 * the null plugin. It serves as an educational example of SPA plugin architecture
 * and demonstrates key patterns used throughout the PipeWire ecosystem.
 *
 * SPA INTERFACE ARCHITECTURE:
 * ==========================
 *
 * SPA uses object-oriented programming in C through several key patterns:
 *
 * 1. Interface System:
 *    - spa_interface: Base interface with type and version info
 *    - spa_callbacks: Function pointer tables (vtables)
 *    - Interface implementations cast to specific types
 *
 * 2. Object Lifecycle:
 *    - Factory creates spa_handle objects
 *    - Handle provides interfaces through spa_handle_get_interface()
 *    - Objects implement spa_node, spa_device, etc.
 *
 * 3. Event System:
 *    - spa_hook: Callback registration mechanism
 *    - Events sent through registered callbacks
 *    - Type-safe callback invocation
 *
 * 4. Property System:
 *    - spa_dict: Key-value property collections
 *    - spa_param: Structured parameter objects
 *    - Format negotiation through parameters
 */

#ifndef SPA_NULL_H
#define SPA_NULL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* SPA Core Headers */
#include <spa/utils/defs.h>
#include <spa/utils/dict.h>
#include <spa/utils/hook.h>
#include <spa/utils/result.h>

/* SPA Node Interface */
#include <spa/node/node.h>
#include <spa/node/utils.h>
#include <spa/node/io.h>

/* SPA Parameter System */
#include <spa/param/param.h>
#include <spa/param/audio/format.h>
#include <spa/param/audio/raw.h>

/* SPA Support Interfaces */
#include <spa/support/log.h>
#include <spa/support/loop.h>
#include <spa/support/system.h>

/* SPA Plugin Interface */
#include <spa/support/plugin.h>

/*
 * PLUGIN CONSTANTS AND IDENTIFIERS:
 * =================================
 * These constants define the plugin's identity within the SPA ecosystem.
 * They follow SPA naming conventions for consistency.
 */

/** Maximum number of audio ports supported by null sink */
#define MAX_PORTS        1

/** Maximum number of buffers in the processing queue */
#define MAX_BUFFERS      16

/** Default buffer size in frames (samples per channel) */
#define DEFAULT_FRAMES   1024

/** Plugin name for null sink factory */
#define SPA_NAME_API_NULL_SINK    "api.null.sink"

/** Plugin library name */
#define SPA_NAME_LIB_NULL         "null"

/*
 * LOGGING SUPPORT:
 * ===============
 * SPA provides structured logging with topics for fine-grained control.
 * Each plugin should define its own log topic.
 */

/** External log topic declaration for null plugin */
extern struct spa_log_topic null_log_topic;

/** Convenience macro for logging with null plugin topic */
#define spa_log_topic_default &null_log_topic

/*
 * NULL SINK STATE STRUCTURE:
 * ==========================
 * This structure maintains the complete state of a null sink node instance.
 * It demonstrates the typical patterns used in SPA node implementations.
 */

/**
 * @brief Null sink node state and configuration
 *
 * This structure contains all state information for a null sink node instance.
 * It follows SPA patterns for node implementation and demonstrates:
 * - Interface embedding (spa_node interface)
 * - Event callback management (spa_hook_list)
 * - Format negotiation state
 * - Buffer management
 * - Timing and synchronization
 *
 * SPA OBJECT EMBEDDING PATTERN:
 * =============================
 * SPA objects embed interfaces directly in their state structures:
 * - The spa_node interface is embedded at the beginning
 * - This allows casting between struct null_state* and spa_node*
 * - Type safety is maintained through interface versioning
 */
struct null_state {
	/*
	 * EMBEDDED SPA NODE INTERFACE:
	 * ============================
	 * The spa_node interface must be the first member to enable
	 * safe casting between null_state* and spa_node*.
	 */
	struct spa_node node;

	/*
	 * SPA SUPPORT INTERFACES:
	 * ======================
	 * These interfaces provide access to core PipeWire services:
	 * - log: Structured logging with topic support
	 * - system: System services (time, scheduling, etc.)
	 * - loop: Event loop for asynchronous operations
	 */
	struct spa_log *log;           /**< Logging interface */
	struct spa_system *system;    /**< System interface for timing */
	struct spa_loop *data_loop;   /**< Data processing event loop */

	/*
	 * EVENT CALLBACK MANAGEMENT:
	 * ==========================
	 * SPA uses hook lists to manage event callbacks from multiple listeners.
	 * Nodes emit events to notify interested parties of state changes.
	 */
	struct spa_hook_list hooks;   /**< List of registered event listeners */

	/*
	 * NODE CONFIGURATION AND STATE:
	 * =============================
	 * These fields maintain the current configuration and operational state
	 * of the null sink node.
	 */
	uint64_t info_all;            /**< Bitmask of available info fields */
	struct spa_node_info info;   /**< Node information structure */
	struct spa_param_info params[8]; /**< Supported parameter types */

	/*
	 * AUDIO FORMAT CONFIGURATION:
	 * ===========================
	 * Audio format negotiation is a key part of SPA node operation.
	 * These fields track the current format configuration.
	 */
	bool have_format;             /**< True if format has been configured */
	struct spa_audio_info current_format; /**< Current audio format */

	/*
	 * PORT MANAGEMENT:
	 * ===============
	 * SPA nodes communicate through ports. This null sink has one input port
	 * that accepts audio buffers and discards them.
	 */
	uint64_t port_info_all;       /**< Port info availability mask */
	struct spa_port_info port_info; /**< Input port information */
	struct spa_param_info port_params[8]; /**< Port parameter types */

	/*
	 * BUFFER PROCESSING STATE:
	 * =======================
	 * These fields manage the buffer processing pipeline:
	 * - I/O areas for communication with graph
	 * - Buffer queue management
	 * - Processing state tracking
	 */
	struct spa_io_buffers *io;    /**< Buffer I/O area from graph */
	struct spa_io_rate_match *rate_match; /**< Rate matching info */

	/*
	 * TIMING AND SYNCHRONIZATION:
	 * ===========================
	 * Audio processing requires precise timing. These fields track
	 * timing information and synchronization state.
	 */
	uint64_t quantum_limit;       /**< Maximum processing quantum */
	struct spa_fraction rate;     /**< Sample rate as fraction */

	/*
	 * PROCESSING STATISTICS:
	 * =====================
	 * For educational and debugging purposes, track processing statistics.
	 */
	uint64_t frame_count;         /**< Total frames processed (dropped) */
	uint64_t buffer_count;        /**< Total buffers processed */

	/*
	 * NODE STATE FLAGS:
	 * ================
	 * Various boolean flags tracking node operational state.
	 */
	unsigned int started:1;       /**< True if node is started */
	unsigned int following:1;     /**< True if following another node */
};

/*
 * FUNCTION PROTOTYPES:
 * ===================
 * These functions implement the core null plugin functionality.
 */

/**
 * @brief Initialize null sink state structure
 *
 * This function initializes a newly allocated null_state structure with
 * default values and prepares it for operation. It demonstrates the
 * initialization patterns used in SPA node implementations.
 *
 * INITIALIZATION RESPONSIBILITIES:
 * ===============================
 * 1. Embed spa_node interface with proper callbacks
 * 2. Initialize hook list for event management
 * 3. Set up default node and port information
 * 4. Configure supported parameter types
 * 5. Initialize format negotiation state
 * 6. Set up timing and synchronization defaults
 *
 * @param state Pointer to null_state structure to initialize
 * @param log   Logging interface from SPA support
 * @param system System interface from SPA support
 * @param loop   Data processing loop from SPA support
 *
 * @return 0 on success, negative error code on failure
 *
 * @note This function should be called immediately after allocating
 *       a new null_state structure
 * @note All SPA support interfaces must be valid (non-NULL)
 */
int null_state_init(struct null_state *state,
                   struct spa_log *log,
                   struct spa_system *system,
                   struct spa_loop *loop);

/**
 * @brief Clean up null sink state structure
 *
 * This function performs cleanup of a null_state structure before
 * deallocation. It demonstrates proper resource management in SPA nodes.
 *
 * CLEANUP RESPONSIBILITIES:
 * ========================
 * 1. Remove all registered event hooks
 * 2. Stop any ongoing processing
 * 3. Free allocated parameter objects
 * 4. Reset all state to safe defaults
 *
 * @param state Pointer to null_state structure to clean up
 *
 * @note This function should be called before freeing the state structure
 * @note It's safe to call this function multiple times
 * @note After cleanup, the state structure should not be used
 */
void null_state_cleanup(struct null_state *state);

/*
 * SPA INTERFACE CONVERSION MACROS:
 * ===============================
 * These macros provide type-safe conversion between SPA interfaces
 * and implementation structures. They follow SPA conventions.
 */

/**
 * @brief Convert spa_node interface to null_state structure
 *
 * This macro safely converts a spa_node pointer to the containing
 * null_state structure. It uses spa_container_of for type safety.
 *
 * @param node Pointer to spa_node interface
 * @return Pointer to containing null_state structure
 */
#define null_state_from_node(node) \
	spa_container_of((node), struct null_state, node)

/*
 * PLUGIN FACTORY DECLARATIONS:
 * ============================
 * External declarations for plugin factories. These are implemented
 * in separate source files and registered through the main plugin
 * enumeration function.
 */

/** Null sink factory - creates null audio sink nodes */
extern const struct spa_handle_factory spa_null_sink_factory;

#ifdef __cplusplus
}
#endif

#endif /* SPA_NULL_H */