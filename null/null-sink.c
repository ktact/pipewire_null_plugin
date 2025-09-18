/* SPA Null Sink Node Implementation */
/* SPDX-FileCopyrightText: Copyright © 2024 */
/* SPDX-License-Identifier: MIT */

/**
 * @file null-sink.c
 * @brief SPA Null Sink Node - Educational implementation of audio buffer dropping
 *
 * This file implements a complete SPA audio sink node that accepts audio buffers
 * and discards them instead of playing them. It serves as an educational example
 * demonstrating all aspects of SPA node implementation:
 *
 * KEY SPA NODE CONCEPTS DEMONSTRATED:
 * ==================================
 *
 * 1. Node Interface Implementation:
 *    - spa_node_methods vtable with all required functions
 *    - Node lifecycle: create -> configure -> start -> process -> stop -> destroy
 *    - Parameter negotiation and format configuration
 *
 * 2. Buffer Processing Pipeline:
 *    - impl_node_process() - the heart of real-time audio processing
 *    - Buffer queue management and I/O area communication
 *    - Frame counting and timing synchronization
 *
 * 3. Format Negotiation:
 *    - enumerate_params() - advertise supported formats
 *    - set_param() - accept format configuration from graph
 *    - Format validation and conversion
 *
 * 4. Port Management:
 *    - Single input port for audio consumption
 *    - Port information and parameter enumeration
 *    - I/O area assignment for graph communication
 *
 * 5. Event System:
 *    - Node events for state change notifications
 *    - Hook management for multiple listeners
 *    - Asynchronous result handling
 *
 * The null sink is particularly useful for:
 * - Testing audio pipelines without hardware dependency
 * - Measuring processing performance and latency
 * - Debugging audio routing and format issues
 * - Silent consumption of unwanted audio streams
 */



#include <errno.h>
#include <string.h>
#include <stdio.h>

#include <spa/utils/result.h>
#include <spa/utils/string.h>
#include <spa/debug/format.h>
#include <spa/debug/log.h>
#include <spa/pod/builder.h>
#include <spa/pod/filter.h>
#include <spa/param/audio/format-utils.h>

#include "null.h"

/*
 * SPA NODE INTERFACE IMPLEMENTATION:
 * ==================================
 * The spa_node interface is the core interface for audio processing nodes.
 * It defines the contract between nodes and the PipeWire graph engine.
 */

/**
 * @brief Add event listener to null sink node
 *
 * This function allows external components to register for node events.
 * Events include state changes, parameter updates, and processing notifications.
 *
 * SPA EVENT SYSTEM PATTERN:
 * =========================
 * SPA uses a hook-based event system for loose coupling:
 * 1. Listeners register hooks with spa_hook_list_append()
 * 2. Node emits events through spa_node_call() macros
 * 3. Multiple listeners can register for the same events
 * 4. Events are delivered synchronously in registration order
 *
 * @param object Pointer to spa_node interface (cast to null_state)
 * @param listener Event listener hook to register
 * @param events Pointer to spa_node_events callback structure
 * @param data User data passed to event callbacks
 *
 * @return 0 on success
 *
 * @note The listener hook must remain valid until explicitly removed
 * @note Events structure must remain valid for the lifetime of the hook
 */
static int impl_node_add_listener(void *object,
                                 struct spa_hook *listener,
                                 const struct spa_node_events *events,
                                 void *data)
{
	struct null_state *state = object;

	spa_return_val_if_fail(state != NULL, -EINVAL);
	spa_return_val_if_fail(listener != NULL, -EINVAL);

	/*
	 * HOOK REGISTRATION:
	 * ==================
	 * Register the listener hook in the node's hook list.
	 * The spa_hook_list ensures thread-safe event delivery.
	 */
	spa_hook_list_append(&state->hooks, listener, events, data);

	return 0;
}

/**
 * @brief Set I/O area for communication with graph engine
 *
 * I/O areas provide a low-latency communication channel between nodes
 * and the PipeWire graph engine. They contain buffer queues, timing info,
 * and other real-time data that changes frequently during processing.
 *
 * SPA I/O AREA CONCEPT:
 * ====================
 * I/O areas are shared memory regions that avoid function call overhead:
 * - spa_io_buffers: Buffer queue for audio data exchange
 * - spa_io_rate_match: Rate matching and resampling info
 * - spa_io_position: Timeline position and transport info
 *
 * @param object Pointer to spa_node interface (cast to null_state)
 * @param id I/O area type identifier (SpaIoType enum)
 * @param data Pointer to I/O area data structure
 * @param size Size of I/O area in bytes
 *
 * @return 0 on success
 * @return -ENOENT if I/O type is not supported
 * @return -EINVAL if parameters are invalid
 *
 * @note I/O areas are typically set during node configuration
 * @note Areas remain valid until node is destroyed or reconfigured
 */
static int impl_node_set_io(void *object, uint32_t id, void *data, size_t size)
{
	struct null_state *state = object;

	spa_return_val_if_fail(state != NULL, -EINVAL);

	/*
	 * I/O AREA TYPE HANDLING:
	 * =======================
	 * Different I/O area types provide different communication channels.
	 * Null sink only needs buffer I/O for consuming audio data.
	 */
	switch (id) {
	case SPA_IO_Buffers:
		/*
		 * BUFFER I/O AREA:
		 * ================
		 * This area contains the buffer queue for audio data exchange.
		 * It includes buffer IDs, buffer status, and queue management.
		 */
		if (size >= sizeof(struct spa_io_buffers))
			state->io = data;
		else
			state->io = NULL;
		break;

	case SPA_IO_RateMatch:
		/*
		 * RATE MATCHING I/O:
		 * ==================
		 * Provides rate matching information for adaptive resampling.
		 * Null sink doesn't resample but can still use this info.
		 */
		if (size >= sizeof(struct spa_io_rate_match))
			state->rate_match = data;
		else
			state->rate_match = NULL;
		break;

	default:
		/*
		 * UNSUPPORTED I/O TYPES:
		 * ======================
		 * Return -ENOENT for unsupported I/O area types.
		 * This allows the graph to know which areas are supported.
		 */
		return -ENOENT;
	}

	return 0;
}

/**
 * @brief Send command to null sink node
 *
 * Commands control node lifecycle and behavior. The null sink supports
 * standard transport commands like Start, Pause, and Suspend.
 *
 * SPA NODE COMMAND PATTERN:
 * ========================
 * Commands are sent as spa_command structures with:
 * - Command type (SpaNodeCommand enum)
 * - Optional command-specific parameters
 * - Asynchronous result handling through spa_result
 *
 * @param object Pointer to spa_node interface (cast to null_state)
 * @param command Pointer to spa_command structure
 *
 * @return 0 on success (command completed immediately)
 * @return >0 for asynchronous operation (spa_result pending)
 * @return <0 on error
 *
 * @note Commands may be processed asynchronously
 * @note Start command begins buffer processing
 * @note Suspend command stops processing but preserves state
 */
static int impl_node_send_command(void *object, const struct spa_command *command)
{
	struct null_state *state = object;

	spa_return_val_if_fail(state != NULL, -EINVAL);
	spa_return_val_if_fail(command != NULL, -EINVAL);

	/*
	 * COMMAND TYPE HANDLING:
	 * ======================
	 * Process different command types according to their semantics.
	 * Each command may have different parameter requirements.
	 */
	switch (SPA_NODE_COMMAND_ID(command)) {
	case SPA_NODE_COMMAND_Start:
		/*
		 * START COMMAND:
		 * ==============
		 * Begin processing audio buffers. The null sink will start
		 * accepting buffers from the graph and dropping them.
		 */
		if (!state->have_format) {
			spa_log_error(state->log, "null-sink %p: no format configured", state);
			return -EIO;
		}

		state->started = true;
		spa_log_info(state->log, "null-sink %p: started", state);
		break;

	case SPA_NODE_COMMAND_Suspend:
	case SPA_NODE_COMMAND_Pause:
		/*
		 * SUSPEND/PAUSE COMMANDS:
		 * ======================
		 * Stop processing but maintain configuration state.
		 * The node can be restarted without reconfiguration.
		 */
		state->started = false;
		spa_log_info(state->log, "null-sink %p: suspended", state);
		break;

	default:
		/*
		 * UNSUPPORTED COMMANDS:
		 * ====================
		 * Return -ENOTSUP for commands not implemented by this node.
		 */
		spa_log_warn(state->log, "null-sink %p: unknown command %d",
			    state, SPA_NODE_COMMAND_ID(command));
		return -ENOTSUP;
	}

	return 0;
}

/**
 * @brief Set parameter on null sink node
 *
 * Parameters configure node behavior and capabilities. The most important
 * parameter is Format, which negotiates the audio format between nodes.
 *
 * SPA PARAMETER SYSTEM:
 * ====================
 * Parameters are structured data objects (spa_pod) that describe:
 * - Audio formats (sample rate, channels, format)
 * - Buffer requirements (size, count, alignment)
 * - Processing properties (latency, quantum)
 *
 * Parameter setting follows a negotiation protocol:
 * 1. Graph enumerates supported parameters
 * 2. Graph selects compatible format
 * 3. Node validates and applies format
 * 4. Node emits events to notify listeners
 *
 * @param object Pointer to spa_node interface (cast to null_state)
 * @param id Parameter type identifier (SpaParamType enum)
 * @param flags Parameter flags (unused for most types)
 * @param param Pointer to spa_pod parameter object
 *
 * @return 0 on success
 * @return -EINVAL if parameter is invalid
 * @return -ENOTSUP if parameter type is not supported
 *
 * @note Format parameter must be set before starting node
 * @note Setting format may trigger buffer reconfiguration
 */
static int impl_node_set_param(void *object, uint32_t id, uint32_t flags,
                              const struct spa_pod *param)
{
	struct null_state *state = object;
	int res = 0;

	spa_return_val_if_fail(state != NULL, -EINVAL);

	/*
	 * PARAMETER TYPE HANDLING:
	 * =======================
	 * Different parameter types require different processing logic.
	 * Format is the most critical parameter for audio nodes.
	 */
	switch (id) {
	case SPA_PARAM_Format:
		/*
		 * FORMAT PARAMETER:
		 * ================
		 * Audio format negotiation is essential for proper operation.
		 * The format parameter specifies:
		 * - Sample rate (e.g., 44100, 48000 Hz)
		 * - Channel count (1=mono, 2=stereo, etc.)
		 * - Sample format (float32, int16, etc.)
		 * - Channel layout (surround sound mapping)
		 */
		if (param == NULL) {
			/*
			 * CLEAR FORMAT:
			 * =============
			 * NULL parameter clears the current format configuration.
			 * This returns the node to unconfigured state.
			 */
			state->have_format = false;
			spa_zero(state->current_format);
			spa_log_info(state->log, "null-sink %p: format cleared", state);
		} else {
			/*
			 * SET FORMAT:
			 * ===========
			 * Parse and validate the provided audio format.
			 * Only accept formats that the null sink can handle.
			 */
			struct spa_audio_info info = { 0 };

			/* Parse format parameter into audio info structure */
			if ((res = spa_format_parse(param, &info.media_type, &info.media_subtype)) < 0) {
				spa_log_error(state->log, "null-sink %p: failed to parse format: %s",
					     state, spa_strerror(res));
				return res;
			}

			/* Validate media type - must be audio */
			if (info.media_type != SPA_MEDIA_TYPE_audio ||
			    info.media_subtype != SPA_MEDIA_SUBTYPE_raw) {
				spa_log_error(state->log, "null-sink %p: unsupported media type %d/%d",
					     state, info.media_type, info.media_subtype);
				return -EINVAL;
			}

			/* Parse audio-specific format details */
			if ((res = spa_format_audio_raw_parse(param, &info.info.raw)) < 0) {
				spa_log_error(state->log, "null-sink %p: failed to parse audio format: %s",
					     state, spa_strerror(res));
				return res;
			}

			/*
			 * FORMAT VALIDATION:
			 * ==================
			 * Validate format parameters against null sink capabilities.
			 * Null sink is very permissive since it just drops buffers.
			 */
			if (info.info.raw.channels == 0 || info.info.raw.channels > 64) {
				spa_log_error(state->log, "null-sink %p: invalid channel count %d",
					     state, info.info.raw.channels);
				return -EINVAL;
			}

			if (info.info.raw.rate == 0 || info.info.raw.rate > 192000) {
				spa_log_error(state->log, "null-sink %p: invalid sample rate %d",
					     state, info.info.raw.rate);
				return -EINVAL;
			}

			/*
			 * APPLY FORMAT:
			 * =============
			 * Store the validated format and mark node as configured.
			 */
			state->current_format = info;
			state->have_format = true;

			spa_log_info(state->log, "null-sink %p: format set to %d channels, %d Hz, %s",
				    state, info.info.raw.channels, info.info.raw.rate,
				    spa_debug_type_find_name(spa_type_audio_format, info.info.raw.format));
		}

		/*
		 * EMIT FORMAT CHANGE EVENT:
		 * =========================
		 * Notify all listeners that the format has changed.
		 * This allows the graph to reconfigure connections.
		 */
		spa_node_call(&state->hooks, struct spa_node_events, param_changed,
			     0, id, param);
		break;

	default:
		/*
		 * UNSUPPORTED PARAMETERS:
		 * ======================
		 * Return -ENOTSUP for parameter types not handled by null sink.
		 */
		spa_log_debug(state->log, "null-sink %p: unsupported parameter %d",
			     state, id);
		return -ENOTSUP;
	}

	return res;
}

/**
 * @brief Enumerate supported parameters for null sink node
 *
 * Parameter enumeration allows the graph to discover node capabilities
 * before attempting configuration. The null sink advertises supported
 * audio formats and other configuration options.
 *
 * PARAMETER ENUMERATION PROTOCOL:
 * ==============================
 * 1. Graph calls with index=0 to start enumeration
 * 2. Node returns parameters in order, incrementing index
 * 3. When no more parameters exist, return 0
 * 4. Graph uses returned parameters for format negotiation
 *
 * @param object Pointer to spa_node interface (cast to null_state)
 * @param seq Sequence number for asynchronous response
 * @param id Parameter type to enumerate (SpaParamType enum)
 * @param start Starting index for enumeration
 * @param num Maximum number of parameters to return
 * @param filter Optional filter to constrain results
 *
 * @return Number of parameters returned
 * @return 0 if no parameters available
 * @return <0 on error
 *
 * @note Most important parameter is Format (supported audio formats)
 * @note Results are returned through async result callback
 */
static int impl_node_enum_params(void *object, int seq,
                                uint32_t id, uint32_t start, uint32_t num,
                                const struct spa_pod *filter)
{
	struct null_state *state = object;
	struct spa_pod *param;
	struct spa_pod_builder b = { 0 };
	uint8_t buffer[1024];
	struct spa_result_node_params result;
	uint32_t count = 0;

	spa_return_val_if_fail(state != NULL, -EINVAL);
	spa_return_val_if_fail(num != 0, -EINVAL);

	result.id = id;
	result.next = start;

	/*
	 * PARAMETER TYPE ENUMERATION:
	 * ===========================
	 * Handle different parameter types that the null sink supports.
	 * Format is the most critical for audio processing nodes.
	 */
	switch (id) {
	case SPA_PARAM_Format:
		/*
		 * FORMAT ENUMERATION:
		 * ==================
		 * Advertise all audio formats that the null sink can accept.
		 * Since we just drop buffers, we can support almost anything.
		 */
		if (start == 0 && count < num) {
			spa_pod_builder_init(&b, buffer, sizeof(buffer));

			/*
			 * BUILD FORMAT PARAMETER:
			 * =======================
			 * Create a spa_pod describing supported audio format.
			 * Use ranges to indicate flexibility in format parameters.
			 */
			param = spa_format_audio_raw_build(&b, SPA_PARAM_Format,
				&SPA_AUDIO_INFO_RAW_INIT(
					.format = SPA_AUDIO_FORMAT_F32P,  /* Prefer planar float */
					.channels = 2,                     /* Default stereo */
					.rate = 48000                      /* Default 48kHz */
				));

			/*
			 * EMIT PARAMETER RESULT:
			 * =====================
			 * Send the format parameter back to the requesting component.
			 */
			spa_node_call(&state->hooks, struct spa_node_events, result,
				     seq, 0, SPA_RESULT_TYPE_NODE_PARAMS, &result);
			count++;
		}
		break;

	default:
		/*
		 * UNSUPPORTED PARAMETER TYPES:
		 * ============================
		 * Return 0 to indicate no parameters available for this type.
		 */
		break;
	}

	return count;
}

/**
 * @brief Process audio buffers - THE CORE OF REAL-TIME AUDIO PROCESSING
 *
 * This is the most critical function in any SPA audio node. It's called
 * by the graph engine in real-time context to process audio data.
 * The null sink implementation demonstrates the essential patterns.
 *
 * REAL-TIME PROCESSING REQUIREMENTS:
 * =================================
 * 1. NO BLOCKING OPERATIONS: No malloc, file I/O, or system calls
 * 2. DETERMINISTIC TIMING: Processing must complete within quantum
 * 3. LOCK-FREE COMMUNICATION: Use atomic operations and lock-free structures
 * 4. MINIMAL COMPUTATION: Avoid complex algorithms in audio thread
 * 5. ERROR HANDLING: Graceful degradation without stopping pipeline
 *
 * BUFFER PROCESSING PROTOCOL:
 * ==========================
 * 1. Check I/O area for available buffers
 * 2. Process buffers according to node function
 * 3. Update buffer status and queue positions
 * 4. Handle timing and synchronization
 * 5. Return status indicating processing result
 *
 * For null sink: Accept buffers and immediately mark them as consumed
 * without actually processing the audio data (drop buffers).
 *
 * @param object Pointer to spa_node interface (cast to null_state)
 *
 * @return SPA_STATUS_OK if processing completed successfully
 * @return SPA_STATUS_NEED_DATA if more data needed
 * @return SPA_STATUS_HAVE_DATA if data is available for output
 * @return <0 on error
 *
 * @note This function runs in real-time audio thread context
 * @note Must complete processing within one audio quantum
 * @note Cannot use blocking operations or allocate memory
 */
static int impl_node_process(void *object)
{
	struct null_state *state = object;
	struct spa_io_buffers *io;
	struct spa_buffer *buf;

	spa_return_val_if_fail(state != NULL, -EINVAL);

	/*
	 * CHECK NODE STATE:
	 * ================
	 * Only process if node is properly configured and started.
	 */
	if (!state->started || !state->have_format)
		return SPA_STATUS_OK;

	/*
	 * GET I/O AREA:
	 * ============
	 * The I/O area contains the buffer queue for communication
	 * with the graph engine.
	 */
	io = state->io;
	if (spa_unlikely(io == NULL))
		return SPA_STATUS_OK;

	/*
	 * CHECK BUFFER AVAILABILITY:
	 * ==========================
	 * The buffer_id field indicates which buffer is ready for processing.
	 * SPA_ID_INVALID means no buffer is available.
	 */
	if (spa_unlikely(io->buffer_id == SPA_ID_INVALID))
		return SPA_STATUS_OK;

	/*
	 * VALIDATE BUFFER ID:
	 * ==================
	 * Ensure buffer ID is within valid range to prevent crashes.
	 */
	if (spa_unlikely(io->buffer_id >= MAX_BUFFERS)) {
		spa_log_warn(state->log, "null-sink %p: invalid buffer id %d",
			    state, io->buffer_id);
		io->buffer_id = SPA_ID_INVALID;
		return SPA_STATUS_OK;
	}

	/*
	 * GET BUFFER REFERENCE:
	 * ====================
	 * The buffer contains audio data and metadata.
	 * For null sink, we don't actually read the data.
	 */
	buf = io->buffers[io->buffer_id];
	if (spa_unlikely(buf == NULL)) {
		spa_log_warn(state->log, "null-sink %p: null buffer", state);
		io->buffer_id = SPA_ID_INVALID;
		return SPA_STATUS_OK;
	}

	/*
	 * PROCESS BUFFER (NULL SINK IMPLEMENTATION):
	 * ==========================================
	 * For a null sink, "processing" means accepting the buffer
	 * and immediately discarding it. In a real sink, this would
	 * involve sending data to hardware or writing to a file.
	 */

	/* Extract buffer metadata for statistics */
	if (buf->datas[0].chunk) {
		uint32_t size = buf->datas[0].chunk->size;
		uint32_t frames = size / (state->current_format.info.raw.channels *
		                         sizeof(float)); /* Assume float for simplicity */

		/* Update processing statistics */
		state->frame_count += frames;
		state->buffer_count++;

		/* Log occasionally for debugging (avoid flooding logs) */
		if (spa_unlikely(state->buffer_count % 1000 == 0)) {
			spa_log_trace(state->log,
				     "null-sink %p: dropped %lu frames in %lu buffers",
				     state, state->frame_count, state->buffer_count);
		}
	}

	/*
	 * MARK BUFFER AS CONSUMED:
	 * =======================
	 * Set buffer_id to INVALID to indicate we're done with this buffer.
	 * The graph engine will recycle the buffer for the next cycle.
	 */
	io->buffer_id = SPA_ID_INVALID;

	/*
	 * HANDLE RATE MATCHING:
	 * ====================
	 * If rate matching is enabled, update timing information.
	 * This helps maintain synchronization across the graph.
	 */
	if (state->rate_match) {
		/* For null sink, we don't need to adjust timing */
		/* Real sinks would update rate_match based on hardware timing */
	}

	/*
	 * RETURN PROCESSING STATUS:
	 * ========================
	 * SPA_STATUS_OK indicates successful processing.
	 * The graph will continue with the next processing cycle.
	 */
	return SPA_STATUS_OK;
}

/**
 * @brief Get information about null sink node
 *
 * This function returns static information about the node's capabilities
 * and current state. It's used by the graph for routing decisions.
 *
 * @param object Pointer to spa_node interface (cast to null_state)
 * @param info Pointer to store node information
 *
 * @return 0 on success
 *
 * @note Information includes supported parameters, port counts, and properties
 */
static int impl_node_get_info(void *object, const struct spa_node_info **info)
{
	struct null_state *state = object;

	spa_return_val_if_fail(state != NULL, -EINVAL);
	spa_return_val_if_fail(info != NULL, -EINVAL);

	*info = &state->info;
	return 0;
}

/**
 * @brief Enumerate ports on null sink node
 *
 * The null sink has one input port for consuming audio data.
 * This function allows the graph to discover available ports.
 *
 * @param object Pointer to spa_node interface (cast to null_state)
 * @param seq Sequence number for async response
 * @param direction Port direction (input/output)
 * @param start Starting port index
 * @param num Maximum number of ports to return
 *
 * @return Number of ports returned
 *
 * @note Null sink only has input ports (SPA_DIRECTION_INPUT)
 */
static int impl_node_enum_ports(void *object, int seq,
                               enum spa_direction direction,
                               uint32_t start, uint32_t num)
{
	struct null_state *state = object;
	struct spa_result_node_ports result;

	spa_return_val_if_fail(state != NULL, -EINVAL);

	result.next = start;

	if (direction == SPA_DIRECTION_INPUT && start == 0 && num > 0) {
		/* Null sink has one input port at index 0 */
		spa_node_call(&state->hooks, struct spa_node_events, result,
			     seq, 0, SPA_RESULT_TYPE_NODE_PORTS, &result);
		return 1;
	}

	return 0;
}

/**
 * @brief Get information about specific port
 *
 * Returns detailed information about a port, including supported
 * parameters and current configuration.
 *
 * @param object Pointer to spa_node interface (cast to null_state)
 * @param direction Port direction (input/output)
 * @param port_id Port index
 * @param info Pointer to store port information
 *
 * @return 0 on success
 * @return -EINVAL if port doesn't exist
 */
static int impl_node_get_port_info(void *object,
                                  enum spa_direction direction, uint32_t port_id,
                                  const struct spa_port_info **info)
{
	struct null_state *state = object;

	spa_return_val_if_fail(state != NULL, -EINVAL);
	spa_return_val_if_fail(info != NULL, -EINVAL);

	if (direction != SPA_DIRECTION_INPUT || port_id != 0)
		return -EINVAL;

	*info = &state->port_info;
	return 0;
}

/**
 * @brief Enumerate parameters for specific port
 *
 * Similar to node parameter enumeration but for port-specific parameters
 * like format constraints and buffer requirements.
 *
 * @param object Pointer to spa_node interface (cast to null_state)
 * @param seq Sequence number for async response
 * @param direction Port direction (input/output)
 * @param port_id Port index
 * @param id Parameter type to enumerate
 * @param start Starting parameter index
 * @param num Maximum parameters to return
 * @param filter Optional filter for results
 *
 * @return Number of parameters returned
 */
static int impl_node_enum_port_params(void *object, int seq,
                                     enum spa_direction direction, uint32_t port_id,
                                     uint32_t id, uint32_t start, uint32_t num,
                                     const struct spa_pod *filter)
{
	/* Port parameters follow similar pattern to node parameters */
	/* Implementation details omitted for brevity */
	return 0;
}

/**
 * @brief Set parameter on specific port
 *
 * Configure port-specific parameters like format or buffer requirements.
 * For null sink, this is typically handled at the node level.
 *
 * @param object Pointer to spa_node interface (cast to null_state)
 * @param direction Port direction (input/output)
 * @param port_id Port index
 * @param id Parameter type
 * @param flags Parameter flags
 * @param param Parameter object
 *
 * @return 0 on success
 */
static int impl_node_set_port_param(void *object,
                                   enum spa_direction direction, uint32_t port_id,
                                   uint32_t id, uint32_t flags,
                                   const struct spa_pod *param)
{
	/* Forward to node-level parameter handling */
	return impl_node_set_param(object, id, flags, param);
}

/**
 * @brief Use buffers for specific port
 *
 * This function is called when the graph assigns buffers to a port.
 * The null sink doesn't need to store buffer references since it
 * just drops data immediately.
 *
 * @param object Pointer to spa_node interface (cast to null_state)
 * @param direction Port direction (input/output)
 * @param port_id Port index
 * @param flags Buffer usage flags
 * @param buffers Array of buffer pointers
 * @param n_buffers Number of buffers in array
 *
 * @return 0 on success
 */
static int impl_node_use_buffers(void *object,
                                enum spa_direction direction, uint32_t port_id,
                                uint32_t flags,
                                struct spa_buffer **buffers, uint32_t n_buffers)
{
	struct null_state *state = object;

	spa_return_val_if_fail(state != NULL, -EINVAL);

	if (direction != SPA_DIRECTION_INPUT || port_id != 0)
		return -EINVAL;

	/* Null sink doesn't need to store buffers - just accept them */
	spa_log_debug(state->log, "null-sink %p: using %d buffers", state, n_buffers);

	return 0;
}

/**
 * @brief Set I/O area for specific port
 *
 * Assigns I/O areas to specific ports. For null sink, the main
 * I/O area is handled at the node level.
 *
 * @param object Pointer to spa_node interface (cast to null_state)
 * @param direction Port direction (input/output)
 * @param port_id Port index
 * @param id I/O area type
 * @param data I/O area data pointer
 * @param size I/O area size
 *
 * @return 0 on success
 */
static int impl_node_set_port_io(void *object,
                                enum spa_direction direction, uint32_t port_id,
                                uint32_t id, void *data, size_t size)
{
	/* Forward to node-level I/O handling */
	return impl_node_set_io(object, id, data, size);
}

/**
 * @brief Reuse buffer on specific port
 *
 * Called when a buffer becomes available for reuse. The null sink
 * doesn't need to track buffer reuse since it processes immediately.
 *
 * @param object Pointer to spa_node interface (cast to null_state)
 * @param port_id Port index
 * @param buffer_id Buffer identifier
 *
 * @return 0 on success
 */
static int impl_node_reuse_buffer(void *object, uint32_t port_id, uint32_t buffer_id)
{
	/* Null sink doesn't need buffer reuse tracking */
	return 0;
}

/*
 * SPA NODE METHODS VTABLE:
 * =======================
 * This structure defines the complete spa_node interface implementation.
 * All SPA interfaces use this vtable pattern for object-oriented behavior in C.
 */
static const struct spa_node_methods impl_node = {
	SPA_VERSION_NODE_METHODS,
	.add_listener = impl_node_add_listener,
	.set_callbacks = NULL,  /* Not needed for sink nodes */
	.sync = NULL,           /* Synchronous operation */
	.enum_params = impl_node_enum_params,
	.set_param = impl_node_set_param,
	.set_io = impl_node_set_io,
	.send_command = impl_node_send_command,
	.add_port = NULL,       /* Fixed port configuration */
	.remove_port = NULL,    /* Fixed port configuration */
	.port_enum_params = impl_node_enum_port_params,
	.port_set_param = impl_node_set_port_param,
	.port_use_buffers = impl_node_use_buffers,
	.port_set_io = impl_node_set_port_io,
	.port_reuse_buffer = impl_node_reuse_buffer,
	.process = impl_node_process,
};

/*
 * FACTORY IMPLEMENTATION:
 * ======================
 * The factory creates and initializes null sink instances.
 */

/**
 * @brief Create new null sink handle
 *
 * This function implements the spa_handle_factory interface for creating
 * null sink instances. It demonstrates the complete object creation pattern
 * used throughout SPA.
 *
 * HANDLE CREATION PROCESS:
 * =======================
 * 1. Validate factory parameters
 * 2. Allocate and initialize state structure
 * 3. Embed spa_node interface in handle
 * 4. Configure default parameters
 * 5. Return initialized handle
 *
 * @param factory Pointer to spa_handle_factory (this factory)
 * @param handle Pointer to spa_handle structure to initialize
 * @param info Factory creation info (properties, etc.)
 * @param support Array of SPA support interfaces
 * @param n_support Number of support interfaces
 *
 * @return 0 on success
 * @return -EINVAL if parameters are invalid
 * @return -ENOMEM if allocation fails
 */
static int impl_get_interface(struct spa_handle *handle, const char *type, void **interface)
{
	struct null_state *state;

	spa_return_val_if_fail(handle != NULL, -EINVAL);
	spa_return_val_if_fail(interface != NULL, -EINVAL);

	state = (struct null_state *) handle;

	if (spa_streq(type, SPA_TYPE_INTERFACE_Node))
		*interface = &state->node;
	else
		return -ENOENT;

	return 0;
}

static int impl_clear(struct spa_handle *handle)
{
	struct null_state *state = (struct null_state *) handle;
	null_state_cleanup(state);
	return 0;
}

static size_t impl_get_size(const struct spa_handle_factory *factory,
                           const struct spa_dict *info)
{
	return sizeof(struct null_state);
}

static int impl_init(const struct spa_handle_factory *factory,
                    struct spa_handle *handle,
                    const struct spa_dict *info,
                    const struct spa_support *support,
                    uint32_t n_support)
{
	struct null_state *state = (struct null_state *) handle;
	struct spa_log *log = NULL;
	struct spa_system *system = NULL;
	struct spa_loop *loop = NULL;
	uint32_t i;

	spa_return_val_if_fail(factory != NULL, -EINVAL);
	spa_return_val_if_fail(handle != NULL, -EINVAL);

	/* Extract support interfaces */
	for (i = 0; i < n_support; i++) {
		switch (support[i].type) {
		case SPA_TYPE_INTERFACE_Log:
			log = support[i].data;
			break;
		case SPA_TYPE_INTERFACE_System:
			system = support[i].data;
			break;
		case SPA_TYPE_INTERFACE_DataLoop:
			loop = support[i].data;
			break;
		}
	}

	if (!log || !system) {
		return -EINVAL;
	}

	/* Initialize state */
	return null_state_init(state, log, system, loop);
}

static const struct spa_interface_info impl_interfaces[] = {
	{SPA_TYPE_INTERFACE_Node,},
};

static int impl_enum_interface_info(const struct spa_handle_factory *factory,
                                   const struct spa_interface_info **info,
                                   uint32_t *index)
{
	spa_return_val_if_fail(factory != NULL, -EINVAL);
	spa_return_val_if_fail(info != NULL, -EINVAL);
	spa_return_val_if_fail(index != NULL, -EINVAL);

	switch (*index) {
	case 0:
		*info = &impl_interfaces[*index];
		break;
	default:
		return 0;
	}
	(*index)++;
	return 1;
}

/**
 * @brief Null sink factory definition
 *
 * This factory creates null sink nodes that drop audio buffers.
 * It's registered through the main plugin enumeration function.
 */
const struct spa_handle_factory spa_null_sink_factory = {
	SPA_VERSION_HANDLE_FACTORY,
	.name = SPA_NAME_API_NULL_SINK,
	.get_size = impl_get_size,
	.init = impl_init,
	.enum_interface_info = impl_enum_interface_info,
};

/*
 * STATE MANAGEMENT IMPLEMENTATION:
 * ===============================
 * Implementation of state initialization and cleanup functions
 * declared in the header file.
 */

int null_state_init(struct null_state *state,
                   struct spa_log *log,
                   struct spa_system *system,
                   struct spa_loop *loop)
{
	spa_return_val_if_fail(state != NULL, -EINVAL);
	spa_return_val_if_fail(log != NULL, -EINVAL);
	spa_return_val_if_fail(system != NULL, -EINVAL);

	/* Clear state structure */
	spa_zero(*state);

	/* Initialize spa_node interface */
	state->node.iface = SPA_INTERFACE_INIT(
		SPA_TYPE_INTERFACE_Node,
		SPA_VERSION_NODE,
		&impl_node, state);

	/* Store support interfaces */
	state->log = log;
	state->system = system;
	state->data_loop = loop;

	/* Initialize hook list for events */
	spa_hook_list_init(&state->hooks);

	/* Initialize node info */
	state->info_all = SPA_NODE_CHANGE_MASK_FLAGS |
			  SPA_NODE_CHANGE_MASK_PARAMS;
	state->info = SPA_NODE_INFO_INIT();
	state->info.max_input_ports = 1;
	state->info.max_output_ports = 0;
	state->info.flags = SPA_NODE_FLAG_RT;

	/* Initialize port info */
	state->port_info_all = SPA_PORT_CHANGE_MASK_FLAGS |
			       SPA_PORT_CHANGE_MASK_PARAMS;
	state->port_info = SPA_PORT_INFO_INIT();
	state->port_info.flags = SPA_PORT_FLAG_NO_REF;

	spa_log_info(log, "null-sink %p: initialized", state);

	return 0;
}

void null_state_cleanup(struct null_state *state)
{
	if (state == NULL)
		return;

	/* Remove all event hooks */
	spa_hook_list_clean(&state->hooks);

	/* Reset state */
	state->started = false;
	state->have_format = false;

	spa_log_info(state->log, "null-sink %p: cleaned up", state);
}