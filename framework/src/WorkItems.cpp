/**********************************************************************
* Copyright (c) 2015 Mark Charlebois
*
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the
* disclaimer below) provided that the following conditions are met:
*
*  * Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
*
*  * Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the
*    documentation and/or other materials provided with the
*    distribution.
*
*  * Neither the name of Dronecode Project nor the names of its
*    contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.
*
* NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
* GRANTED BY THIS LICENSE.  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
* HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*************************************************************************/

#include "DriverFramework.hpp"
#include "SyncObj.hpp"
#include "WorkItems.hpp"

using namespace DriverFramework;

extern bool    g_exit_requested;

bool WorkItems::isValidIndex(unsigned int index)
{
	WorkItems &inst = instance();

	inst.m_lock.lock();
	bool ret = inst._isValidIndex(index);
	inst.m_lock.unlock();
	return ret;
}

bool WorkItems::_isValidIndex(unsigned int index)
{
	return (index < m_work_items.size());
}

void WorkItems::WorkItem::updateStats(unsigned int cur_usec)
{
	unsigned long delay_usec = (m_last == ~0x0UL) ? (cur_usec - m_queue_time) : (cur_usec - m_last);

	if (delay_usec < m_min) {
		m_min = delay_usec;
	}

	if (delay_usec > m_max) {
		m_max = delay_usec;
	}

	m_total += delay_usec;
	m_count += 1;
	m_last = cur_usec;

#if SHOW_STATS == 1

	if ((m_count % 100) == 99) {
		dumpStats();
	}

#endif
}

void WorkItems::WorkItem::resetStats()
{
	m_last = ~(unsigned long)0;
	m_min = ~(unsigned long)0;
	m_max = 0;
	m_total = 0;
	m_count = 0;
}

void WorkItems::WorkItem::dumpStats()
{
	DF_LOG_DEBUG("Stats for callback=%p: count=%lu, avg=%lu min=%lu max=%lu",
		     m_callback, m_count, m_total / m_count, m_min, m_max);
}

void WorkItems::finalize()
{
	WorkItems &inst = instance();

	inst.m_lock.lock();
	inst._finalize();
	inst.m_lock.unlock();
}

void WorkItems::_finalize()
{
	m_work_list.clear();
	m_work_items.clear();
}

void WorkItems::unschedule(unsigned int index)
{
	WorkItems &inst = instance();

	inst.m_lock.lock();
	inst._unschedule(index);
	inst.m_lock.unlock();
}

void WorkItems::_unschedule(unsigned int index)
{
	DFUIntList::Index idx = nullptr;
	idx = m_work_list.next(idx);

	while (idx != nullptr) {
		// If we find it in the list at the current idx, let's go ahead and delete it.
		unsigned cur_index;

		if (m_work_list.get(idx, cur_index)) {

			if (cur_index == index) {
				// remove unscheduled item
				WorkItem *item = nullptr;

				if (!getAt(index, &item)) {
					DF_LOG_ERR("HRTWorkQueue::unscheduleWorkItem - invalid index");

				} else {
					item->m_in_use = false;
					idx = m_work_list.erase(idx);
					// We're only unscheduling one item, so we can bail out here.
					break;
				}
			}
		}

		idx = m_work_list.next(idx);
	}
}

void WorkItems::processExpiredWorkItems(uint64_t &next)
{
	DF_LOG_INFO("WorkItems::processExpiredWorkItems %" PRIu64 "", next);
	WorkItems &inst = instance();

	inst.m_lock.lock();
	inst._processExpiredWorkItems(next);
	inst.m_lock.unlock();
}

void WorkItems::_processExpiredWorkItems(uint64_t &next)
{
	DF_LOG_INFO("WorkItems::processExpiredWorkItems");
	uint64_t elapsed;
	uint64_t now;

	DFUIntList::Index idx = nullptr;
	idx = m_work_list.next(idx);

	while ((!g_exit_requested) && (idx != nullptr)) {
		DF_LOG_INFO("HRTWorkQueue::process work exists");
		unsigned int index;
		m_work_list.get(idx, index);

		if (index < m_work_items.size()) {
			WorkItem *item = nullptr;
			getAt(index, &item);
			now = offsetTime();
			elapsed = now - item->m_queue_time;
			//DF_LOG_DEBUG("now = %lu elapsed = %lu delay = %luusec\n", now, elapsed, item.m_delay_usec);

			if (elapsed >= item->m_delay_usec) {

				DF_LOG_DEBUG("HRTWorkQueue::process do work (%p) (%u)", item, item->m_delay_usec);
				item->updateStats(now);

				// reschedule work
				item->m_queue_time += item->m_delay_usec;
				item->m_in_use = true;

				void *tmpptr = item->m_arg;
				m_lock.unlock();
				item->m_callback(tmpptr);
				m_lock.lock();
			}

			// Get next scheduling time
			uint64_t cur_next = item->m_queue_time + item->m_delay_usec;

			if (cur_next < next) {
				next = cur_next;
			}

			idx = m_work_list.next(idx);
		}
	}
}