/* Support for types. */

struct field;
struct class;
struct type;
struct attr;


/* This converts a string to a writeable integer. */
error__t type_parse(
    struct type *type, unsigned int number,
    const char *string, unsigned int *value);

/* This formats the value into a string according to the type rules. */
error__t type_format(
    struct type *type, unsigned int number,
    unsigned int value, char string[], size_t length);


/* Parses type description in name and returns type. */
error__t create_type(
    const char **string, bool forced, unsigned int count, struct type **type);

void destroy_type(struct type *type);

/* Adds attribute line to specified type. */
error__t type_parse_attribute(struct type *type, const char **line);


/* Adds type attributes to given attr_map. */
void create_type_attributes(
    struct class *class, struct type *type, struct hash_table *attr_map);


/* Formats double without leading spaces. */
error__t format_double(char result[], size_t length, double value);


/* Returns name of type. */
const char *get_type_name(const struct type *type);
