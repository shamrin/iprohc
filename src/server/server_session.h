/*
 * This file is part of iprohc.
 *
 * iprohc is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * any later version.
 *
 * iprohc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with iprohc.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file   server_session.h
 * @brief  The context of the client session at server
 * @author Didier Barvaux <didier.barvaux@toulouse.viveris.com>
 */

#ifndef IPROHC_SERVER_SESSION__H
#define IPROHC_SERVER_SESSION__H

#include "session.h"

#include <stdbool.h>

/** The context of the client session at server */
struct iprohc_server_session
{
	bool is_init;                   /**< Whether the client session is used or not */
	struct iprohc_session session;  /**< The generic session context */

	struct timeval last_keepalive;  /** The time at which the last keepalive was sent */
	int packing;                    /** The packing level enforced by client */
};

#endif

