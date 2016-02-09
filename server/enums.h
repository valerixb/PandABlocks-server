/* Implementation of enums. */

struct enumeration;


/* An enumeration can be specified from a static list of entries, or can be
 * built dynamically. */
struct enum_entry { unsigned int value; const char *name; };
struct enum_set { const struct enum_entry *enums; size_t count; };



/* Converts converts enumeration number to string or returns NULL if index not a
 * valid enumeration value. */
const char *enum_index_to_name(
    const struct enumeration *enumeration, unsigned int value);

/* Converts string to enumeration index if possible, or returns false. */
bool enum_name_to_index(
    const struct enumeration *enumeration,
    const char *name, unsigned int *value);


/* Used to iterate over the list of enumeration values.  Use in the same way as
 * hash_table_walk: start by setting *ix=0 and call repeatedly until false is
 * returned. */
bool walk_enumerations(
    const struct enumeration *enumeration,
    size_t *ix, struct enum_entry *entry);

/* Outputs list of enumerations to given connection. */
void write_enum_labels(
    const struct enumeration *enumeration, struct connection_result *result);


/* Constructs enumeration from static enum_set.  The enum_set.enums array is not
 * copied and must remain valid. */
const struct enumeration *create_static_enumeration(
    const struct enum_set *enum_set);

/* Constructs dynamic enumeration with the given number of index entries. */
struct enumeration *create_dynamic_enumeration(size_t count);

/* Destroys enumeration created by either of the calls above. */
void destroy_enumeration(const struct enumeration *enumeration);


/* Adds enumeration entry to dynamic enumeration.  Fails with error code if
 * index out of range or if enumeration entry already present. */
error__t add_enumeration(
    struct enumeration *enumeration, const char *name, unsigned int index);


/* Helper methods for building types from an unwrapped enumeration.  If
 * type_data is a struct enumeration then the following three methods can be
 * used directly as type access methods. */

/* Parses string according to enumeration passed as type_data, assigning result
 * to *value if possible. */
error__t enum_parse(
    void *type_data, unsigned int number,
    const char *string, unsigned int *value);

/* Converts index passed as value according to enumeration passed as type_data,
 * writing result to string if possible. */
error__t enum_format(
    void *type_data, unsigned int number,
    unsigned int value, char string[], size_t length);

/* Simply returns type_data as an enumeration. */
const struct enumeration *enum_get_enumeration(void *type_data);


/* Type methods for enum. */
extern const struct type_methods enum_type_methods;


/* bit_mux and pos_mux enumerations and types. */
extern struct enumeration *pos_mux_lookup;

extern const struct type_methods pos_mux_type_methods;
