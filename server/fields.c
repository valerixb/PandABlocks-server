/* Fields and field classes. */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "hardware.h"
#include "hashtable.h"
#include "parse.h"
#include "config_server.h"
#include "types.h"
#include "classes.h"
#include "attributes.h"

#include "fields.h"



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Core structure definitions. */

/* The top level entities are blocks. Each block has a name, a number of
 * instances, a register base used for all block register methods, and a table
 * of fields. */
struct block {
    char *name;                 // Block name
    unsigned int count;         // Number of instances of this block
    unsigned int base;          // Block register base
    struct hash_table *fields;  // Map from field name to fields
    char *description;          // User readable description
};


/* The field structure contains all of the data associated with a field.  Each
 * field has a name and a class and (depending on the class) possibly a type.
 * There is also a register associated with each field, and depending on the
 * class there may be further type specific data. */
struct field {
    const struct block *block;      // Parent block
    char *name;                     // Field name
    unsigned int sequence;          // Field sequence number
    char *description;              // User readable description
    struct class *class;            // Class defining hardware interface and
    struct hash_table *attrs;       // Attribute lookup table
};



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Top level block and field API. */


/* Map of block names. */
static struct hash_table *block_map;


/* A couple of helpers for walking the block and field maps. */
#define _id_FOR_EACH_TYPE(ix, type, test, map, value) \
    size_t ix = 0; \
    for (type value; \
         (test)  &&  hash_table_walk(map, &ix, NULL, (void **) &value); )
/* Called thus:
 *  FOR_EACH_TYPE(type, test, map, value) { loop }
 *
 * type     Type of values in map
 * test     Condition for testing loop: loops while test is true
 * map      Hash table containing set of value for iteration
 * value    Name of variable to which each value is assigned. */
#define FOR_EACH_TYPE(args...)  _id_FOR_EACH_TYPE(UNIQUE_ID(), args)

#define FOR_EACH_BLOCK_WHILE(cond, block_var) \
    FOR_EACH_TYPE(struct block *, cond, block_map, block_var)
#define FOR_EACH_BLOCK(block_var) FOR_EACH_BLOCK_WHILE(true, block_var)

#define FOR_EACH_FIELD_WHILE(args...) FOR_EACH_TYPE(struct field *, args)
#define FOR_EACH_FIELD(args...) FOR_EACH_FIELD_WHILE(true, args)


error__t lookup_block(
    const char *name, struct block **block, unsigned int *count)
{
    return
        TEST_OK_(*block = hash_table_lookup(block_map, name),
            "No such block")  ?:
        DO(if (count)  *count = (*block)->count);
}


error__t lookup_field(
    const struct block *block, const char *name, struct field **field)
{
    return TEST_OK_(
        *field = hash_table_lookup(block->fields, name),
        "No such field");
}


error__t lookup_attr(
    const struct field *field, const char *name, struct attr **attr)
{
    /* Both classes and types can have attributes.  Try the type attribute
     * first, fail if neither succeeds. */
    return
        TEST_OK_(*attr = hash_table_lookup(field->attrs, name),
            "No such attribute");
}


const char *get_block_description(struct block *block)
{
    return block->description;
}

const char *get_field_description(struct field *field)
{
    return field->description;
}


error__t block_list_get(struct connection_result *result)
{
    FOR_EACH_BLOCK(block)
    {
        char value[MAX_RESULT_LENGTH];
        snprintf(value, sizeof(value), "%s %d", block->name, block->count);
        result->write_many(result->write_context, value);
    }
    result->response = RESPONSE_MANY;
    return ERROR_OK;
}


error__t field_list_get(
    const struct block *block, struct connection_result *result)
{
    FOR_EACH_FIELD(block->fields, field)
    {
        char string[MAX_RESULT_LENGTH];
        size_t length = (size_t) snprintf(string, sizeof(string), "%s %u ",
            field->name, field->sequence);
        describe_class(field->class, string + length, sizeof(string) - length);

        result->write_many(result->write_context, string);
    }
    result->response = RESPONSE_MANY;
    return ERROR_OK;
}


error__t attr_list_get(struct field *field, struct connection_result *result)
{
    size_t ix = 0;
    const void *key;
    while (hash_table_walk(field->attrs, &ix, &key, NULL))
        result->write_many(result->write_context, key);
    result->response = RESPONSE_MANY;
    return ERROR_OK;
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Field read and write.  These all just delegate the implementation down to the
 * class. */

error__t field_get(
    struct field *field, unsigned int number, struct connection_result *result)
{
    return class_get(field->class, number, true, result);
}

error__t field_put(struct field *field, unsigned int number, const char *string)
{
    return class_put(field->class, number, string);
}

error__t field_put_table(
    struct field *field, unsigned int number,
    bool append, struct put_table_writer *writer)
{
    return class_put_table(field->class, number, append, writer);
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Change set management. */

/* Alas it is possible for an error to be detected during formatting when
 * generating a change report.  If this occurs we back up over the value being
 * written and write an error mark instead. */
static void handle_error_report(
    char *string, size_t length, size_t prefix, error__t error)
{
    if (error)
    {
        prefix -= 1;
        string[prefix] = '\0';
        ERROR_REPORT(error, "Error reporting *CHANGES for %s", string);
        snprintf(string + prefix, length - prefix, " (error)");
    }
}


size_t format_field_name(
    char *string, size_t length,
    const struct field *field, const struct attr *attr,
    unsigned int number, char suffix)
{
    size_t result;
    if (field->block->count == 1)
        result = (size_t) snprintf(string, length, "%s.%s",
            field->block->name, field->name);
    else
        result = (size_t) snprintf(string, length, "%s%d.%s",
            field->block->name, number + 1, field->name);
    ASSERT_OK(result < length);
    if (attr)
        result += (size_t) snprintf(string + result, length - result,
            ".%s", get_attr_name(attr));
    ASSERT_OK(result < length + 1);
    string[result++] = suffix;
    string[result] = '\0';
    return result;
}


/* Placeholder for formatting result.  If the class wants to return a multi-line
 * result then this let's us just throw it away. */
static void dummy_write_many(void *write_context, const char *string) { }

static void report_changed_value(
    const struct field *field, unsigned int number,
    struct connection_result *result)
{
    char string[MAX_RESULT_LENGTH];
    size_t prefix =
        format_field_name(string, sizeof(string), field, NULL, number, '=');

    /* Use the class's own formatting method to format into the result string
     * via our own connection result.  If a multiple string result is returned
     * then, for the moment, we report an unexpected error. */
    struct connection_result format_result = {
        .string = string + prefix,
        .length = sizeof(string) - prefix,
        .write_many = dummy_write_many,
    };
    handle_error_report(string, sizeof(string), prefix,
        class_get(field->class, number, false, &format_result)  ?:
        TEST_OK(format_result.response == RESPONSE_ONE));
    result->write_many(result->write_context, string);
}


static void report_changed_attr(
    struct field *field, struct attr *attr, unsigned int number,
    struct connection_result *result)
{
    char string[MAX_RESULT_LENGTH];
    size_t prefix =
        format_field_name(string, sizeof(string), field, attr, number, '=');

    struct connection_result format_result = {
        .string = string + prefix,
        .length = sizeof(string) - prefix,
        .write_many = dummy_write_many,
    };
    handle_error_report(string, sizeof(string), prefix,
        attr_get(attr, number, &format_result)  ?:
        TEST_OK(format_result.response == RESPONSE_ONE));
    result->write_many(result->write_context, string);
}


static void report_changed_table(
    struct field *field, unsigned int number,
    struct connection_result *result)
{
    char string[MAX_RESULT_LENGTH];
    format_field_name(string, sizeof(string), field, NULL, number, '<');
    result->write_many(result->write_context, string);
}


static void generate_attr_change_sets(
    struct connection_result *result, struct field *field,
    uint64_t report_index)
{
    /* Also work through all attributes for their change sets. */
    FOR_EACH_TYPE(struct attr *, true, field->attrs, attr)
    {
        bool changes[field->block->count];
        get_attr_change_set(attr, report_index, changes);
        for (unsigned int i = 0; i < field->block->count; i ++)
            if (changes[i])
                report_changed_attr(field, attr, i, result);
    }
}


/* Walks all fields and generates a change event for all changed fields. */
void generate_change_sets(
    struct connection_result *result, enum change_set change_set)
{
    /* Get the change index for this connection and update it so the next
     * changes request will be up to date.  Use a fresh index for this. */
    uint64_t report_index[CHANGE_SET_SIZE];
    update_change_index(result->change_set_context, change_set, report_index);
    refresh_class_changes(change_set);

    /* Work through all fields in all blocks. */
    FOR_EACH_BLOCK(block)
    {
        FOR_EACH_FIELD(block->fields, field)
        {
            bool changes[block->count];
            get_class_change_set(
                field->class, change_set & (enum change_set) ~CHANGES_TABLE,
                report_index, changes);
            for (unsigned int i = 0; i < block->count; i ++)
                if (changes[i])
                    report_changed_value(field, i, result);

            /* We need to report table changes separately, totally different
             * reporting syntax! */
            get_class_change_set(
                field->class, change_set & CHANGES_TABLE,
                report_index, changes);
            for (unsigned int i = 0; i < block->count; i ++)
                if (changes[i])
                    report_changed_table(field, i, result);

            if (change_set & CHANGES_ATTR)
                generate_attr_change_sets(
                    result, field, report_index[CHANGE_IX_ATTR]);
        }
    }
    result->response = RESPONSE_MANY;
}


/* This is a cut down version of generate_change_sets without reporting. */
bool check_change_set(
    struct change_set_context *change_set_context, enum change_set change_set)
{
    uint64_t report_index[CHANGE_SET_SIZE];
    update_change_index(change_set_context, change_set, report_index);
    refresh_class_changes(change_set);

    FOR_EACH_BLOCK(block)
    {
        FOR_EACH_FIELD(block->fields, field)
        {
            bool changes[block->count];
            get_class_change_set(
                field->class, change_set, report_index, changes);
            for (unsigned int i = 0; i < block->count; i ++)
                if (changes[i])
                    return true;

            if (change_set & CHANGES_ATTR)
            {
                FOR_EACH_TYPE(struct attr *, true, field->attrs, attr)
                {
                    get_attr_change_set(
                        attr, report_index[CHANGE_IX_ATTR], changes);
                    for (unsigned int i = 0; i < block->count; i ++)
                        if (changes[i])
                            return true;
                }
            }
        }
    }
    return false;
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Initialisation and shutdown. */


error__t initialise_fields(void)
{
    /* Top level block map.  This will be initialised from the configuration
     * file. */
    block_map = hash_table_create(false);
    return ERROR_OK;
}


/* We implement orderly shutdown so that we can easily detect memory leaks
 * during development. */

static void destroy_field(struct field *field)
{
    free(field->name);
    if (field->class)
        destroy_class(field->class);
    free(field->description);
    delete_attributes(field->attrs);
    hash_table_destroy(field->attrs);
    free(field);
}

static void destroy_block(struct block *block)
{
    FOR_EACH_FIELD(block->fields, field)
        destroy_field(field);
    hash_table_destroy(block->fields);
    free(block->name);
    free(block->description);
    free(block);
}

void terminate_fields(void)
{
    if (block_map)
    {
        FOR_EACH_BLOCK(block)
            destroy_block(block);
        hash_table_destroy(block_map);
    }
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Field and database creation and validation. */


error__t create_block(
    struct block **block, const char *name, unsigned int count)
{
    *block = malloc(sizeof(struct block));
    **block = (struct block) {
        .name = strdup(name),
        .count = count,
        .base = UNASSIGNED_REGISTER,
        .fields = hash_table_create(false),
    };
    return TEST_OK_(
        hash_table_insert(block_map, (*block)->name, *block) == NULL,
        "Block %s already exists", name);
}


error__t block_set_register(struct block *block, unsigned int base)
{
    return
        TEST_OK_(block->base == UNASSIGNED_REGISTER,
            "Register already assigned")  ?:
        DO(block->base = base);
}


error__t block_set_description(struct block *block, const char *description)
{
    return
        TEST_OK_(block->description == NULL, "Description already set")  ?:
        DO(block->description = strdup(description));
}


static struct field *create_field_block(
    const struct block *block, const char *name)
{
    struct field *field = malloc(sizeof(struct field));
    *field = (struct field) {
        .block = block,
        .name = strdup(name),
        .sequence = (unsigned int) hash_table_count(block->fields),
        .attrs = hash_table_create(false),
    };
    return field;
}

error__t create_field(
    const char **line, struct field **field, const struct block *block)
{
    char field_name[MAX_NAME_LENGTH];
    return
        parse_name(line, field_name, sizeof(field_name))  ?:
        parse_whitespace(line)  ?:
        DO(*field = create_field_block(block, field_name))  ?:
        TRY_CATCH(
            create_class(
                line, block->count, (*field)->attrs, &(*field)->class)  ?:
            /* Insert the field into the blocks map of fields. */
            TEST_OK_(
                hash_table_insert(
                    block->fields, (*field)->name, *field) == NULL,
                "Field %s.%s already exists", block->name, field_name),

        // catch
            destroy_field(*field)
        );
}


error__t field_parse_attribute(struct field *field, const char **line)
{
    return class_parse_attribute(field->class, line);
}


error__t field_parse_register(struct field *field, const char **line)
{
    return class_parse_register(field->class, field, line);
}


error__t field_set_description(struct field *field, const char *description)
{
    return
        TEST_OK_(field->description == NULL, "Description already set")  ?:
        DO(field->description = strdup(description));
}


/* Ensure that every block and field has valid register assignments. */
error__t validate_fields(void)
{
    error__t error = ERROR_OK;
    FOR_EACH_BLOCK_WHILE(!error, block)
    {
        error = TEST_OK_(block->base != UNASSIGNED_REGISTER,
            "No base address for block %s", block->name);
        FOR_EACH_FIELD_WHILE(!error, block->fields, field)
        {
            error = finalise_class(field->class, block->base);
            if (error)
                error_extend(error,
                    "Checking field %s.%s", block->name, field->name);
        }
    }
    return error;
}
