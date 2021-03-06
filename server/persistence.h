/* Support for persistence. */

/* The persistence loop periodically writes the entire saved state to a file.
 * The loop is controlled by three timeouts:
 *  - poll_rate     This determines how often we check for changes
 *  - holdoff       This is delay from change detection to performing write
 *  - backoff       This delay is used after writing before resuming polling
 * The idea is to detect changes promptly, give them time to be finished, but to
 * try to avoid writing changes too frequently. */
error__t initialise_persistence(
    const char *file_name,
    unsigned int poll_interval, unsigned int holdoff_interval,
    unsigned int backoff_interval);

/* This starts the persistence thread, to be called after daemonising. */
error__t start_persistence(void);

/* This will ensure that the persistence state is updated, so should be called
 * after all active threads have been closed. */
void terminate_persistence(void);

/* This forces an immediate save of the persistence state. */
error__t save_persistent_state(void);
