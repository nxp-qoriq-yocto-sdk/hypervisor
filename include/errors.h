/** @file
 * Error definitions.
 */

#ifndef ERRORS_H
#define ERRORS_H

#include <libos/errors.h>

#define FH_ERR_FAILED          (-1) /**< An error (non-specific) occurred during the hcall */
#define FH_ERR_INVALID_PARM    (-2) /**< An parameter supplied to the hcall was out of range or invalid */
#define FH_ERR_NO_SPACE        (-3) /**< The operation had insufficient resources to complete. */
#define FH_ERR_CONFIG          (-4) /**< There was a configuration error detected */
#define FH_ERR_INVALID_STATE   (-5) /**< The state of the object being operated on was not valid */

#endif
