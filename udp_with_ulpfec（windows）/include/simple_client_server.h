/* $Id: simple_client_server.h 207 2014-12-10 19:47:50Z roca $ */
/*
 * OpenFEC.org AL-FEC Library.
 * (c) Copyright 2009-2014 INRIA - All rights reserved
 * Contact: vincent.roca@inria.fr
 *
 * This software is governed by the CeCILL-C license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL-C
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 * therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL-C license and that you accept its terms.
 */
#ifndef SIMPLE_CLIENT_SERVER_H_
#define SIMPLE_CLIENT_SERVER_H_

//#include <stdio.h>
//#include <stdlib.h>
//#include <math.h>
//#include <string.h>
//#include <io.h>
//#include <process.h>
//#include <fcntl.h>
//#include <errno.h> 
//#include <ctype.h>
//#include <sys/types.h>
//#include <time.h>
//#include <winsock2.h>
//#pragma comment(lib, "ws2_32.lib")
//#include "of_debug.h"

 /*
  * OS dependant definitions
  */
#ifndef SOCKET
#define SOCKET		int
#endif

#ifndef SOCKADDR
#define SOCKADDR	struct sockaddr
#endif

#ifndef SOCKADDR_IN
#define SOCKADDR_IN	struct sockaddr_in
#endif

#ifndef INVALID_SOCKET
#define INVALID_SOCKET	(-1)
#endif

#ifndef SOCKET_ERROR
#define SOCKET_ERROR	(-1)
#endif

#define DEST_IP		"225.0.10.101"	/* Destination IPv4 address */
#define DEST_PORT	5557		/* Destination port (UDP) */

#endif //SIMPLE_CLIENT_SERVER_H_

