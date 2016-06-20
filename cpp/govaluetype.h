#ifndef GOVALUETYPE_H
#define GOVALUETYPE_H

#include "govalue.h"

using GoTypeInfo_ = void;

class GoValueType : public GoValueWrapper
{
public:
    GoValueType(GoTypeInfo* typeInfo, GoTypeSpec_* spec) : GoValueWrapper(hookGoValueTypeNew(this, spec), typeInfo, nullptr) {};

};

class GoPaintedValueType : public GoPaintedValueWrapper
{
public:
    GoPaintedValueType(GoTypeInfo* typeInfo, GoTypeSpec_* spec) : GoPaintedValueWrapper(hookGoValueTypeNew(this, spec), typeInfo, nullptr) {};

};

#endif // GOVALUETYPE_H

// vim:ts=4:sw=4:et
