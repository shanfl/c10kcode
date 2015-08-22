/*--------------------------------------------------------------------------
 Copyright 1999,2000, Dan Kegel http://www.kegel.com/
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
 Uses a priority queue.  For more info, see 
 http://members.xoom.com/killough/heaps.html or any book on data structures.
 The particular priority queue implementation here is the Heap,
 invented in 1962 by J.W.J. Williams; our implementation is based on
 the code in Chapter 12 of "Programming Pearls" by Jon Bentley.

 The priority queue methods are private, and probably should be in a
 separate class, but they're part of Sked as a historical accident.
 The public methods addClient() and runAll() are implemented in terms
 of the private priority queue methods.
 We have grafted on a kludgy deleteClient() method in a rather horrible
 way: the index in the heap is stored in the client record, and maintained
 when a record is moved in the heap.  A hash table would be better, but hey.
--------------------------------------------------------------------------*/

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <limits.h>		// for MAXINT
#include "Sked.h"
#include "dprint.h"

// Uncomment to enable expensive tests; with these on, we're nowhere near n log n
// Should do this once in a blue moon, but only with very small n...
//#define EXPENSIVE_TESTS
#ifdef EXPENSIVE_TESTS
#define bigassert(x) assert(x)
#else
#define bigassert(x)
#endif

//#define VERBOSE

#ifdef VERBOSE 
#define VDPRINT(x) DPRINT(x)
#else
#define VDPRINT(x)
#endif

/*----------------------------------------------------------------------
 Helper function to store a SkedRecord at a particular index in the
 heap array.
 This will make more sense when we implement our planned delClient kludge.
 The odd 'do { ... } while (0)' trick is just there so ... is treated
 as a single C statement, even if it contains several statements.
 This *is* currently faster than an inline function, who knows why.

 Warning: don't call with arguments that have side effects!
----------------------------------------------------------------------*/
#define storeAt(i, r) do { m_records[(i)] = (r); (r).skc->skedIndex = i; VDPRINT(("storeAt(%d, %p)\n", i, (r).skc)); } while (0)

/* The private methods */

#ifdef EXPENSIVE_TESTS
/*----------------------------------------------------------------------
 Debugging function.
 Return true if m_records[lb...ub] has the heap property, i.e. 
 if for all i from lb to ub inclusive,
	 m_records[i/2].when <= m_records[i].when
----------------------------------------------------------------------*/
bool Sked::isHeap(int lb, int ub)
{
	int i;

	//DPRINT(("Sked::isHeap(%d, %d)\n", lb, ub));
	assert((lb >= 1) && (lb <= m_used));
	assert((ub >= 1) && (ub <= m_used));

	for (i=lb; i<(2*lb) && i<ub; i++) {
		assert(m_records[i].skc);
		if (m_records[i].skc->skedIndex != i) {
			DPRINT(("Sked::isHeap(%d,%d): m_records[%d].skc->skedIndex = %d!\n",
				lb, ub, i, m_records[i].skc->skedIndex));
		}
		assert(m_records[i].skc->skedIndex == i);
	}
	for (i=(2*lb); i<=ub; i++) {
		if (eclock_after(m_records[i/2].when, m_records[i].when)) {
			DPRINT(("Sked::isHeap(%d, %d): %d.when > %d.when! \n", lb, ub, i/2, i));
			return false;
		}
		assert(m_records[i].skc);
		if (i != m_records[i].skc->skedIndex) {
			DPRINT(("Sked::isHeap: %d: index doesn't match one in record, %d\n", i, m_records[i].skc->skedIndex));
			return false;
		}
	}
	return true;
}
#endif

/*----------------------------------------------------------------------
 Sift up a heap element towards the root until it's in an ok place.
 On entry, the heap is in good shape except perhaps for the element at 
 the given index is in the wrong place, i.e. its value might be less
 than the value of its parent.
 On exit, the heap is rearranged so it is in good shape everywhere.
----------------------------------------------------------------------*/
void Sked::siftUp(int index)
{
	/* "sift up" the new record to its correct position in the heap */ 
	while (index > 1) {
		/* Invariant: Heap(1,size()) except perhaps between index 
		 * and its parent 
		 */
		int parent = index / 2;
		if (eclock_after(m_records[index].when, m_records[parent].when))
			break;

		SkedRecord tmp = m_records[parent];
		storeAt(parent, m_records[index]);
		storeAt(index, tmp);

		index = parent;
	}

	/* Postcondition */
	bigassert(isHeap(1, m_used));
}

/*----------------------------------------------------------------------
 Sift down a heap element until it's in an ok place.
 On entry, the heap is in good shape except perhaps for the element at 
 the given index is in the wrong place, i.e. its value might be greater 
 than the values of its children.
 On exit, the heap is rearranged so it is in good shape everywhere.
----------------------------------------------------------------------*/
void Sked::siftDown(int index)
{
	VDPRINT(("Sked::siftDown(%d)\n", index));

	/* "sift down" the element to its correct position in the heap */
	while (true) {
		/* Invariant: Heap(1,m_used) except perhaps between index 
		 * and its (0, 1 or 2) children
		 */
		int child = index * 2;
		if (child > m_used)
			break;
		/* child is the left child of index */
		if (child < m_used) {
			/* child+1 is the right child of index */
			if (!eclock_after(m_records[child+1].when, m_records[child].when))
				child++;
		}
		assert(child <= m_used);
		/* child is the least child of index */
		if (eclock_after(m_records[child].when, m_records[index].when))
			break;
		/* swap child and index */
		VDPRINT(("Sked::siftDown: %d.when > %d.when, so swapping\n", index, child));
		SkedRecord tmp = m_records[child];
		storeAt(child, m_records[index]);
		storeAt(index, tmp);
		index = child;
	}

	/* Postcondition */
	bigassert(isHeap(1, m_used));
	
	VDPRINT(("Sked::siftDown: done\n"));
}

/*----------------------------------------------------------------------
 Remove the given element from the middle of the heap.
 On entry, the heap is in good shape.
 On exit, the heap is in good shape, and the given element is gone.
----------------------------------------------------------------------*/
void Sked::remove(int index)
{
	VDPRINT(("Sked::remove(%d): m_used %d\n", index, m_used));
	/* Precondition */
	bigassert(isHeap(1, m_used));
	assert(m_used >= 0);
	assert(index <= m_used);

	if (m_used-- > 1) {
		/* "sift up" the given record to the top, preserving heap property */ 
		while (index > 1) {
			int parent = index / 2;

			VDPRINT(("Sked::remove: swapping %d and %d\n", index, parent));
			SkedRecord tmp = m_records[parent];
			storeAt(parent, m_records[index]);
			storeAt(index, tmp);

			index = parent;
		}

		/* Move the last element to the head of the heap, overwriting the
		 * element we just moved there.
		 */
		storeAt(1, m_records[m_used+1]);

		/* "sift down" the new top event to its correct position in the heap */
		if (m_used > 1) {
			bigassert(isHeap(2, m_used));
			siftDown(1);
		}
		/* Postcondition */
		bigassert(isHeap(1, m_used));
	}
	VDPRINT(("Sked::remove: done, m_used %d\n", m_used));
}

/*----------------------------------------------------------------------
 Adds a scheduled event to the priority queue, allocating additional
 memory if necessary.
 Copies the given record, does not save the skr pointer.
 Returns 0 on success, or Unix error code on failure.
----------------------------------------------------------------------*/
int Sked::push(SkedRecord *skr)
{
	assert(skr->skc);
	VDPRINT(("Sked::push: size %d\n", size()));

	/* Resize array if needed */
	if (m_used+1 == m_allocated) {
		int new_alloc = m_allocated * 2;
		int new_size = new_alloc * sizeof(SkedRecord);
		DPRINT(("Sked::push: Reallocating %d bytes.\n", new_size));
		SkedRecord *tmp = (SkedRecord*)realloc(m_records, new_size);
		if (!tmp)
			return ENOMEM;
		m_allocated = new_alloc;
		m_records = tmp;
		DPRINT(("Sked::push: setting m_allocated to %d\n", m_allocated));
	}

	/* Precondition */
	if (m_used > 0)
		bigassert(isHeap(1, m_used));

	/* Add the new record */
	m_used = m_used + 1;
	storeAt(m_used, *skr);

	/* "sift up" the new record to its correct position in the heap */ 
	siftUp(m_used);

	/* Postcondition */
	bigassert(isHeap(1, m_used));
	VDPRINT(("Sked::push: done, size now %d\n", size()));
	return 0;
}

/*----------------------------------------------------------------------
 Removes the first scheduled event in the priority queue and brings the
 second scheduled event to the top.
----------------------------------------------------------------------*/
void Sked::pop()
{
	VDPRINT(("Sked::pop: size %d\n", size()));
	if (empty())
		return;

	/* Precondition */
	bigassert(isHeap(1, m_used));

	/* Tell the element at the head of the heap he's being removed */
	assert(m_records[1].skc);
	assert(m_records[1].skc->skedIndex == 1);
	//DPRINT(("Sked::pop: clearing skc %p ->skedIndex\n", m_records[1].skc));
	m_records[1].skc->skedIndex = 0;
	m_used--;
	assert(size() >= 0);
	
	if (size() > 0) {
		/* Move the old last element to the head of the heap */
		storeAt(1, m_records[m_used+1]);
		if (size() > 1) {
			/* "sift down" the new top to its correct position in the heap */
			bigassert(isHeap(2, m_used));
			siftDown(1);
			bigassert(isHeap(1, m_used));
		}
		assert(m_records[1].skc->skedIndex == 1);
	}
	VDPRINT(("Sked::pop: done, size now %d\n", size()));
}

/* The public methods */

/*----------------------------------------------------------------------
 Allocates memory for the priority queue.
 Returns 0 on success, or Unix error code on failure.
----------------------------------------------------------------------*/
int Sked::init()
{
	m_allocated = 256;
	int nbytes = m_allocated * sizeof(SkedRecord);
	DPRINT(("Sked::init: Allocating %d bytes.\n", nbytes));
	m_records = (SkedRecord*)malloc(nbytes);
	if (!m_records) {
		m_allocated = m_used = 0;
		return ENOMEM;
	}
	m_used = 0;
	return 0;
}

/*----------------------------------------------------------------------
 Schedule an event for the future.
 Returns 0 on success, or Unix error code on failure.
----------------------------------------------------------------------*/
int Sked::addClient(SkedClient *skc, clock_t when)
{
	DPRINT(("Sked::addClient(%p, %d)\n", skc, when));
	assert(skc);
	assert(!skc->skedIndex);
	SkedRecord skr(skc, when);
	assert(skr.skc);
	return push(&skr);
}

/*----------------------------------------------------------------------
 Delete the given client from the queue of future events.
 Returns 0 on success, or Unix error code on failure.
----------------------------------------------------------------------*/
int Sked::delClient(SkedClient *skc)
{
	DPRINT(("Sked::delClient: index %d, m_used %d\n", skc->skedIndex, m_used));
	if (skc->skedIndex == 0)
		return 0;		/* not scheduled */

	assert(m_used > 0);

	/* Remove the element from the heap */
	remove(skc->skedIndex);

	/* Mark the client as not being in the heap */
	skc->skedIndex = 0;

	return 0;
}

/*----------------------------------------------------------------------
 Run events whose time has come.
 i.e. for each client registered with addClient with a 'when' before 'now',
 calls the client's skedCallback() method.
 Returns 0 on success, Unix error code on failure.
----------------------------------------------------------------------*/
int Sked::runAll(clock_t now)
{
	do {
		const SkedRecord *p;
		p = top();

		/* If there are no more nodes ready to execute, give up happily */
		//DPRINT(("Sked::runAll: p %p, p->when - now %d\n", p, (p ? p->when : now) - now));
		if (!p || eclock_after(p->when, now))
			return 0; 

		//DPRINT(("Sked::runAll: p %p, skc %p, index %d\n", p, p->skc, p->skc->skedIndex));
		if ((p->skc->skedIndex < 1) || (p->skc->skedIndex > m_used) || (m_records[p->skc->skedIndex].skc != p->skc) ) {
			DPRINT(("Sked::runAll: invalid index %d or pointer doesn't match\n", p->skc->skedIndex));
			return EINVAL;
		}

		/* Note: the callback may try to remove itself from the queue
		 * or add itself back in to the queue, so we have to pop it
		 * before we call its callback.
		 */
		SkedClient *skc = p->skc;
		//DPRINT(("Sked::runAll: before pop; skc %p, skc->skedIndex %d\n", skc, skc->skedIndex));
		assert(skc->skedIndex == 1);
		pop();
		//DPRINT(("Sked::runAll: after pop; skc %p, skc->skedIndex %d\n", skc, skc->skedIndex));
		assert(!skc->skedIndex);

		//DPRINT(("Sked::runAll: Calling callback; skc %p\n", skc));
		skc->skedCallback(now);
	} while (true);
	return 0;
}

