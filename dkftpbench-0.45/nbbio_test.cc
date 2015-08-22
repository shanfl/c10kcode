#include "nbbio.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>     
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define NVECTORS 3

void check(int d, int e, int line)
{
	if (d != e) {
		printf("check: %d != %d at line %d\n", d, e, line);
		exit(1);
	}
}
#define CHECK(d, e) check(d, e, __LINE__)

static void dumpBuf(const void *p, int len)
{
	int i;
	for (i=0; i<len; i++) {
		printf("%02d ", 0xff & ((unsigned const char *)p)[i]);
		if ((i & 31) == 31) 
			puts("");
	}
	puts("");
}

int main()
{
	nbbio nb;
	int err;
	char buf[nbbio_BUFLEN];
	char buf2[nbbio_BUFLEN];
	int i;
	int bytesused;
	int len;
	int c;

	/* Sanity checks */
	CHECK( nb.bytesUsed(), 0 );
	CHECK( nb.contigBytesUsed(), 0 );
	CHECK( nb.bytesFree(), nbbio_BUFLEN-1 );
	CHECK( nb.contigBytesFree(), nbbio_BUFLEN-1 );
	CHECK( nb.isEmpty(), TRUE );
	CHECK( nb.isFull(), FALSE );

	/* Fill a buffer with three lines, piecemeal */
	const char *vectors[NVECTORS] = { "Hi", "there", "cutie" };
	bytesused=0;
	for (i=0; i < NVECTORS; i++) {
		err = nb.put(vectors[i], 1);
		bytesused++;
		if (err) {
			printf("Failed at line %d: %s\n", __LINE__, strerror(err));
			exit(1);
		}
		err = nb.put(vectors[i]+1, strlen(vectors[i])-1);
		bytesused+=strlen(vectors[i])-1;
		if (err) {
			printf("Failed at line %d\n", __LINE__);
			exit(1);
		}
		if (i & 1) {
			err = nb.put("\r\n", 2);
			bytesused+=2;
		} else {
			err = nb.put("\n", 1);
			bytesused++;
		}
		if (err) {
			printf("Failed at line %d\n", __LINE__);
			exit(1);
		}
	}

	/* Sanity checks */
	CHECK( nb.bytesUsed(), bytesused );
	CHECK( nb.contigBytesUsed(), bytesused );	/* assume no wrap yet */
	CHECK( nb.bytesFree(), nbbio_BUFLEN-1 - bytesused );
	CHECK( nb.contigBytesFree(), nbbio_BUFLEN-1 - bytesused );
	CHECK( nb.isEmpty(), FALSE );
	CHECK( nb.isFull(), FALSE );

	/* make sure we can get them out */
	for (i=0; i < NVECTORS; i++) {
		err = nb.readline(buf, sizeof(buf));
		if (err) {
			printf("Failed at line %d; can't read line %d\n", __LINE__, i);
			exit(1);
		}
		if (strcmp(buf, vectors[i])) {
			printf("Bad data; failed at line %d\n", __LINE__);
			exit(1);
		}
	}

	/* Sanity checks */
	CHECK( nb.bytesUsed(), 0 );
	CHECK( nb.contigBytesUsed(), 0 );
	CHECK( nb.bytesFree(), nbbio_BUFLEN-1 );
	CHECK( nb.contigBytesFree(), nbbio_BUFLEN - bytesused );
	CHECK( nb.isEmpty(), TRUE );	/* assume last line ended in \n only */
	CHECK( nb.isFull(), FALSE );

	/* Fill it to the brim with one long line */
	for (i=0; i<nbbio_BUFLEN-1; i++)
		buf2[i] = 32 + (i & 63);
	err = nb.put(buf2, nbbio_BUFLEN-1);
	if (err) {
		printf("Failed at line %d; can't put big line\n", __LINE__);
		exit(1);
	}

	/* Sanity checks */
	CHECK( nb.bytesUsed(), nbbio_BUFLEN-1 );
	CHECK( nb.contigBytesUsed(), nbbio_BUFLEN-bytesused );	/* -old m_putTo */
	CHECK( nb.bytesFree(), 0 );
	CHECK( nb.contigBytesFree(), 0 );
	CHECK( nb.isEmpty(), FALSE );
	CHECK( nb.isFull(), TRUE );

	for (i=0; i<nbbio_BUFLEN-1; i++) {
		if (nb.isEmpty()) {
			printf("Failed at line %d\n", __LINE__);
			exit(1);
		}
		c = nb.readc();
		int wanted = (32 + (i & 63));
		if (c != wanted) {
			printf("Failed at line %d: bad value; i = %d, wanted %d, got %d\n", __LINE__, i, wanted, c);
			exit(1);
		}
	}

	/* Sanity checks */
	CHECK( nb.bytesUsed(), 0 );
	CHECK( nb.contigBytesUsed(), 0 );
	CHECK( nb.bytesFree(), nbbio_BUFLEN-1 );
	CHECK( nb.contigBytesFree(), nbbio_BUFLEN-bytesused+1 );
	CHECK( nb.isEmpty(), TRUE );
	CHECK( nb.isFull(), FALSE );

	/* File I/O */
	int wfd = open("nbbiotest.dat", O_CREAT|O_TRUNC|O_RDWR, 0664);
	if (wfd < 0) {
		printf("Failed at line %d; can't create nbbiotest.dat\n", __LINE__);
		perror("open");
		exit(1);
	}
	bytesused=0;
	for (i=0; i < NVECTORS; i++) {
		err = nb.put(vectors[i], 1);
		bytesused++;
		if (err) {
			printf("Failed at line %d: %s\n", __LINE__, strerror(err));
			exit(1);
		}
		err = nb.put(vectors[i]+1, strlen(vectors[i])-1);
		bytesused+=strlen(vectors[i])-1;
		if (err) {
			printf("Failed at line %d\n", __LINE__);
			exit(1);
		}
		if (i & 1) {
			err = nb.put("\r\n", 2);
			bytesused+=2;
		} else {
			err = nb.put("\n", 1);
			bytesused++;
		}
		if (err) {
			printf("Failed at line %d\n", __LINE__);
			exit(1);
		}
	}
	err = nb.flushTo(wfd);
	if (err) {
		printf("Failed at line %d\n", __LINE__);
		exit(1);
	}
	close(wfd);

	/* Sanity checks */
	CHECK( nb.bytesUsed(), 0 );
	CHECK( nb.contigBytesUsed(), 0 );
	CHECK( nb.bytesFree(), nbbio_BUFLEN-1 );
	CHECK( nb.contigBytesFree(), nbbio_BUFLEN - 2 * bytesused + 1);
	CHECK( nb.isEmpty(), TRUE );
	CHECK( nb.isFull(), FALSE );

	int rfd = open("nbbiotest.dat", O_RDONLY);
	if (rfd < 0) {
		printf("Failed at line %d; can't open nbbiotest.dat\n", __LINE__);
		perror("open");
		exit(1);
	}
	err = nb.fillFrom(rfd);
	if (err) {
		printf("Failed at line %d\n", __LINE__);
		exit(1);
	}

	/* make sure we can get them out */
	for (i=0; i < NVECTORS; i++) {
		err = nb.readline(buf, sizeof(buf));
		if (err) {
			printf("Failed at line %d; can't read line %d\n", __LINE__, i);
			exit(1);
		}
		if (strcmp(buf, vectors[i])) {
			printf("Bad data; failed at line %d\n", __LINE__);
			exit(1);
		}
	}

	/* Check "fillFrom when # of bytes readable == # of contig bytes left",
	 * which could cause a premature EOF due to 2nd read() returning 0
	 * Thanks to Tom Emersoni <TOMEMERSON at ms.globalpay.com> for finding that one
	 */

	/* 1. Create a file with a single line of C's, len nbbio_BUFLEN/2 (inc CRLF) */
	wfd = open("nbbiotest.dat", O_CREAT|O_TRUNC|O_RDWR, 0664);
	if (wfd < 0) {
		printf("Failed at line %d; can't create nbbiotest.dat\n", __LINE__);
		perror("open");
		exit(1);
	}
	memset(buf, 'C', nbbio_BUFLEN/2 - 2);
	buf[nbbio_BUFLEN/2 - 2] = '\r';
	buf[nbbio_BUFLEN/2 - 1] = '\n';
	int nwrite = write(wfd, buf, nbbio_BUFLEN/2);
	CHECK(nbbio_BUFLEN/2, nwrite);
	err = close(wfd);
	CHECK(err, 0);

	/* 2. Rotate buf so there's two bytes of free space at the beginning,
	 * then fill buf with a line of B's until there's exactly nbbio_BUFLEN/2 contigFree 
	 */
	nb.init();
	CHECK(nb.contigBytesFree(), nbbio_BUFLEN-1);
	CHECK(nb.bytesFree(), nbbio_BUFLEN-1);
	nb.put("AZ", 2);
	c = nb.readc();
	CHECK(c, 'A');
	c = nb.readc();
	CHECK(c, 'Z');
	CHECK(nb.contigBytesFree(), nbbio_BUFLEN-2);	/* because putTo is 2 */
	CHECK(nb.bytesFree(), nbbio_BUFLEN-1);
	while (nb.contigBytesFree() > nbbio_BUFLEN/2 + 2) {
		nb.put("B", 1);
		CHECK(nb.contigBytesFree(), nb.bytesFree()-1);
	}
	nb.put("\r\n", 2);
	CHECK(nb.contigBytesFree(), nbbio_BUFLEN/2);
	CHECK(nb.bytesFree(), nbbio_BUFLEN/2+1);

	/* 3. Fill buf from the file.  Make sure it doesn't barf because
	 * we hit EOF.
	 */
	rfd = open("nbbiotest.dat", O_RDONLY);
	if (rfd < 0) {
		printf("Failed at line %d; can't open nbbiotest.dat\n", __LINE__);
		perror("open");
		exit(1);
	}
	err = nb.fillFrom(rfd);
	CHECK(err, 0);
	close(rfd);

	/* 4. Make sure we can read both lines. */
	memset(buf2, 255, sizeof(buf2));
	err = nb.readline(buf2, sizeof(buf2));
	CHECK(err, 0);
	len = strlen(buf2);
	CHECK(len, nbbio_BUFLEN/2 - 2 - 2);
	CHECK(buf2[0], 'B');	/* first line all B's */

	err = nb.readline(buf2, sizeof(buf2));
	CHECK(err, 0);
	CHECK(buf2[0], 'C');	/* 2nd line all C's */
	err = memcmp(buf, buf2, nbbio_BUFLEN/2 - 2);
	if (err) {
		printf("got:");
		dumpBuf(buf2, nbbio_BUFLEN/2-2);
		printf("wanted:");
		dumpBuf(buf, nbbio_BUFLEN/2-2);
	}
	CHECK(err, 0);
	len = strlen(buf2);
	if (len != nbbio_BUFLEN/2 - 2) {
		printf("got:");
		dumpBuf(buf2, len);
		printf("wanted:");
		dumpBuf(buf, nbbio_BUFLEN/2-2);
	}
	CHECK(len, nbbio_BUFLEN/2 - 2);

	/* Whew. */
	unlink("nbbiotest.dat");

	printf("Test passed\n");
	exit(0);
}
