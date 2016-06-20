#include <private/qmetaobjectbuilder_p.h>

#include <QtOpenGL/QtOpenGL>
#include <QtOpenGL/QGLFunctions>

#include <QtQml/QtQml>
#include <QQmlEngine>
#include <QDebug>

#include "govalue.h"
#include "capi.h"

class GoValueMetaObject : public QMetaObject
{
public:
    GoValueMetaObject(QObject* wrapper, GoAddr *addr, GoTypeInfo *typeInfo) : wrapper(wrapper), addr(addr), typeInfo(typeInfo) {
        this->d = metaObjectFor(typeInfo)->d;
    }

    virtual int metaCall(QMetaObject::Call c, int id, void** a);
    void signalChange(int propertyIdx);

private:
    QObject* wrapper;
    GoAddr* addr;
    GoTypeInfo* typeInfo;
};

int GoValueMetaObject::metaCall(QMetaObject::Call call, int idx, void** qargs) {
    // TODO Cache propertyOffset, methodOffset (and maybe qmlEngine)
    int methOffset = this->methodOffset();
    int propOffset = this->propertyOffset();
    int ownMethodCount = this->methodCount() - methOffset;
    int ownPropertyCount = this->propertyCount() - propOffset;

    switch (call) {
    case QMetaObject::InvokeMetaMethod: {
            int methodIdx = idx - ownPropertyCount; //idx is offset by all the properties signals; we have to shift it back before calling Go code
            if (methodIdx < ownMethodCount) {
                auto methodInfo = this->typeInfo->methods[methodIdx];
                DataValue args[1 + MaxParams];  // args[0] is the result if any.
                for (int i = 1; i < methodInfo.numIn+1; i++) {
                    args[i] = packDataValue2(*reinterpret_cast<QVariant*>(qargs[i]));
                }
                hookGoValueCallMethod(qmlEngine(this->wrapper), this->addr, methodInfo.reflectIndex, args);
                if (methodInfo.numOut > 0) {
                    *reinterpret_cast<QVariant*>(qargs[0]) = unpackDataValue2(args[0], qmlEngine(this->wrapper));
                }
            }
            idx -= ownMethodCount;
        }
        break;
    case QMetaObject::ReadProperty: {
            if (idx < ownPropertyCount) {
				auto fieldInfo = this->typeInfo->fields[idx];
				DataValue result;
				hookGoValueReadField(qmlEngine(this->wrapper), this->addr, fieldInfo.reflectIndex, fieldInfo.reflectGetIndex, fieldInfo.reflectSetIndex, &result);
				if (fieldInfo.memberType != DTListProperty) {
					*reinterpret_cast<QVariant*>(qargs[0]) = unpackDataValue2(result, qmlEngine(this->wrapper));
				} else {
					qDebug() << "DTListProperty";
					if (result.dataType != DTListProperty) {
						panicf("reading DTListProperty field returned non-DTListProperty result");
					}
					// TODO Could provide a single variable in the stack to ReadField instead.
					QQmlListProperty<QObject>* list = *reinterpret_cast<QQmlListProperty<QObject>**>(result.data);
					*reinterpret_cast<QQmlListProperty<QObject>*>(qargs[0]) = *list;
					delete list;
				}
			}
			idx -= ownPropertyCount;
        }
        break;
    case QMetaObject::WriteProperty: {
            if (idx < ownPropertyCount) {
	            auto fieldInfo = this->typeInfo->fields[idx];
	            DataValue assign = packDataValue2(*reinterpret_cast<QVariant*>(qargs[0]));
	            hookGoValueWriteField(qmlEngine(this->wrapper), addr, fieldInfo.reflectIndex, fieldInfo.reflectSetIndex, &assign);
	            this->activate(this->wrapper, methOffset + idx, 0);
            }
            idx -= ownPropertyCount;
        }
        break;
	case QMetaObject::ResetProperty:
    case QMetaObject::QueryPropertyDesignable:
    case QMetaObject::QueryPropertyScriptable:
    case QMetaObject::QueryPropertyStored:
    case QMetaObject::QueryPropertyEditable:
    case QMetaObject::QueryPropertyUser: {
            idx -= ownPropertyCount;
        }
//        break;
	default:
//        CreateInstance,
//        IndexOfMethod,
//        RegisterPropertyMetaType,
//        RegisterMethodArgumentMetaType
		qWarning() << "Unhandled Metacall" << call;
    }
    return idx;
}


void GoValueMetaObject::signalChange(int propertyIndex) {
    // Properties are added first, so the first fieldLen methods are in
    // fact the signals of the respective properties.
    this->activate(this->wrapper, this->methodOffset() + propertyIndex, 0);
}



QMetaObject* metaObjectFor(GoTypeInfo* typeInfo) {
    if (typeInfo->metaObject) {
            return reinterpret_cast<QMetaObject*>(typeInfo->metaObject);
    }

    QMetaObjectBuilder mob;
    if (typeInfo->paint) {
//        mob.setSuperClass(&QQuickPaintedItem::staticMetaObject);
		mob.setSuperClass(&GoPaintedValueWrapper::staticMetaObject);
	} else {
		mob.setSuperClass(&GoValueWrapper::staticMetaObject);
	}
    mob.setClassName(typeInfo->typeName);
//    mob.setStaticMetacallFunction(qt_static_metacall);

    GoMemberInfo* memberInfo = typeInfo->fields;
    int relativePropIndex = mob.propertyCount();
    for (int i = 0; i < typeInfo->fieldsLen; i++, memberInfo++) {
		auto signal = mob.addSignal("__" + QByteArray(memberInfo->memberName) + "Changed()");

        const char* typeName = "QVariant";
        if (memberInfo->memberType == DTListProperty) {
            typeName = "QQmlListProperty<QObject>";
        }

        auto property = mob.addProperty(memberInfo->memberName, typeName, signal.index());
        property.setWritable(true);
        relativePropIndex++;
    }

    memberInfo = typeInfo->methods;
    for (int i = 0; i < typeInfo->methodsLen; i++, memberInfo++) {
        if (*memberInfo->resultSignature) {
            mob.addMethod(memberInfo->methodSignature, memberInfo->resultSignature);
        } else {
            mob.addMethod(memberInfo->methodSignature);
        }
    }

    // TODO Support default properties.
    //mob.addClassInfo("DefaultProperty", "objects");

    QMetaObject* mo = mob.toMetaObject();
    typeInfo->metaObject = mo;
    return mo;
}





GoValueWrapper::GoValueWrapper(GoAddr* addr, GoTypeInfo *typeInfo, QObject *parent) : addr(addr), typeInfo(typeInfo) {
    this->meta = new GoValueMetaObject(this, addr, typeInfo);
    this->setParent(parent);
}

GoValueWrapper::~GoValueWrapper() {
    hookGoValueDestroyed(qmlEngine(this), addr);
}

void GoValueWrapper::propertyChanged(int propertyIndex) {
    this->meta->signalChange(propertyIndex);
}

struct qt_meta_stringdata_GoValueWrapper_t { QByteArrayData data[1]; char stringdata[16]; };
static const qt_meta_stringdata_GoValueWrapper_t qt_meta_stringdata_GoValueWrapper = {
	{{{{(-1)}}, 14, 0, 0, offsetof(qt_meta_stringdata_GoValueWrapper_t, stringdata) + 0 - 0 * sizeof(QByteArrayData)}}, "GoValueWrapper\0"  //QByteArray data header
};
static const uint qt_meta_data_GoValueWrapper[] = {7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};   //revision, methods, properties, etc.
const QMetaObject GoValueWrapper::staticMetaObject = {
    {&QObject::staticMetaObject, qt_meta_stringdata_GoValueWrapper.data, qt_meta_data_GoValueWrapper, qt_static_metacall, 0, 0}  //parent, name, members, static metacall function, related, extra
};

const QMetaObject* GoValueWrapper::metaObject() const {
    return this->meta;
}

void GoValueWrapper::qt_static_metacall(QObject* obj, QMetaObject::Call call, int idx, void** args) {
    Q_UNUSED(obj); Q_UNUSED(call); Q_UNUSED(idx); Q_UNUSED(args);
}

int GoValueWrapper::qt_metacall(QMetaObject::Call call, int idx, void** args) {
	idx = QObject::qt_metacall(call, idx, args);
    if (idx < 0) {
        return idx;
    }
    return this->meta->metaCall(call, idx, args);
}

void *GoValueWrapper::qt_metacast(const char* _clname) {
    if (!_clname) {
        return 0;
    }
    if (!strcmp(_clname, "GoValueWrapper\0")) {
        return static_cast<void*>(const_cast<GoValueWrapper*>(this));
    }
    return QObject::qt_metacast(_clname);
}




GoPaintedValueWrapper::GoPaintedValueWrapper(GoAddr* addr, GoTypeInfo *typeInfo, QObject *parent) : addr(addr), typeInfo(typeInfo) {
    this->meta = new GoValueMetaObject(this, addr, typeInfo);
    this->setParent(parent);
    QQuickItem::setFlag(QQuickItem::ItemHasContents, true);
    QQuickPaintedItem::setRenderTarget(QQuickPaintedItem::FramebufferObject);
}

GoPaintedValueWrapper::~GoPaintedValueWrapper() {
    hookGoValueDestroyed(qmlEngine(this), addr);
}

void GoPaintedValueWrapper::propertyChanged(int propertyIndex) {
    this->meta->signalChange(propertyIndex);
}

struct qt_meta_stringdata_GoPaintedValueWrapper_t { QByteArrayData data[1]; char stringdata[23]; };
#define QT_MOC_LITERAL(idx, ofs, len) Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, offsetof(qt_meta_stringdata_GoPaintedValueWrapper_t, stringdata) + ofs - idx * sizeof(QByteArrayData))
static const qt_meta_stringdata_GoPaintedValueWrapper_t qt_meta_stringdata_GoPaintedValueWrapper = {{QT_MOC_LITERAL(0, 0, 21)}, "GoPaintedValueWrapper\0"};
#undef QT_MOC_LITERAL
static const uint qt_meta_data_GoPaintedValueWrapper[] = {7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};   //revision, methods, properties, etc.
const QMetaObject GoPaintedValueWrapper::staticMetaObject = {
    {&QQuickPaintedItem::staticMetaObject, qt_meta_stringdata_GoPaintedValueWrapper.data, qt_meta_data_GoPaintedValueWrapper, qt_static_metacall, 0, 0}  //parent, name, members, static metacall function, related, extra
};

const QMetaObject* GoPaintedValueWrapper::metaObject() const {
    return this->meta;
}

void GoPaintedValueWrapper::qt_static_metacall(QObject* obj, QMetaObject::Call call, int idx, void** args) {
    Q_UNUSED(obj); Q_UNUSED(call); Q_UNUSED(idx); Q_UNUSED(args);
}

int GoPaintedValueWrapper::qt_metacall(QMetaObject::Call call, int idx, void** args) {
	idx = QQuickPaintedItem::qt_metacall(call, idx, args);
    if (idx < 0) {
        return idx;
    }
    return this->meta->metaCall(call, idx, args);
}

void *GoPaintedValueWrapper::qt_metacast(const char* _clname) {
    if (!_clname) {
        return 0;
    }
    if (!strcmp(_clname, "GoPaintedValueWrapper\0")) {
        return static_cast<void*>(const_cast<GoPaintedValueWrapper*>(this));
    }
    return QQuickPaintedItem::qt_metacast(_clname);
}

void GoPaintedValueWrapper::paint(QPainter* painter) {
    painter->beginNativePainting();
    hookGoValuePaint(qmlEngine(this), this->addr, this->typeInfo->paint->reflectIndex);
    painter->endNativePainting();
}

// vim:ts=4:sw=4:et:ft=cpp
