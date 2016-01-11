#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "error.h"
#include "hardware.h"
#include "parse.h"
#include "config_server.h"
#include "fields.h"
#include "classes.h"
#include "attributes.h"
#include "types.h"
#include "locking.h"
#include "mux_lookup.h"

#include "capture.h"


static pthread_mutex_t bit_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t pos_mutex = PTHREAD_MUTEX_INITIALIZER;



/* For bit and pos out classes we read all the values together and record the
 * corresponding change indexes.  This means we need a global state structure to
 * record the last reading, together with index information for each class index
 * to identify the corresponding fields per class. */

static struct {
    bool bits[BIT_BUS_COUNT];       // Current value of each bit
    uint64_t update_index[BIT_BUS_COUNT];   // Change flag for each bit
    uint32_t capture[BIT_BUS_COUNT / 32];   // Capture request for each bit
    int capture_index[BIT_BUS_COUNT / 32];  // Capture index for each bit
} bit_out_state = { };

static struct {
    uint32_t positions[POS_BUS_COUNT];      // Current array of positions
    uint64_t update_index[POS_BUS_COUNT];   // Change flag for each position
    uint32_t capture;                   // Capture request for each position
    int capture_index[POS_BUS_COUNT];   // Capture index for each position
} pos_out_state = { };



/*****************************************************************************/
/* Class initialisation. */

/* This records which kind of capture is being processed. */
enum capture_type {
    CAPTURE_BIT,        // Single bit
    CAPTURE_POSN,       // Ordinary position
    CAPTURE_ADC,        // ADC => may have extended values
    CAPTURE_CONST,      // Constant value, cannot be captured
    CAPTURE_ENCODER,    // Encoders may have extended values
};

struct capture_state {
    unsigned int count;
    enum capture_type capture_type;
    struct type *type;
    unsigned int index_array[];
};


static error__t capture_init(
    const struct register_methods *register_methods, const char *type_name,
    enum capture_type capture_type, unsigned int count,
    struct hash_table *attr_map, void **class_data)
{
    struct capture_state *state =
        malloc(sizeof(struct capture_state) + count * sizeof(unsigned int));

    *state = (struct capture_state) {
        .count = count,
        .capture_type = capture_type,
    };
    for (unsigned int i = 0; i < count; i ++)
        state->index_array[i] = UNASSIGNED_REGISTER;
    *class_data = state;

    const char *empty_line = "";
    return create_type(
        &empty_line, type_name, count, register_methods, state,
        attr_map, &state->type);
}


static error__t parse_capture_type(
    const char **line, enum capture_type *capture_type)
{
    if (**line == '\0')
        *capture_type = CAPTURE_POSN;
    else
    {
        char type_name[MAX_NAME_LENGTH];
        error__t error =
            parse_whitespace(line)  ?:
            parse_name(line, type_name, sizeof(type_name));
        if (error)
            return error;
        else if (strcmp(type_name, "adc") == 0)
            *capture_type = CAPTURE_ADC;
        else if (strcmp(type_name, "const") == 0)
            *capture_type = CAPTURE_CONST;
        else if (strcmp(type_name, "encoder") == 0)
            *capture_type = CAPTURE_ENCODER;
        else
            return FAIL_("Unknown pos_out type");
    }
    return ERROR_OK;
}


static error__t bit_out_read(
    void *reg_data, unsigned int number, uint32_t *result)
{
    struct capture_state *state = reg_data;
    LOCK(bit_mutex);
    *result = bit_out_state.bits[state->index_array[number]];
    UNLOCK(bit_mutex);
    return ERROR_OK;
}

static error__t pos_out_read(
    void *reg_data, unsigned int number, uint32_t *result)
{
    struct capture_state *state = reg_data;
    LOCK(pos_mutex);
    *result = pos_out_state.positions[state->index_array[number]];
    UNLOCK(pos_mutex);
    return ERROR_OK;
}

static const struct register_methods bit_out_methods = {
    .read = bit_out_read,
};

static const struct register_methods pos_out_methods = {
    .read = pos_out_read,
};


static error__t bit_out_init(
    const char **line, unsigned int count,
    struct hash_table *attr_map, void **class_data)
{
    return capture_init(
        &bit_out_methods, "bit", CAPTURE_BIT, count, attr_map, class_data);
}

static error__t pos_out_init(
    const char **line, unsigned int count,
    struct hash_table *attr_map, void **class_data)
{
    enum capture_type capture_type = 0;
    return
        parse_capture_type(line, &capture_type)  ?:
        capture_init(
            &pos_out_methods, "position", capture_type, count,
            attr_map, class_data);
}


/* For validation ensure that an index has been assigned to each field. */
static error__t capture_finalise(void *class_data)
{
    struct capture_state *state = class_data;
    for (unsigned int i = 0; i < state->count; i ++)
        if (state->index_array[i] == UNASSIGNED_REGISTER)
            return FAIL_("Output selector not assigned");
    return ERROR_OK;
}


static void capture_destroy(void *class_data)
{
    struct capture_state *state = class_data;
    destroy_type(state->type);
}


/* We fill in the index array and create name lookups at the same time. */

static error__t parse_out_registers(
    struct capture_state *state, const char **line, size_t limit)
{
    error__t error = ERROR_OK;
    for (unsigned int i = 0; !error  &&  i < state->count; i ++)
        error =
            parse_whitespace(line)  ?:
            parse_uint(line, &state->index_array[i])  ?:
            TEST_OK_(state->index_array[i] < limit, "Mux index out of range");
    return error;
}

static error__t capture_parse_register(
    struct mux_lookup *lookup, size_t length, struct capture_state *state,
    struct field *field, const char **line)
{
    return
        parse_out_registers(state, line, length) ?:
        add_mux_indices(lookup, field, state->count, state->index_array);
}

static error__t bit_out_parse_register(
    void *class_data, struct field *field, unsigned int block_base,
    const char **line)
{
    return capture_parse_register(
        &bit_mux_lookup, BIT_BUS_COUNT, class_data, field, line);
}

static error__t pos_out_parse_register(
    void *class_data, struct field *field, unsigned int block_base,
    const char **line)
{
    return capture_parse_register(
        &pos_mux_lookup, POS_BUS_COUNT, class_data, field, line);
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Class value access (classes are read only). */

/* The refresh method is called when we need a fresh value.  We retrieve values
 * and changed bits from the hardware and update settings accordingly. */

void do_bit_out_refresh(uint64_t change_index)
{
    LOCK(bit_mutex);
    bool changes[BIT_BUS_COUNT];
    hw_read_bits(bit_out_state.bits, changes);
    for (unsigned int i = 0; i < BIT_BUS_COUNT; i ++)
        if (changes[i]  &&  change_index > bit_out_state.update_index[i])
            bit_out_state.update_index[i] = change_index;
    UNLOCK(bit_mutex);
}

void do_pos_out_refresh(uint64_t change_index)
{
    LOCK(pos_mutex);
    bool changes[POS_BUS_COUNT];
    hw_read_positions(pos_out_state.positions, changes);
    for (unsigned int i = 0; i < POS_BUS_COUNT; i ++)
        if (changes[i]  &&  change_index > pos_out_state.update_index[i])
            pos_out_state.update_index[i] = change_index;
    UNLOCK(pos_mutex);
}


static void bit_out_refresh(void *class_data, unsigned int number)
{
    do_bit_out_refresh(get_change_index());
}

static void pos_out_refresh(void *class_data, unsigned int number)
{
    do_pos_out_refresh(get_change_index());
}


/* When reading just return the current value from our static state. */

static error__t capture_get(
    void *class_data, unsigned int number, struct connection_result *result)
{
    struct capture_state *state = class_data;
    return type_get(state->type, number, result);
}


/* Computation of change set. */
static void bit_pos_change_set(
    struct capture_state *state, const uint64_t update_index[],
    const uint64_t report_index, bool changes[])
{
    for (unsigned int i = 0; i < state->count; i ++)
        changes[i] = update_index[state->index_array[i]] > report_index;
}

static void bit_out_change_set(
    void *class_data, const uint64_t report_index, bool changes[])
{
    LOCK(bit_mutex);
    bit_pos_change_set(
        class_data, bit_out_state.update_index, report_index, changes);
    UNLOCK(bit_mutex);
}

static void pos_out_change_set(
    void *class_data, const uint64_t report_index, bool changes[])
{
    LOCK(pos_mutex);
    bit_pos_change_set(
        class_data, pos_out_state.update_index, report_index, changes);
    UNLOCK(pos_mutex);
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Attributes. */


/* Update the bit and pos capture index arrays.  This needs to be called each
 * time either capture mask is written. */
static void update_capture_index(void)
{
    int capture_index = 0;

    /* Position capture. */
    for (unsigned int i = 0; i < POS_BUS_COUNT; i ++)
        if (pos_out_state.capture & (1U << i))
            pos_out_state.capture_index[i] = capture_index++;
        else
            pos_out_state.capture_index[i] = -1;

    /* Bit capture. */
    for (unsigned int i = 0; i < BIT_BUS_COUNT / 32; i ++)
        if (bit_out_state.capture[i])
            bit_out_state.capture_index[i] = capture_index++;
        else
            bit_out_state.capture_index[i] = -1;
}


static error__t bit_out_capture_format(
    void *owner, void *data, unsigned int number,
    char result[], size_t length)
{
    struct capture_state *state = data;
    unsigned int ix = state->index_array[number];
    bool capture = bit_out_state.capture[ix / 32] & (1U << (ix % 32));
    return format_string(result, length, "%d", capture);
}

static error__t pos_out_capture_format(
    void *owner, void *data, unsigned int number,
    char result[], size_t length)
{
    struct capture_state *state = data;
    unsigned int ix = state->index_array[number];
    bool capture = pos_out_state.capture & (1U << (ix % 32));
    return format_string(result, length, "%d", capture);
}


static void update_bit(uint32_t *target, unsigned int ix, bool value)
{
    if (value)
        *target |= 1U << ix;
    else
        *target &= ~(1U << ix);
}

static error__t bit_out_capture_put(
    void *owner, void *data, unsigned int number, const char *value)
{
    struct capture_state *state = data;
    unsigned int ix = state->index_array[number];

    bool capture;
    error__t error =
        parse_bit(&value, &capture)  ?:
        parse_eos(&value);

    if (!error)
    {
        update_bit(&bit_out_state.capture[ix / 32], ix % 32, capture);
        uint32_t capture_mask = 0;
        for (unsigned int i = 0; i < BIT_BUS_COUNT / 32; i ++)
            capture_mask |= (uint32_t) (bool) bit_out_state.capture[i] << i;
//         hw_write_bit_capture(capture_mask);
        update_capture_index();
    }
    return error;
}

static error__t pos_out_capture_put(
    void *owner, void *data, unsigned int number, const char *value)
{
    struct capture_state *state = data;
    unsigned int ix = state->index_array[number];

    bool capture;
    error__t error =
        parse_bit(&value, &capture)  ?:
        parse_eos(&value);

    if (!error)
    {
        update_bit(&pos_out_state.capture, ix, capture);
//         hw_write_position_capture_masks(pos_out_state.capture, 0, 0);
        update_capture_index();
    }
    return error;
}

static error__t bit_out_index_format(
    void *owner, void *data, unsigned int number,
    char result[], size_t length)
{
    struct capture_state *state = data;
    unsigned int ix = state->index_array[number];

    int capture_index = bit_out_state.capture_index[ix / 32];
    return
        IF_ELSE(capture_index >= 0,
            format_string(result, length, "%d:%d", capture_index, ix % 32),
        // else
            DO(*result = '\0'));
}

static error__t pos_out_index_format(
    void *owner, void *data, unsigned int number,
    char result[], size_t length)
{
    struct capture_state *state = data;
    unsigned int ix = state->index_array[number];

    int capture_index = pos_out_state.capture_index[ix];
    return
        IF_ELSE(capture_index >= 0,
            format_string(result, length, "%d", capture_index),
        // else
            DO(*result = '\0'));
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Capture enumeration. */

void report_capture_list(struct connection_result *result)
{
    /* Position capture. */
    for (unsigned int i = 0; i < POS_BUS_COUNT; i ++)
        if (pos_out_state.capture & (1U << i))
            result->write_many(
                result->write_context,
                mux_lookup_get_name(&pos_mux_lookup, i));

    /* Bit capture. */
    for (unsigned int i = 0; i < BIT_BUS_COUNT / 32; i ++)
        if (bit_out_state.capture[i])
        {
            char string[MAX_NAME_LENGTH];
            snprintf(string, sizeof(string), "*BITS%d", i);
            result->write_many(result->write_context, string);
        }

    result->response = RESPONSE_MANY;
}


void report_capture_bits(struct connection_result *result, unsigned int group)
{
    for (unsigned int i = 0; i < 32; i ++)
        result->write_many(result->write_context,
            mux_lookup_get_name(&bit_mux_lookup, 32*group + i) ?: "");
    result->response = RESPONSE_MANY;
}


void report_capture_positions(struct connection_result *result)
{
    for (unsigned int i = 0; i < POS_BUS_COUNT; i ++)
        result->write_many(result->write_context,
            mux_lookup_get_name(&pos_mux_lookup, i) ?: "");
    result->response = RESPONSE_MANY;
}


void reset_capture_list(void)
{
    for (unsigned int i = 0; i < BIT_BUS_COUNT / 32; i ++)
        bit_out_state.capture[i] = 0;
    pos_out_state.capture = 0;
    update_capture_index();
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Startup and shutdown. */


error__t initialise_capture(void)
{
    initialise_mux_lookup();
    update_capture_index();
    return ERROR_OK;
}


void terminate_capture(void)
{
    terminate_mux_lookup();
}


const struct class_methods bit_out_class_methods = {
    "bit_out",
    .init = bit_out_init,
    .parse_register = bit_out_parse_register,
    .finalise = capture_finalise,
    .destroy = capture_destroy,
    .get = capture_get, .refresh = bit_out_refresh,
    .change_set = bit_out_change_set,
    .change_set_index = CHANGE_IX_BITS,
    .attrs = (struct attr_methods[]) {
        { "CAPTURE", true,
            .format = bit_out_capture_format,
            .put = bit_out_capture_put,
        },
        { "CAPTURE_INDEX",
            .format = bit_out_index_format,
        },
    },
    .attr_count = 2,
};

const struct class_methods pos_out_class_methods = {
    "pos_out",
    .init = pos_out_init,
    .parse_register = pos_out_parse_register,
    .finalise = capture_finalise,
    .destroy = capture_destroy,
    .get = capture_get, .refresh = pos_out_refresh,
    .change_set = pos_out_change_set,
    .change_set_index = CHANGE_IX_POSITION,
    .attrs = (struct attr_methods[]) {
        { "CAPTURE", true,
            .format = pos_out_capture_format,
            .put = pos_out_capture_put,
        },
        { "CAPTURE_INDEX",
            .format = pos_out_index_format,
        },
    },
    .attr_count = 2,
};
