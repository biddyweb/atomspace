/*
 * opencog/atoms/execution/ExecutionOutputLink.cc
 *
 * Copyright (C) 2009, 2013, 2015 Linas Vepstas
 * All Rights Reserved
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

#include <dlfcn.h>
#include <stdlib.h>

#include <opencog/atoms/base/atom_types.h>
#include <opencog/atomspace/AtomSpace.h>
#include <opencog/atoms/core/DefineLink.h>
#include <opencog/cython/PythonEval.h>
#include <opencog/guile/SchemeEval.h>

#include "ExecutionOutputLink.h"
#include "Instantiator.h"

using namespace opencog;

ExecutionOutputLink::ExecutionOutputLink(const HandleSeq& oset,
                                         TruthValuePtr tv,
                                         AttentionValuePtr av)
	: FunctionLink(EXECUTION_OUTPUT_LINK, oset, tv, av)
{
	if (2 != oset.size())
		throw SyntaxException(TRACE_INFO,
			"ExecutionOutputLink must have schema and args! Got arity=%d",
			oset.size());

	if (DEFINED_SCHEMA_NODE != oset[0]->getType() and
	    LAMBDA_LINK != oset[0]->getType() and
	    GROUNDED_SCHEMA_NODE != oset[0]->getType())
	{
		throw SyntaxException(TRACE_INFO,
			"ExecutionOutputLink must have schema! Got %s",
			oset[0]->toString().c_str());
	}

	if (LIST_LINK != oset[1]->getType())
		throw SyntaxException(TRACE_INFO,
			"ExecutionOutputLink must have args! Got %s",
			oset[1]->toString().c_str());
}

ExecutionOutputLink::ExecutionOutputLink(const Handle& schema,
                                         const Handle& args,
                                         TruthValuePtr tv,
                                         AttentionValuePtr av)
	: FunctionLink(EXECUTION_OUTPUT_LINK, schema, args, tv, av)
{
	Type stype = schema->getType();
	if (GROUNDED_SCHEMA_NODE != stype and
	    LAMBDA_LINK != stype and
	    DEFINED_SCHEMA_NODE != stype)
	{
		throw SyntaxException(TRACE_INFO,
			"ExecutionOutputLink expecting schema, got %s",
			schema->toString().c_str());
	}

	if (LIST_LINK != args->getType())
		throw SyntaxException(TRACE_INFO,
			"ExecutionOutputLink expecting args, got %s",
			args->toString().c_str());
}

ExecutionOutputLink::ExecutionOutputLink(Link& l)
	: FunctionLink(l)
{
	Type tscope = l.getType();
	if (EXECUTION_OUTPUT_LINK != tscope)
		throw SyntaxException(TRACE_INFO,
			"Expection an ExecutionOutputLink!");
}

/// execute -- execute the function defined in an ExecutionOutputLink
///
/// Each ExecutionOutputLink should have the form:
///
///     ExecutionOutputLink
///         GroundedSchemaNode "lang: func_name"
///         ListLink
///             SomeAtom
///             OtherAtom
///
/// The "lang:" should be either "scm:" for scheme, or "py:" for python.
/// This method will then invoke "func_name" on the provided ListLink
/// of arguments to the function.
///
Handle ExecutionOutputLink::execute(AtomSpace* as) const
{
	return do_execute(as, _outgoing[0], _outgoing[1]);
}

/// do_execute -- execute the SchemaNode of the ExecutionOutputLink
///
/// Expects "gsn" to be a GroundedSchemaNode or a DefinedSchemaNode
/// Expects "args" to be a ListLink
/// Executes the GroundedSchemaNode, supplying the args as argument
///
Handle ExecutionOutputLink::do_execute(AtomSpace* as,
                         const Handle& gsn, const Handle& cargs)
{
	LAZY_LOG_FINE << "Execute gsn: " << gsn->toShortString()
	              << "with arguments: " << cargs->toShortString();

	// Search for additional execution links, and execute them too.
	// We will know that happend if the returned handle differs from
	// the input handle. If the results are different, add the new
	// results to the atomspace. We need to do this, because scheme,
	// and python expects to find thier arguments in the atomspace,
	// but this is arguably broken, as it pollutes the atomspace with
	// junk that is never cleaned up.  We punt for now, but something
	// should be done about this. XXX FIXME ...
	Instantiator inst(as);
	Handle args(cargs);
	if (cargs->isLink())
	{
		std::vector<Handle> new_oset;
		bool changed = false;
		for (Handle ho : cargs->getOutgoingSet())
		{
			Handle nh(inst.execute(ho));
			// nh might be NULL if ho was a DeleteLink
			if (nullptr == nh)
			{
				changed = true;
				continue;
			}

			// Unwrap the top-most DontExecLink's.  Lower ones are left
			// untouched.  We do this as a sop for issue opencog/atomspace#704
			// but maybe we should not?
			if (DONT_EXEC_LINK == nh->getType())
			{
				nh = nh->getOutgoingAtom(0);
			}
			new_oset.emplace_back(nh);

			if (nh != ho) changed = true;
		}
		if (changed)
			args = as->add_link(LIST_LINK, new_oset);
	}

	// Get the schema name.
	const std::string& schema = NodeCast(gsn)->getName();
	// printf ("Grounded schema name: %s\n", schema.c_str());

	// At this point, we only run scheme, python schemas and functions from
	// libraries loaded at runtime.
	if (0 == schema.compare(0,4,"scm:", 4))
	{
#ifdef HAVE_GUILE
		// Be friendly, and strip leading white-space, if any.
		size_t pos = 4;
		while (' ' == schema[pos]) pos++;

		SchemeEval* applier = SchemeEval::get_evaluator(as);
		Handle h(applier->apply(schema.substr(pos), args));

		// Exceptions were already caught, before leaving guile mode,
		// so we can't rethrow.  Just throw a new exception.
		if (applier->eval_error())
			throw RuntimeException(TRACE_INFO,
			    "Failed evaluation; see logfile for stack trace.");
		return h;
#else
		throw RuntimeException(TRACE_INFO,
		    "Cannot evaluate scheme GroundedSchemaNode!");
#endif /* HAVE_GUILE */
	}

	if (0 == schema.compare(0, 3,"py:", 3))
	{
#ifdef HAVE_CYTHON
		// Be friendly, and strip leading white-space, if any.
		size_t pos = 3;
		while (' ' == schema[pos]) pos++;

		// Get a reference to the python evaluator. 
		// Be sure to specify the atomspace in which the
		// evaluation is to be performed.
		PythonEval &applier = PythonEval::instance();
		Handle h = applier.apply(as, schema.substr(pos), args);

		// Return the handle
		return h;
#else
		throw RuntimeException(TRACE_INFO,
		    "Cannot evaluate python GroundedSchemaNode!");
#endif /* HAVE_CYTHON */
	}

	// This is used by the Haskel bindings.  XXX -- it uses a broken
	// API, namely the UUID's, and should be fixed....
	if (0 == schema.compare(0, 4, "lib:", 4))
	{
		// Be friendly, and strip leading white-space, if any.
		size_t pos = 4;
		while (' ' == schema[pos]) pos++;

		//Get the name of the Library and Function
		//They should be sperated by a .
		std::size_t dotpos = schema.find("\\");
		if (std::string::npos == dotpos)
		{
			throw RuntimeException(TRACE_INFO,
				"Library name and function name must be separated by a '\\'");
		}
		std::string libName  = schema.substr(pos, dotpos - pos);
		std::string funcName = schema.substr(dotpos + 1);

		// Try and load the library and function.
		void* libHandle = dlopen(libName.c_str(), RTLD_LAZY);
		if (nullptr == libHandle)
			throw RuntimeException(TRACE_INFO,
				"Cannot open library: %s - %s", libName.c_str(), dlerror());

		void* sym = dlsym(libHandle, funcName.c_str());
		if (nullptr == sym)
			throw RuntimeException(TRACE_INFO,
				"Cannot find symbol %s in library: %s - %s",
				funcName.c_str(), libName.c_str(), dlerror());

		// Convert the void* pointer to the correct function type.
		// XXX FIXME -- it is incorrect to use UUID's for this purpose!
		// UUID's are meant for storage, not for the library API.
		// (The UUID lookup on the receiving side is going to be slow).
		UUID (*func)(AtomSpace*, UUID);
		func = reinterpret_cast<UUID (*)(AtomSpace *, UUID)>(sym);

		// Execute the function.
		Handle h(func(as, args.value()));

		// Close library after use.
		dlclose(libHandle);

		// Return the handle.
		return h;
	}

	// Unkown proceedure type.
	throw RuntimeException(TRACE_INFO,
		"Cannot evaluate unknown GroundedSchemaNode!");
}
