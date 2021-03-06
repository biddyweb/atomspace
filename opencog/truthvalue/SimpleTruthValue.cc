/*
 * opencog/atomspace/SimpleTruthValue.cc
 *
 * Copyright (C) 2002-2007 Novamente LLC
 * All Rights Reserved
 *
 * Written by Welter Silva <welter@vettalabs.com>
 *            Guilherme Lamacie

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
 *
 */

#include <math.h>
#include <typeinfo>

#include <opencog/util/platform.h>
#include <opencog/util/exceptions.h>

#include "SimpleTruthValue.h"

//#define DPRINTF printf
#define DPRINTF(...)

using namespace opencog;

SimpleTruthValue::SimpleTruthValue(strength_t m, confidence_t c)
{
    _mean = m;
    _confidence = c;
}

SimpleTruthValue::SimpleTruthValue(const TruthValue& source)
{
    _mean = source.getMean();
    _confidence = source.getConfidence();
}
SimpleTruthValue::SimpleTruthValue(SimpleTruthValue const& source)
{
    _mean = source._mean;
    _confidence = source._confidence;
}

strength_t SimpleTruthValue::getMean() const
{
    return _mean;
}

count_t SimpleTruthValue::getCount() const
{
    // Formula from PLN book.
    confidence_t cf = std::min(_confidence, 0.9999998f);
    return static_cast<count_t>(DEFAULT_K * cf / (1.0f - cf));
}

confidence_t SimpleTruthValue::getConfidence() const
{
    return _confidence;
}

// This is the merge formula appropriate for PLN.
TruthValuePtr SimpleTruthValue::merge(TruthValuePtr other,
                                      const MergeCtrl& mc) const
{
    switch(mc.tv_formula)
    {
        case MergeCtrl::TVFormula::HIGHER_CONFIDENCE:
            return higher_confidence_merge(other);

        case MergeCtrl::TVFormula::PLN_BOOK_REVISION:
        {
            // Based on Section 5.10.2(A heuristic revision rule for STV)
            // of the PLN book
            if (other->getType() != SIMPLE_TRUTH_VALUE)
                throw RuntimeException(TRACE_INFO,
                                   "Don't know how to merge %s into a "
                                   "SimpleTruthValue using the default style",
                                   typeid(*other).name());

            confidence_t cf = std::min(getConfidence(), 0.9999998f);
            auto count = static_cast<count_t>(DEFAULT_K * cf / (1.0f - cf));
            auto count2 = other->getCount();
#define CVAL  0.2f
            auto count_new = count + count2 - std::min(count, count2) * CVAL;
            auto mean_new = (getMean() * count + other->getMean() * count2)
                / (count + count2);
            confidence_t confidence_new = static_cast<confidence_t>(count_new / (count_new + DEFAULT_K));
            return std::make_shared<SimpleTruthValue>(mean_new, confidence_new);
        }
        default:
            throw RuntimeException(TRACE_INFO,
                                   "SimpleTruthValue::merge: case not implemented");
            return nullptr;
       }
}

std::string SimpleTruthValue::toString() const
{
    char buf[1024];
    sprintf(buf, "(stv %f %f)",
            static_cast<float>(getMean()),
            static_cast<float>(getConfidence()));
    return buf;
}

bool SimpleTruthValue::operator==(const TruthValue& rhs) const
{
    const SimpleTruthValue *stv = dynamic_cast<const SimpleTruthValue *>(&rhs);
    if (NULL == stv) return false;

#define FLOAT_ACCEPTABLE_ERROR 0.000001
    if (FLOAT_ACCEPTABLE_ERROR < fabs(_mean - stv->_mean)) return false;

    if (FLOAT_ACCEPTABLE_ERROR < fabs(_confidence - stv->_confidence)) return false;
    return true;
}

TruthValueType SimpleTruthValue::getType() const
{
    return SIMPLE_TRUTH_VALUE;
}
