/*--------------------------------------------------------------------------
 Copyright 1999, Disappearing, Inc.  http://www.disappearing.com/
 See the file COPYING

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
--------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------
 Trivial circular buffer class for holding bytes on their way to or
 from a socket.
--------------------------------------------------------------------------*/

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "nbbio.h"
// change to #if 1 to enable DPRINT in this module
#if 0
#include "dprint.h"
#else
#define DPRINT(s)
#endif  

/*-----------------------------------------------------------------
 Call this to put bytes into the buffer.  
 Returns 0 on success, unix error code on failure.
-----------------------------------------------------------------*/
int nbbio::put(const char *p, int n)
{
	int ncopy;

	DPRINT(("nbbio::put: n %d, putTo %d, p='%s'\n", n, m_putTo, p));

	/* Abort if message won't fit now (or ever) */
	if (bytesFree() < n) {
		if (n >= nbbio_BUFLEN)
			return EMSGSIZE;	/* would never fit */
		return EWOULDBLOCK;		/* won't fit just now */
	}

	/* At this point, we know we will succeed.  Figure out how
	 * many bytes to copy in the first half, copy them, and update
	 * the m_putTo.
	 */
	ncopy = contigBytesFree();
	if (ncopy > n)
		ncopy = n;
	DPRINT(("nbbio::put: memcpy(%d, p, %d)\n", m_putTo, ncopy));
	memcpy(m_buf+m_putTo, p, ncopy);
	n -= ncopy;
	m_putTo += ncopy;

	/* If we have wrapped around the end of the buffer, */
	if (m_putTo == nbbio_BUFLEN) {
		m_putTo = 0;

		/* Figure out how many bytes to copy in the second half, copy, update. */
		if (n > 0) {
			DPRINT(("nbbio::put: 2nd memcpy(0, p, %d)\n", n));
			memcpy(m_buf, p + ncopy, n);
			m_putTo = n;
		}
	}

	return 0;
}


/*-----------------------------------------------------------------
 Call this to read a line from the buffer.
 Expects the line to end in CR LF (but allows them to end in just CR
 or LF).  Strips the CR LF and null terminates the line during copy.

 Note: wierd interface: If it can't read a whole line, it reads part
 of the line into linebuf, and returns EWOULDBLOCK, in which case
 the caller should try again later (after calling fillFrom()).
 The caller must not touch linebuf until readline returns zero, 
 as this routine uses linebuf as temporary storage.

 Returns 0 on success, unix error code on failure.
-----------------------------------------------------------------*/
int nbbio::readline(char *linebuf, int maxlen)
{
	char c;

	DPRINT(("nbbio::readline: start.  m_linelen %d, m_lastchar %x\n", m_linelen, m_lastchar));

	if (isEmpty())
		return EWOULDBLOCK;		/* thanks for playing. */

	while (!isEmpty() && (m_linelen < maxlen)) {
		c = readc();
		//DPRINT(("nbbio::readline: got %x (%c). m_linelen %d, m_lastchar %x\n", c, (isprint(c)?c:'?'), m_linelen, m_lastchar));
		if (c == '\n') {
			// LF
			if (m_lastchar != '\r') {
				// plain old LF
				linebuf[m_linelen++] = 0;
				m_lastchar = c;
				m_linelen = 0;
				return 0;
			}
			// If we get here, this is LF part of a CR LF.  Silently ignore.
		} else if (c == '\r') {
			// CR
			linebuf[m_linelen++] = 0;
			m_lastchar = c;
			m_linelen = 0;
			return 0;
		} else {
			linebuf[m_linelen++] = c;
			m_lastchar = c;
		}
	}

	if (m_linelen == maxlen)
		return ENOSPC;		/* no room for null byte */

	return EWOULDBLOCK;		/* We read stuff, but no EOL yet. */
}

/*-----------------------------------------------------------------
 For output buffers, call this periodically to write part or all 
 of the buffer to a nonblocking file descriptor (typically a socket,
 the same one every time).
 (Typically called after put(), and after ::poll() says fd is ready 
 for reading.)
 Returns 0 on success, unix error code on failure.
-----------------------------------------------------------------*/
int nbbio::flushTo(int fd)
{
	int contigused = contigBytesUsed();
	int nwrite;
	if (!contigused)
		return 0;
	nwrite = write(fd, m_buf+m_getFrom, contigused);
	DPRINT(("nbbio::flushTo: fd %d, contigused %d, p='%.*s', wrote %d\n", fd, contigused, nwrite, m_buf+m_getFrom, nwrite));
	if (nwrite < 0)
		return errno;
	m_getFrom += nwrite;

	/* If we have wrapped around the end of the buffer, */
	if (m_getFrom == nbbio_BUFLEN) {
		m_getFrom = 0;
		if (!isEmpty())
			return flushTo(fd);		/* second half write */
	}
	return 0;
}

/*-----------------------------------------------------------------
 For input buffers, call this periodically to fill the buffer by 
 reading from a nonblocking file descriptor (typically a socket,
 the same one every time).
 (Typically called after ::poll() says fd is ready for reading,
 and is followed by one or more calls to readline().)
 Returns 0 on success, unix error code on failure.
 In particular, returns EPIPE if the peer has disconnected.

 Like put(), except that the data comes from a file descriptor
 rather than the caller's buffer.
-----------------------------------------------------------------*/
int nbbio::fillFrom(int fd)
{
	int nread;
	assert(fd != -1);
	int contigfree = contigBytesFree();
	if (!contigfree)
		return 0;
	nread = read(fd, m_buf+m_putTo, contigfree);
	DPRINT(("nbbio::fillFrom: fd %d, contigfree %d, p='%.*s', read %d\n", fd, contigfree, nread, m_buf+m_putTo, nread));
	if (nread == 0)
		return EPIPE;
	if (nread < 0)
		return errno;
	m_putTo += nread;

	/* If we have wrapped around the end of the buffer, */
	if (m_putTo == nbbio_BUFLEN) {
		m_putTo = 0;
		if (nread == contigfree) {
#if 0
			return fillFrom(fd);		/* simple, but fails if 1st read got it all */
#else
			/* Can't just recurse, as a read() of zero is ok here */
			contigfree = contigBytesFree();
			if (!contigfree)
				return 0;
			nread = read(fd, m_buf, contigfree);
			DPRINT(("nbbio::fillFrom: fd %d, contigfree %d, p='%.*s', read %d\n", fd, contigfree, nread, m_buf, nread));
			if (nread < 0)
				return errno;
			m_putTo = nread;
#endif
		}
	}
	return 0;
}

