/*
 * opencog/generate/SimpleCallback.cc
 *
 * Copyright (C) 2020 Linas Vepstas <linasvepstas@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License v3 as
 * published by the Free Software Foundation and including the exceptions
 * at http://opencog.org/wiki/Licenses
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program; if not, write to:
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <opencog/atoms/base/Link.h>

#include "SimpleCallback.h"

using namespace opencog;

SimpleCallback::SimpleCallback(AtomSpace* as, const Dictionary& dict)
	: GenerateCallback(as), LinkStyle(as), _dict(dict)
{
	_steps_taken = 0;
	_num_solutions_found = 0;
}

SimpleCallback::~SimpleCallback() {}

void SimpleCallback::clear(void)
{
	while (not _lexlit_stack.empty()) _lexlit_stack.pop();
	while (not _opensel_stack.empty()) _opensel_stack.pop();
	_lexlit.clear();
	_opensel._opensect.clear();
	_opensel._openit.clear();
	_root_sections.clear();
	_root_iters.clear();
	_steps_taken = 0;
	_num_solutions_found = 0;
	CollectStyle::clear();
}

void SimpleCallback::root_set(const HandleSet& roots)
{
	// Might be getting re-used.
	clear();

	for (const Handle& point: roots)
	{
		_root_sections.push_back(_dict.entries(point));
		_root_iters.push_back(_root_sections.back().begin());
	}
}

/// Return the next unexplored set of root sections. This will
/// exhaustively explore all permutations, unless earlier
/// termination criteria are met.
HandleSet SimpleCallback::next_root(void)
{
	static HandleSet empty_set;
	size_t len = _root_iters.size();
	if (len == 0) return empty_set;

	// Stop iterating if limits have been reached.
	if (max_steps < _steps_taken) return empty_set;
	if (max_solutions <= _num_solutions_found) return empty_set;

	// This implements a kind-of odometer. It cycles through each
	// of the sections for each of the starting points. It does
	// this by keeping a sequence of iterators, bumping each
	// iterator in turn, when the previous one rolls over.
	HandleSet starters;
	for (size_t i=0; i<len; i++)
	{
		auto iter = _root_iters[i];
		if (_root_sections[i].end() == iter)
		{
			if (len-1 == i) return empty_set;
			iter = _root_sections[i].begin();
			_root_iters[i] = iter;
			_root_iters[i+1] ++;
		}
		else if (0 == i)
		{
			_root_iters[0]++;
		}
		starters.insert(*iter);
	}
	return starters;
}

/// Return a section containing `to_con`.
/// Pick a new section from the lexis.
Handle SimpleCallback::select_from_lexis(const Frame& frame,
                               const Handle& fm_sect, size_t offset,
                               const Handle& to_con)
{
	const HandleSeq& to_sects = _dict.connectables(to_con);

	// Do we have an iterator (a future/promise) for the to-connector?
	// If not, then set one up. Else use the one we found.  The iterator
	// that we are setting up here will point into the dictionary, i.e.
	// into the pool of allowable sections that we can pick from.
	unsigned curit = _lexlit.get(to_con, 0);
	if (0 == curit)
	{
		// Oh no, dead end!
		if (0 == to_sects.size()) return Handle::UNDEFINED;

		// Start it up.
		_lexlit[to_con] = 1;
		return create_unique_section(to_sects[0]);
	}

	if (to_sects.size() <= curit)
	{
		// We've iterated to the end; we're done.
		_lexlit.erase(to_con);
		return Handle::UNDEFINED;
	}

	// Increment and save.
	_lexlit[to_con] ++;
	return create_unique_section(to_sects[curit]);
}

Handle SimpleCallback::check_self(const HandleSeq& to_sects,
                                  const Handle& fm_sect,
                                  const Handle& to_con,
                                  size_t fit)
{
	// We've iterated to the end; we're done.
	if (to_sects.size() <= fit)
		return Handle::UNDEFINED;

	// Increment and save.
	_opensel._openit[to_con] ++;

	// If we allow self-connections, then return whatever.
	if (allow_self_connections) return to_sects[fit];

	// Make sure we are not self-connecting...
	while (true)
	{
		Handle tosect(to_sects[fit]);
		if (*tosect != *fm_sect) return tosect;
		fit ++;
		_opensel._openit[to_con] = fit;
		if (to_sects.size() <= fit) return Handle::UNDEFINED;
	}
}

/// Return a section containing `to_con`.
/// Try to attach to an existing open section.
Handle SimpleCallback::select_from_open(const Frame& frame,
                               const Handle& fm_sect, size_t offset,
                               const Handle& to_con)
{
	// Do we have an iterator (a future/promise) for the to-connector
	// in the current frame? If so, then return that and increment.
	unsigned fit = _opensel._openit.get(to_con, 0);
	if (0 < fit)
	{
		const HandleSeq& to_sects = _opensel._opensect[to_con];
		return check_self(to_sects, fm_sect, to_con, fit);
	}

	// Set up an iterator, if possible.
	const Handle& linkty = to_con->getOutgoingAtom(0);
	HandleSeq to_sects;
	for (const Handle& open_sect : frame._open_sections)
	{
		const Handle& conseq = open_sect->getOutgoingAtom(1);
		for (const Handle& con : conseq->getOutgoingSet())
		{
			if (*con == *to_con)
			{
				// Wait, are these already connected?
				if (max_pair_links <= num_links(fm_sect, open_sect, linkty))
					continue;
				to_sects.push_back(open_sect);
			}
		}
	}

	// There aren't any open sections ...
	if (0 == to_sects.size()) return Handle::UNDEFINED;

	// Start iterating over the sections that contain to_con.
	_opensel._openit[to_con] = 0;
	return check_self(to_sects, fm_sect, to_con, 0);
}

/// Return a section containing `to_con`.
/// First try to attach to an existing open section.
/// If that fails, then pick a new section from the lexis.
Handle SimpleCallback::select(const Frame& frame,
                              const Handle& fm_sect, size_t offset,
                              const Handle& to_con)
{
	// See if we can find other open connectors to connect to.
	Handle open_sect = select_from_open(frame, fm_sect, offset, to_con);
	if (open_sect) return open_sect;

	// If this is non-empty, the the odometer rolled over.
	if (_opensel._opensect.find(to_con) != _opensel._opensect.end())
		return Handle::UNDEFINED;

	// Select from the dictionary...
	return select_from_lexis(frame, fm_sect, offset, to_con);
}

/// Create an undirected edge connecting the two points `fm_pnt` and
/// `to_pnt`, using the connectors `fm_con` and `to_con`. The edge
/// is "undirected" because a SetLink is used to hold the two
/// end-points. Recall SetLinks are unordered links, so neither point
/// can be identified as head or tail.
Handle SimpleCallback::make_link(const Handle& fm_con,
                                 const Handle& to_con,
                                 const Handle& fm_pnt,
                                 const Handle& to_pnt)
{
	return create_undirected_link(fm_con, to_con, fm_pnt, to_pnt);
}

size_t SimpleCallback::num_links(const Handle& fm_sect,
                                 const Handle& to_sect,
                                 const Handle& link_type)
{
	return num_undirected_links(fm_sect, to_sect, link_type);
}

void SimpleCallback::push_frame(const Frame& frm)
{
	_opensel_stack.push(_opensel);
	_opensel._opensect.clear();
	_opensel._openit.clear();
}

void SimpleCallback::pop_frame(const Frame& frm)
{
	_opensel = _opensel_stack.top(); _opensel_stack.pop();
}

void SimpleCallback::push_odometer(const Odometer& odo)
{
	_lexlit_stack.push(_lexlit);
	_lexlit.clear();
}

void SimpleCallback::pop_odometer(const Odometer& odo)
{
	_lexlit = _lexlit_stack.top(); _lexlit_stack.pop();
}

bool SimpleCallback::step(const Frame& frm)
{
	_steps_taken ++;
	if (max_steps < _steps_taken) return false;
	if (max_solutions <= _num_solutions_found) return false;
	if (max_network_size < frm._linkage.size()) return false;
	if (max_depth < frm._nodo) return false;
	return true;
}

void SimpleCallback::solution(const Frame& frm)
{
	_num_solutions_found++;
	record_solution(frm);
}
