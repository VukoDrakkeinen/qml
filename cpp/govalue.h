#ifndef GOVALUE_H
#define GOVALUE_H

// Unfortunately we need access to private bits, because the
// whole dynamic meta-object concept is sadly being hidden
// away, and without it this package wouldn't exist.
#include <private/qmetaobject_p.h>

#include <QQuickPaintedItem>
#include <QPainter>

#include "capi.h"

class GoValueMetaObject;
QMetaObject *metaObjectFor(GoTypeInfo *typeInfo);



class GoValueWrapper : public QObject
{
    Q_OBJECT
public:
    GoValueWrapper(GoAddr* addr, GoTypeInfo* typeInfo, QObject* parent = nullptr);
    virtual ~GoValueWrapper();

    void propertyChanged(int propIndex);

public:
    GoAddr* addr;
    GoTypeInfo* typeInfo;

private:
    GoValueMetaObject* meta;
};



class GoPaintedValueWrapper : public QQuickPaintedItem
{
    Q_OBJECT
public:
    GoPaintedValueWrapper(GoAddr* addr, GoTypeInfo* typeInfo, QObject* parent = nullptr);
    virtual ~GoPaintedValueWrapper();

    void propertyChanged(int propIndex);
    virtual void paint(QPainter* painter);

public:
    GoAddr* addr;
    GoTypeInfo* typeInfo;

private:
    GoValueMetaObject* meta;
};

#endif // GOVALUE_H

// vim:ts=4:sw=4:et:ft=cpp
