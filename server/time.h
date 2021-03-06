/* Time class and type support. */

error__t initialise_time(void);
void terminate_time(void);

/* We have both a time class and a time type because a type can only be used for
 * 32 bit data, whereas we need a class for some instances of 48 bit times. */
extern const struct class_methods time_class_methods;
extern const struct type_methods time_type_methods;
