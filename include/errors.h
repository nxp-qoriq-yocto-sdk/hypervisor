/** @file
 * Error definitions.
 */

#ifndef ERRORS_H
#define ERRORS_H

#include <libos/errors.h>

#define EAGAIN		11	/**< The operation had insufficient resources to complete and should be retried later. */
#define ENOMEM		12	/**< There was insufficient memory to complete the operation */
#define EFAULT		16	/**< Bad guest address */
#define EINVAL		22	/**< An argument supplied to the hcall was out of range or invalid */

#define FH_ERR_INTERNAL         1024    /**< An internal error occured */
#define FH_ERR_CONFIG           1025    /**< A configuration error was detected */
#define FH_ERR_INVALID_STATE    1026    /**< The object is in an invalid state */
#define FH_ERR_UNIMPLEMENTED    1027    /**< Unimplemented hypercall */

#endif
