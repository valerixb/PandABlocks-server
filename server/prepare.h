/* Data capture preparation. */

error__t initialise_prepare(void);

void terminate_prepare(void);


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Capture option line parsing. */

enum data_format {
    DATA_FORMAT_UNFRAMED,   // Raw and unframed data
    DATA_FORMAT_FRAMED,     // Framed binary data
    DATA_FORMAT_BASE64,     // Base 64 formatted data
    DATA_FORMAT_ASCII,      // ASCII numerical data
};

enum data_process {
    DATA_PROCESS_RAW,       // Unprocessed raw captured data
    DATA_PROCESS_UNSCALED,  // Integer numbers
    DATA_PROCESS_SCALED,    // Floating point scaled numbers
};

/* Data capture and processing options. */
struct data_options {
    enum data_format data_format;   // How data is transported to the client
    enum data_process data_process; // How data is processed
    bool omit_header;       // With this option the header will be omitted
    bool omit_status;       // This option will omit *all* status reports
    bool one_shot;          // Connection is closed after one experiment
    bool xml_header;        // Header is sent in XML format
};


/* Parses option line from connection request. */
error__t parse_data_options(const char *line, struct data_options *options);


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Interface for registering captured data sources. */

struct output;


/* When registering output sources and preparing data capture we need to treat a
 * number of output sources specially. */
enum prepare_class {
    PREPARE_CLASS_NORMAL,      // Normal output source
    PREPARE_CLASS_TIMESTAMP,   // Timestamp, may need special offset handling
    PREPARE_CLASS_TS_OFFSET,   // Timestamp offset
    PREPARE_CLASS_ADC_COUNT,   // ADC sample count for average calculations
};


/* This function is called during system startup to register output sources.  Up
 * to 2 separate capture indices can be registered for each source. */
error__t register_output(
    struct output *output, unsigned int number,
    const char field_name[],
    enum prepare_class prepare_class, const unsigned int capture_index[2]);

/* *CAPTURE= implementation: resets all capture settings. */
void reset_capture_list(void);

/* *CAPTURE? implementation: returns list of all captured fields. */
void report_capture_list(struct connection_result *result);

/* *CAPTURE.LABELS? implementation: returns list of all fields which can be
 * selected for capture. */
void report_capture_labels(struct connection_result *result);


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Interface data request and associated header. */

struct captured_fields;
struct data_capture;
struct data_options;
struct buffered_file;


/* Sends header describing current set of data options.  Returns false if
 * writing to the connection fails. */
bool send_data_header(
    const struct captured_fields *fields,
    const struct data_capture *capture,
    const struct data_options *options,
    struct buffered_file *file, uint64_t lost_samples);


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Data preparation. */

struct output_field;
struct scaling;


struct capture_group {
    unsigned int count;
    struct output_field **outputs;
};

enum ts_capture {
    TS_IGNORE,
    TS_CAPTURE,
    TS_OFFSET,
};

/* This contains all the information required to process the data capture
 * stream, is prepared by prepare_captured_fields() and finally processed by
 * prepare_data_capture(). */
struct captured_fields {
    /* Timestamp capture status, needs special handling. */
    enum ts_capture ts_capture;
    /* Special fields.  These may be repeated in the unscaled capture group. */
    struct output_field *timestamp;
    struct output_field *offset;
    struct output_field *adc_count;
    /* Other fields grouped by processing. */
    struct capture_group unscaled;
    struct capture_group scaled32;
    struct capture_group scaled64;
    struct capture_group adc_mean;
};


/* Call to extract set of captured fields, then call prepare_data_capture. */
const struct captured_fields *prepare_captured_fields(void);


/* This is called on each captured output to extract the information needed for
 * capture processing. */
enum framing_mode get_output_info(
    const struct output_field *output,
    unsigned int capture_index[2], struct scaling *scaling);