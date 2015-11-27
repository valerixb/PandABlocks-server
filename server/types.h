/* Support for types. */

struct type;


/* API used by type to access the underlying register value.  All methods in
 * this structure are optional. */
struct register_methods {
    /* Reads current register value. */
    uint32_t (*read)(void *reg_data, unsigned int number);
    /* Writes to register. */
    void (*write)(void *reg_data, unsigned int number, uint32_t value);
    /* Notifies register change. */
    void (*changed)(void *reg_data);
};



/* Reads value from associated register and formats for presentation into the
 * given result.  Implements  block.field?  method. */
error__t type_get(
    struct type *type, unsigned int number, struct connection_result *result);

/* Parses given string and writes result into associated register.  Implements
 * block.field=value  method. */
error__t type_put(struct type *type, unsigned int number, const char *string);


/* Parses type description in name and returns type.  The bound register will be
 * used read and write the underlying value for formatting. */
error__t create_type(
    const char **line, const char *default_type, unsigned int count,
    const struct register_methods *reg, void *reg_data,
    struct hash_table *attr_map, struct type **type);

/* Releases internal resources associated with type. */
void destroy_type(struct type *type);

/* Returns name of type. */
const char *get_type_name(const struct type *type);


/* Adds attribute line to specified type.  Used to add enumeration options. */
error__t type_parse_attribute(struct type *type, const char **line);
