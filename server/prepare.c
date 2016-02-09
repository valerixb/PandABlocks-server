/* Data capture preparation. */

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "error.h"
#include "parse.h"
#include "buffered_file.h"
#include "config_server.h"
#include "data_server.h"
#include "output.h"

#include "prepare.h"




/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Output fields registration. */

/* This structure is used to record a single registered output field. */
struct output_field {
    /* The field is identified by the output and index number. */
    const struct output *output;
    unsigned int number;

    /* The field name is computed at output registration. */
    const char *field_name;
    /* The two capture index values for this field. */
    unsigned int capture_index[2];

    /* The following fields are updated during capture preparation. */
    enum framing_mode framing_mode;     // Used to configure hardware framing
    struct scaling scaling;             // Scaling for fields with scaling
};


/* All registered outputs. */
static struct output_field *output_fields[MAX_OUTPUT_COUNT];
static unsigned int output_field_count;

/* Offsets into outputs of the three special fields. */
static struct output_field *timestamp_output;
static struct output_field *offset_output;
static struct output_field *adc_count_output;


/* The three special fields need to be remembered separately. */
static void process_special_field(
    enum output_class output_class, struct output_field *field)
{
    switch (output_class)
    {
        case OUTPUT_CLASS_NORMAL:
            break;
        case OUTPUT_CLASS_TIMESTAMP:
            ASSERT_OK(timestamp_output == NULL);
            timestamp_output = output_fields[output_field_count];
            break;
        case OUTPUT_CLASS_TS_OFFSET:
            ASSERT_OK(offset_output == NULL);
            offset_output = output_fields[output_field_count];
            break;
        case OUTPUT_CLASS_ADC_COUNT:
            ASSERT_OK(adc_count_output == NULL);
            adc_count_output = output_fields[output_field_count];
            break;
    }
}


void register_outputs(
    const struct output *output, unsigned int count,
    enum output_class output_class, const unsigned int capture_index[][2])
{
    ASSERT_OK(output_field_count + count <= MAX_OUTPUT_COUNT);
    for (unsigned int i = 0; i < count; i ++)
    {
        char field_name[MAX_NAME_LENGTH];
        format_output_name(output, i, field_name, sizeof(field_name));

        struct output_field *field = malloc(sizeof(struct output_field));
        *field = (struct output_field) {
            .output = output,
            .number = i,
            .field_name = strdup(field_name),
            .capture_index = { capture_index[i][0], capture_index[i][1], },
        };

        output_fields[output_field_count++] = field;
        process_special_field(output_class, field);
    }
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Header formatting. */


bool send_data_header(
    const struct captured_fields *fields,
    const struct data_capture *capture,
    const struct data_options *options,
    struct buffered_file *file, uint64_t lost_samples)
{
    write_formatted_string(
        file, "header: lost %"PRIu64" samples\n", lost_samples);
    write_string(file, "header\n", 7);
    return flush_out_buf(file);
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Output preparation. */

enum framing_mode get_output_info(
    const struct output_field *output,
    unsigned int capture_index[2], struct scaling *scaling)
{
    capture_index[0] = output->capture_index[0];
    capture_index[1] = output->capture_index[1];
    *scaling = output->scaling;
    return output->framing_mode;
}


/* This structure is updated by calling prepare_captured_fields. */
static struct captured_fields captured_fields;


const struct captured_fields *prepare_captured_fields(void)
{
    captured_fields.ts_capture = TS_IGNORE;
    captured_fields.timestamp = timestamp_output;
    captured_fields.offset = offset_output;
    captured_fields.adc_count = adc_count_output;

    captured_fields.unscaled.count = 0;
    captured_fields.scaled32.count = 0;
    captured_fields.scaled64.count = 0;
    captured_fields.adc_mean.count = 0;

    /* Walk the list of outputs and gather them into their groups. */
    for (unsigned int i = 0; i < output_field_count; i ++)
    {
        struct output_field *output = output_fields[i];

        /* Fetch and store the current capture settings for this field. */
        enum capture_mode capture_mode = get_capture_mode(
            output->output, output->number,
            &output->framing_mode, &output->scaling);

        /* Dispatch output into the appropriate group for processing. */
        struct capture_group *capture = NULL;
        switch (capture_mode)
        {
            case CAPTURE_OFF:       break;
            case CAPTURE_UNSCALED:  capture = &captured_fields.unscaled; break;
            case CAPTURE_SCALED32:  capture = &captured_fields.scaled32; break;
            case CAPTURE_SCALED64:  capture = &captured_fields.scaled64; break;
            case CAPTURE_ADC_MEAN:  capture = &captured_fields.adc_mean; break;

            case CAPTURE_TS_NORMAL:
                captured_fields.ts_capture = TS_CAPTURE;
                break;
            case CAPTURE_TS_OFFSET:
                captured_fields.ts_capture = TS_OFFSET;
                break;
        }
        if (capture)
            capture->outputs[capture->count++] = output;
    }

    return &captured_fields;
}
