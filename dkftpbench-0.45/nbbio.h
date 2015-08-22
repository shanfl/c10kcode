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

#ifndef nbbio_h
#define nbbio_h

#define nbbio_BUFLEN 1024	/* must be power of two */
/**--------------------------------------------------------------------------
 Non-blocking buffered i/o: Trivial FIFO class for holding bytes on their 
 way to or from a nonblocking socket.  
 Also provides a way to read a CRLF terminated line of ASCII text from
 the buffer.
 Kind of like stdio from C, or like a combination of BufferedOutputStream,
 BufferedInputStream, and BufferedReader in Java, except that none of the 
 methods in this class sleep or take more than a few microseconds to execute.

 All I/O is nonblocking to make this class usable inside a 
 many-client-per-thread server. (Nonblocking functions, by definition, always 
 return immediately; they often queue requests to be processed later, or
 return the error code EWOULDBLOCK advising the caller to try again later.)
 These methods are not threadsafe; by design, only one thread at a time
 should call these methods on any one instance of this class.

 Note: this class is usually used either as an input buffer, in which
 case only the methods fillFrom() and readline() are used, or as an output
 buffer, in which case only the methods put() and flushTo() are used. 
 One could argue this class should be split into two to reflect this.

 This implementation of a FIFO circular buffer will be familliar to assembly 
 language programmers who have written interrupt-driven serial I/O programs.
 m_buf holds the bytes to be buffered.
 Bytes are added to the buffer by copying to the offset m_putTo and then 
 updating m_putTo.  
 Bytes are removed from the buffer by copying from the offset m_getFrom
 and then updating m_getFrom.
 The buffer is empty if both indices are equal; it is full if they would
 become equal after putting one more byte into the buffer.

 The buffer is a fixed size; this design does not let it grow or shrink.
 The size of the buffer must be a power of two because I have chosen
 to use "& (BUFLEN-1)" to keep the indices within bounds after each update
 rather than "% BUFLEN" or "if > BUFLEN".  
 These used to be standard practices back on 8-bit CPU's which had no memory
 allocator and were slow at division, and I've grown attached to the idiom.
 Simplicity is its main advantage.
--------------------------------------------------------------------------*/

class nbbio {
	/// The buffer; chars are stored here by put() and fillFrom().
	char m_buf[nbbio_BUFLEN];

	/// Where in m_buf to PULL BYTES FROM  (aka 'head')
	int m_getFrom;

	/// Where in m_buf to APPEND BYTES TO  (aka 'tail')
	int m_putTo;

	/// Current length of caller's line buffer between calls to readline().
	int m_linelen;

	/** The last char parsed.
	 * Lets us accept lines that with CR or LF rather than CR LF.
	 * When a LF is received, the line is processed, and m_lastchar is cleared.
	 * When a CR is received, the line is processed; m_lastchar is set to CR;
	 * and if the next character is LF, it is silently ignored.
	 */
	char m_lastchar;

public:
	/**-----------------------------------------------------------------
	 Constructor just calls init().
	 -----------------------------------------------------------------*/
	nbbio() { init(); }

	/**-----------------------------------------------------------------
	 Init just zeroes things out
	 -----------------------------------------------------------------*/
	void init() { m_putTo=m_getFrom=m_linelen=m_lastchar=0; }

	/**-----------------------------------------------------------------
	 Number of bytes used so far
	 -----------------------------------------------------------------*/
	int bytesUsed() { return ((m_putTo - m_getFrom) & (nbbio_BUFLEN - 1)); }

	/**-----------------------------------------------------------------
	 Number of bytes still available
	 -----------------------------------------------------------------*/
	int bytesFree() { return (nbbio_BUFLEN - 1 - bytesUsed()); }

	/**-----------------------------------------------------------------
	 TRUE if no more bytes can be pulled out 
	 -----------------------------------------------------------------*/
	bool isEmpty() { return (m_putTo == m_getFrom); }

	/**-----------------------------------------------------------------
	 TRUE if no more bytes can be put in 
	 -----------------------------------------------------------------*/
	bool isFull() { return (bytesFree() == 0); }

	/**-----------------------------------------------------------------
	 Call this to put bytes into the buffer.  
	 Returns 0 on success, unix error code on failure.
	 -----------------------------------------------------------------*/
	int put(const char *p, int n);

	/**-----------------------------------------------------------------
	 Call this to read a single char from the buffer.
	 Only call when buffer is not empty.
	 Returns character.
	 -----------------------------------------------------------------*/
	int readc() { 
		int c = m_buf[m_getFrom++];
		m_getFrom &= (nbbio_BUFLEN - 1);
		return c; 
	}

	/**-----------------------------------------------------------------
	 Call this to read a line from the buffer.
	 Expects the line to end in CR LF (but allows them to end in just CR
	 or LF).  Strips the CR LF and null terminates the line during copy.

	 Note: weird interface: If it can't read a whole line, it reads part
	 of the line into linebuf, and returns EWOULDBLOCK, in which case
	 the caller should try again later (after calling fillFrom()).
	 The caller must not touch linebuf until readline returns zero, 
	 as this routine uses linebuf as temporary storage.

	 Returns 0 on success, unix error code on failure.
	 -----------------------------------------------------------------*/
	int readline(char *linebuf, int maxlen);

	/**-----------------------------------------------------------------
	 For output buffers, call this periodically to write part or all 
	 of the buffer to a nonblocking file descriptor (typically a socket,
	 the same one every time).
	 (Typically called after put(), and after ::poll() says fd is ready 
	 for reading.)
	 Returns 0 on success, unix error code on failure.
	 -----------------------------------------------------------------*/
	int flushTo(int fd);

	/**-----------------------------------------------------------------
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
	int fillFrom(int fd);

	/**-----------------------------------------------------------------
	 Number of contiguous bytes used from the m_getFrom to the m_putTo or the
	 wraparound, whichever comes first.  Used to calculate bytes available
	 for linear I/O.  Tells you how many bytes you can pass to write() in a
	 single call when flushing the buffer.
	 Public to make the self-test easier.
	 -----------------------------------------------------------------*/
	int contigBytesUsed() { 
		return (((m_putTo >= m_getFrom) ? m_putTo : nbbio_BUFLEN) - m_getFrom);
	}

	/**-----------------------------------------------------------------
	 Number of contiguous bytes free from the m_putTo to the m_getFrom or the
	 wraparound, whichever comes first.  Used to calculate bytes available
	 for linear I/O.  Tells you how many bytes you can read() in a 
	 single call when filling the buffer.
	 Public to make the self-test easier.
	 -----------------------------------------------------------------*/
	int contigBytesFree() { 
		/* The region we can copy to starts with m_putTo,
		 * and ends before m_getFrom or nbbio_BUFLEN, whichever is higher,
		 * except that we can't use the last byte in the buffer if
		 * it would cause an overflow.
		 */
		return (((m_getFrom > m_putTo) ? m_getFrom-1 : nbbio_BUFLEN) - m_putTo)
				- (!m_getFrom);	// can't use m_buf[BUFLEN-1] if !m_getFrom
	}

};

#endif
