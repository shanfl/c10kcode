/*--------------------------------------------------------------------------
 Copyright 1999, Dan Kegel http://www.kegel.com/
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
 Module to schedule events in the future.
 Takes little time to add or delete an event even when tens of thousands 
 of events are scheduled, i.e. adding or deleting an event takes CPU 
 time proportional to the log of the number of already-scheduled events.
 Stores scheduled events in a priority queue.  For more info, see 
 http://members.xoom.com/killough/heaps.html or any book on data structures.
 The particular priority queue implementation here is the Heap,
 invented in 1962 by J.W.J. Williams; our implementation is based on
 the code in Chapter 12 of "Programming Pearls" by Jon Bentley.
 Bentley didn't provide a delete(), so the one here is my own attempt;
 there may be a better way to do it.

 The priority queue methods are private, and probably should be in a
 separate class, but they're part of Sked as a historical accident.
 The public methods addClient() and runAll() are implemented in terms
 of the private priority queue methods.
 The SkedClient interface is really wierd in that it has a data element,
 which is used by this module to remember where in the heap the client
 is.  There's probably a better way.
--------------------------------------------------------------------------*/

#ifndef Sked_h
#define Sked_h

#include "eclock.h"

/*----------------------------------------------------------------------
 Interface that must be implemented by user code that wishes to be
 scheduled.
----------------------------------------------------------------------*/
class SkedClient {
public:
	/*----------------------------------------------------------------------
	 User-supplied function.
	 When the specified time has elapsed, Sked calls this method.
	----------------------------------------------------------------------*/
	virtual void skedCallback(clock_t now) = 0;

	SkedClient() { skedIndex = 0; }

	/* A data element used to track where this client is in the heap. */
	int skedIndex;		// 0 if not in heap yet, else index into m_records
};

/*--------------------------------------------------------------------------
 The scheduler itself.
--------------------------------------------------------------------------*/
class Sked {
public:
	struct SkedRecord {
		SkedRecord(SkedClient *c, clock_t w) { skc=c; when=w; }
		SkedClient *skc;
		clock_t when;
	};

private:
	/* m_records is the array holding the implicit tree heap.
	 * Note: m_records[0] is never used.  We use m_records as a 1-based array.
	 */
	SkedRecord *m_records;
	int m_allocated;       // allocated size, including 0th element

	/* m_used is the highest index in use.
	 * It's also the number of events queued.
	 * It starts out at zero.  When the first event is queued,
	 * it's stored at m_records[1], and m_used is incremented to 1.
	 */
	int m_used;            // highest index in use

	/*======================================================================
	 Private methods for managing the priority queue. The priority
	 queue is implemented as a binary heap. The functions isHeap(),
	 push() and pop() are based upon the algorithms isHeap(), siftUp()
	 and siftDown() in Chapter 12 of "Programming Pearls" by Jon Bentley.
	 Note that m_records[1] is the top of the queue and m_records[0]
	 is not used.
	======================================================================*/

public:	/* Only public for self-test */
	/*----------------------------------------------------------------------
	 Debugging function.
	 Return true if m_records[lb...ub] has the heap property, i.e. 
	 if for all i from lb to ub inclusive,
		 m_records[i/2].when <= m_records[i].when
	----------------------------------------------------------------------*/
	bool isHeap(int lb, int ub);

	/*----------------------------------------------------------------------
	 Returns the actual number of scheduled events in the queue.
	----------------------------------------------------------------------*/
	int size() { return m_used; }

	/*----------------------------------------------------------------------
	 Returns the first scheduled event in the queue, or NULL if the
	 queue is empty.
	----------------------------------------------------------------------*/
	const SkedRecord *top() { return empty() ? NULL : m_records+1; }

	/*----------------------------------------------------------------------
	 Sift up a heap element towards the root until it's in an ok place.
	 On entry, the heap is in good shape except perhaps for the element at 
	 the given index is in the wrong place, i.e. its value might be less
	 than the value of its parent.
	 On exit, the heap is rearranged so it is in good shape everywhere.
	----------------------------------------------------------------------*/
	void siftUp(int index);

	/*----------------------------------------------------------------------
	 Sift down a heap element until it's in an ok place.
	 On entry, the heap is in good shape except perhaps for the element at 
	 the given index is in the wrong place, i.e. its value might be greater 
	 than the values of its children.
	 On exit, the heap is rearranged so it is in good shape everywhere.
	----------------------------------------------------------------------*/
	void siftDown(int index);

	/*----------------------------------------------------------------------
	 Remove the given element from the middle of the heap.
	 On entry, the heap is in good shape.
	 On exit, the heap is in good shape, and the given element is gone.
	----------------------------------------------------------------------*/
	void remove(int index);

	/*----------------------------------------------------------------------
	 Adds a scheduled event to the priority queue, allocating additional
	 memory if necessary.
	 Copies the given record, does not save the skr pointer.
	 Returns 0 on success, or Unix error code on failure.
	----------------------------------------------------------------------*/
	int push(SkedRecord *skr);

	/*----------------------------------------------------------------------
	 Removes the first scheduled event in the priority queue and brings the
	 second scheduled event to the top.
	----------------------------------------------------------------------*/
	void pop();

public:
	/*----------------------------------------------------------------------
	 Constructor.
	----------------------------------------------------------------------*/
	Sked() { m_used = 0;  m_records = NULL; }

	/*----------------------------------------------------------------------
	 Allocates memory for the priority queue.
	 Returns 0 on success, or Unix error code on failure.
	----------------------------------------------------------------------*/
	int init();

	/*----------------------------------------------------------------------
	 Returns true if there are no scheduled events in the queue. 
	----------------------------------------------------------------------*/
	bool empty() { return size() == 0; }

	/*----------------------------------------------------------------------
	 Schedule an event for the future.
	 Only one event may be outstanding for any one SkedClient.
	 Returns 0 on success, or Unix error code on failure.
	----------------------------------------------------------------------*/
	int addClient(SkedClient *skc, clock_t when);

	/*----------------------------------------------------------------------
	 Delete the given client from the queue of future events.
	 Returns 0 on success, or Unix error code on failure.
	----------------------------------------------------------------------*/
	int delClient(SkedClient *skc);

	/*----------------------------------------------------------------------
	 Run events whose time has come.
	 i.e. for each client registered with addClient with a 'when' before 'now',
	 calls the client's skedCallback() method.
	 Returns 0 on success, Unix error code on failure.
	----------------------------------------------------------------------*/
	int runAll(clock_t now);

	/*----------------------------------------------------------------------
	 Return time of next event, or the given default if no event scheduled.
	----------------------------------------------------------------------*/
	clock_t nextTime(clock_t defval) { return empty() ? defval : top()->when; }
};

#endif
