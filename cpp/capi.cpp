#include <QApplication>
#include <QQuickView>
#include <QQuickItem>
#include <QtQml>
#include <QDebug>
#include <QQuickImageProvider>

#include <string.h>

#include "govalue.h"
#include "govaluetype.h"
#include "connector.h"
#include "capi.h"

QJSValue unpackDataValueJS(DataValue value, QQmlEngine* engine);
QVariant unpackDataValue2(DataValue value, QQmlEngine* engine);
DataValue packDataValue2(QVariant qvar);

static char *local_strdup(const char *str)
{
    char *strcopy = 0;
    if (str) {
        size_t len = strlen(str) + 1;
        strcopy = (char *)malloc(len);
        memcpy(strcopy, str, len);
    }
    return strcopy;
}

error *errorf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    QString str = QString().vsprintf(format, ap);
    va_end(ap);
    QByteArray ba = str.toUtf8();
    return local_strdup(ba.constData());
}

void panicf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    QString str = QString().vsprintf(format, ap);
    va_end(ap);
    QByteArray ba = str.toUtf8();
    hookPanic(local_strdup(ba.constData()));
}

void newGuiApplication()
{
    static char empty[1] = {0};
    static char *argv[] = {empty, 0};
    static int argc = 1;
    new QApplication(argc, argv);

    // The event loop should never die.
    qApp->setQuitOnLastWindowClosed(false);
}

void applicationExec()
{
    qApp->exec();
}

void applicationExit()
{
    qApp->exit(0);
}

void applicationFlushAll()
{
    qApp->processEvents();
}

void *currentThread()
{
    return QThread::currentThread();
}

void *appThread()
{
    return QCoreApplication::instance()->thread();
}

QQmlEngine_ *newEngine(QObject_ *parent)
{
    return new QQmlEngine(reinterpret_cast<QObject *>(parent));
}

QQmlContext_ *engineRootContext(QQmlEngine_ *engine)
{
    return reinterpret_cast<QQmlEngine *>(engine)->rootContext();
}

void engineSetContextForObject(QQmlEngine_ *engine, QObject_ *object)
{
    QQmlEngine *qengine = reinterpret_cast<QQmlEngine *>(engine);
    QObject *qobject = reinterpret_cast<QObject *>(object);

    QQmlEngine::setContextForObject(qobject, qengine->rootContext());
}

void engineSetOwnershipCPP(QQmlEngine_ *engine, QObject_ *object)
{
    QQmlEngine *qengine = reinterpret_cast<QQmlEngine *>(engine);
    QObject *qobject = reinterpret_cast<QObject *>(object);

    qengine->setObjectOwnership(qobject, QQmlEngine::CppOwnership);
}

void engineSetOwnershipJS(QQmlEngine_ *engine, QObject_ *object)
{
    QQmlEngine *qengine = reinterpret_cast<QQmlEngine *>(engine);
    QObject *qobject = reinterpret_cast<QObject *>(object);

    qengine->setObjectOwnership(qobject, QQmlEngine::JavaScriptOwnership);
}

QQmlComponent_ *newComponent(QQmlEngine_ *engine, QObject_ *parent)
{
    QQmlEngine *qengine = reinterpret_cast<QQmlEngine *>(engine);
    //QObject *qparent = reinterpret_cast<QObject *>(parent);
    QQmlComponent *qcomponent = new QQmlComponent(qengine);
    // Qt 5.2.0 returns NULL on qmlEngine(qcomponent) without this.
    QQmlEngine::setContextForObject(qcomponent, qengine->rootContext());
    return qcomponent;
}

class GoImageProvider : public QQuickImageProvider {

    // TODO Destroy this when engine is destroyed.

    public:

    GoImageProvider(void *imageFunc) : QQuickImageProvider(QQmlImageProviderBase::Image), imageFunc(imageFunc) {};

    virtual QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize)
    {
        QByteArray ba = id.toUtf8();
        int width = 0, height = 0;
        if (requestedSize.isValid()) {
            width = requestedSize.width();
            height = requestedSize.height();
        }
        QImage *ptr = reinterpret_cast<QImage *>(hookRequestImage(imageFunc, (char*)ba.constData(), ba.size(), width, height));
        QImage image = *ptr;
        delete ptr;

        *size = image.size();
        if (requestedSize.isValid() && requestedSize != *size) {
            image = image.scaled(requestedSize, Qt::KeepAspectRatio);
        }
        return image;
    };

    private:

    void *imageFunc;
};

void engineAddImageProvider(QQmlEngine_ *engine, QString_ *providerId, void *imageFunc)
{
    QQmlEngine *qengine = reinterpret_cast<QQmlEngine *>(engine);
    QString *qproviderId = reinterpret_cast<QString *>(providerId);

    qengine->addImageProvider(*qproviderId, new GoImageProvider(imageFunc));
}

void componentLoadURL(QQmlComponent_ *component, const char *url, int urlLen)
{
    QByteArray qurl(url, urlLen);
    QString qsurl = QString::fromUtf8(qurl);
    reinterpret_cast<QQmlComponent *>(component)->loadUrl(qsurl);
}

void componentSetData(QQmlComponent_ *component, const char *data, int dataLen, const char *url, int urlLen)
{
    QByteArray qdata(data, dataLen);
    QByteArray qurl(url, urlLen);
    QString qsurl = QString::fromUtf8(qurl);
    reinterpret_cast<QQmlComponent *>(component)->setData(qdata, qsurl);
}

char *componentErrorString(QQmlComponent_ *component)
{
    QQmlComponent *qcomponent = reinterpret_cast<QQmlComponent *>(component);
    if (qcomponent->isReady()) {
        return NULL;
    }
    if (qcomponent->isError()) {
        QByteArray ba = qcomponent->errorString().toUtf8();
        return local_strdup(ba.constData());
    }
    return local_strdup("component is not ready (why!?)");
}

QObject_ *componentCreate(QQmlComponent_ *component, QQmlContext_ *context)
{
    QQmlComponent *qcomponent = reinterpret_cast<QQmlComponent *>(component);
    QQmlContext *qcontext = reinterpret_cast<QQmlContext *>(context);

    if (!qcontext) {
        qcontext = qmlContext(qcomponent);
    }
    return qcomponent->create(qcontext);
}

QQuickWindow_ *componentCreateWindow(QQmlComponent_ *component, QQmlContext_ *context)
{
    QQmlComponent *qcomponent = reinterpret_cast<QQmlComponent *>(component);
    QQmlContext *qcontext = reinterpret_cast<QQmlContext *>(context);

    if (!qcontext) {
        qcontext = qmlContext(qcomponent);
    }
    QObject *obj = qcomponent->create(qcontext);
    if (!objectIsWindow(obj)) {
        QQuickView *view = new QQuickView(qmlEngine(qcomponent), 0);
        view->setContent(qcomponent->url(), qcomponent, obj);
        view->setResizeMode(QQuickView::SizeRootObjectToView);
        obj = view;
    }
    return obj;
}

// Workaround for bug https://bugs.launchpad.net/bugs/1179716
struct DoShowWindow : public QQuickWindow {
    void show() {
        QQuickWindow::show();
        QResizeEvent resize(size(), size());
        resizeEvent(&resize);
    }
};

void windowShow(QQuickWindow_ *win)
{
    reinterpret_cast<DoShowWindow *>(win)->show();
}

void windowHide(QQuickWindow_ *win)
{
    reinterpret_cast<QQuickWindow *>(win)->hide();
}

uintptr_t windowPlatformId(QQuickWindow_ *win)
{
    return reinterpret_cast<QQuickWindow *>(win)->winId();
}

void windowConnectHidden(QQuickWindow_ *win)
{
    QQuickWindow *qwin = reinterpret_cast<QQuickWindow *>(win);
    QObject::connect(qwin, &QWindow::visibleChanged, [=](bool visible){
        if (!visible) {
            hookWindowHidden(win);
        }
    });
}

QObject_ *windowRootObject(QQuickWindow_ *win)
{
    if (objectIsView(win)) {
        return reinterpret_cast<QQuickView *>(win)->rootObject();
    }
    return win;
}

QImage_ *windowGrabWindow(QQuickWindow_ *win)
{
    QQuickWindow *qwin = reinterpret_cast<QQuickWindow *>(win);
    QImage *image = new QImage;
    *image = qwin->grabWindow().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    return image;
}

QImage_ *newImage(int width, int height)
{
    return new QImage(width, height, QImage::Format_ARGB32_Premultiplied);
}

void delImage(QImage_ *image)
{
    delete reinterpret_cast<QImage *>(image);
}

void imageSize(QImage_ *image, int *width, int *height)
{
    QImage *qimage = reinterpret_cast<QImage *>(image);
    *width = qimage->width();
    *height = qimage->height();
}

unsigned char *imageBits(QImage_ *image)
{
    QImage *qimage = reinterpret_cast<QImage *>(image);
    return qimage->bits();
}

const unsigned char *imageConstBits(QImage_ *image)
{
    QImage *qimage = reinterpret_cast<QImage *>(image);
    return qimage->constBits();
}

void contextSetObject(QQmlContext_ *context, QObject_ *value)
{
    QQmlContext *qcontext = reinterpret_cast<QQmlContext *>(context);
    QObject *qvalue = reinterpret_cast<QObject *>(value);

    // Give qvalue an engine reference if it doesn't yet have one.
    if (!qmlEngine(qvalue)) {
        QQmlEngine::setContextForObject(qvalue, qcontext->engine()->rootContext());
    }

    qcontext->setContextObject(qvalue);
}

void contextSetProperty(QQmlContext_ *context, QString_ *name, DataValue *value)
{
    const QString *qname = reinterpret_cast<QString *>(name);
    QQmlContext *qcontext = reinterpret_cast<QQmlContext *>(context);

    QVariant qvar = unpackDataValue2(*value, qcontext->engine());
    qcontext->setContextProperty(*qname, qvar);
}

void contextGetProperty(QQmlContext_ *context, QString_ *name, DataValue *result)
{
    QQmlContext *qcontext = reinterpret_cast<QQmlContext *>(context);
    const QString *qname = reinterpret_cast<QString *>(name);

    QVariant var = qcontext->contextProperty(*qname);
    *result = packDataValue2(var);
}

QQmlContext_ *contextSpawn(QQmlContext_ *context)
{
    QQmlContext *qcontext = reinterpret_cast<QQmlContext *>(context);
    return new QQmlContext(qcontext);
}

void delObject(QObject_ *object)
{
    delete reinterpret_cast<QObject *>(object);
}

void delObjectLater(QObject_ *object)
{
    reinterpret_cast<QObject *>(object)->deleteLater();
}

const char *objectTypeName(QObject_ *object)
{
    return reinterpret_cast<QObject *>(object)->metaObject()->className();
}

int objectGetProperty(QObject_ *object, const char *name, DataValue *result)
{
    QObject *qobject = reinterpret_cast<QObject *>(object);
    QVariant var = QQmlProperty::read(qobject, name, qmlContext(qobject));
    *result = packDataValue2(var);

    return var.isValid() ? 1 : 0;
}

error *objectSetProperty(QObject_ *object, const char *name, DataValue *value)
{
    QObject *qobject = reinterpret_cast<QObject *>(object);

    QQmlProperty property(qobject, QString(name));
    if (!property.isValid()) {
        return errorf("cannot set non-existent property \"%s\" on type %s", name, qobject->metaObject()->className());
    }

    QVariant qvar = unpackDataValue2(*value, qmlEngine(qobject));
    if (!property.write(qvar)) {
        if (!property.isWritable()) {
            return errorf("cannot set non-writable property \"%s\" on type", name, qobject->metaObject()->className());
        }
        auto propertyType = property.propertyType();
        auto qvarType = qvar.userType();
        QObject*  unwrapped;
        if (qvarType == qMetaTypeId<QJSValue>() && (unwrapped = qvar.value<QJSValue>().toQObject())) {
            return errorf("cannot set property \"%s\" with type %s to value of %s*", name, QMetaType::typeName(propertyType), unwrapped->metaObject()->className());
        } else {
            return errorf("cannot set property \"%s\" with type %s to value of %s", name, QMetaType::typeName(propertyType), QMetaType::typeName(qvarType));
        }
    }

    return 0;
}

error *objectInvoke(QObject_ *object, const char *method, int methodLen, DataValue *resultdv, DataValue *paramsdv, int paramsLen)
{
    QObject *qobject = reinterpret_cast<QObject *>(object);

    QVariant result;
    QVariant param[MaxParams];  //needed because Q_ARG takes an address, so we can't use a temporary QVariant created by unpackDataValue
    QGenericArgument arg[MaxParams];
    for (int i = 0; i < paramsLen; i++) {
        param[i] = unpackDataValue2(paramsdv[i], qmlEngine(qobject));
        arg[i] = Q_ARG(QVariant, param[i]);
    }
    if (paramsLen > 10) {
        panicf("fix the parameter dispatching");
    }

	//todo: test
	bool ok = QMetaObject::invokeMethod(qobject, method, Qt::DirectConnection, Q_RETURN_ARG(QVariant, result), arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], arg[8], arg[9]);
	if (ok) {
		*resultdv = packDataValue2(result);
		return 0;
	}

    const QMetaObject *metaObject = qobject->metaObject();
    // Walk backwards so descendants have priority.
    for (int i = metaObject->methodCount()-1; i >= 0; i--) {
        QMetaMethod metaMethod = metaObject->method(i);
        QMetaMethod::MethodType methodType = metaMethod.methodType();
        if (methodType == QMetaMethod::Method || methodType == QMetaMethod::Slot) {
            QByteArray name = metaMethod.name();
            if (name.length() == methodLen && qstrncmp(name.constData(), method, methodLen) == 0) {
                if (metaMethod.parameterCount() < paramsLen) {
                    // TODO Might continue looking to see if a different signal has the same name and enough arguments.
                    return errorf("method \"%s\" has too few parameters for provided arguments", method);
                }

                bool ok;
                if (metaMethod.returnType() == QMetaType::Void) {
                    ok = metaMethod.invoke(qobject, Qt::DirectConnection,
                        arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], arg[8], arg[9]);
                } else {
                    ok = metaMethod.invoke(qobject, Qt::DirectConnection, Q_RETURN_ARG(QVariant, result),
                        arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], arg[8], arg[9]);
                }
                if (!ok) {
                    return errorf("invalid parameters to method \"%s\"", method);
                }

                *resultdv = packDataValue2(result);
                return 0;
            }
        }
    }

    return errorf("object does not expose a method \"%s\"", method);
}

void objectFindChild(QObject_ *object, QString_ *name, DataValue *resultdv)
{
    QObject *qobject = reinterpret_cast<QObject *>(object);
    QString *qname = reinterpret_cast<QString *>(name);
    
    QVariant var;
    QObject *result = qobject->findChild<QObject *>(*qname);
    if (result) {
        var.setValue(result);
    }
    *resultdv = packDataValue2(var);
}

void objectSetParent(QObject_ *object, QObject_ *parent)
{
    QObject *qobject = reinterpret_cast<QObject *>(object);
    QObject *qparent = reinterpret_cast<QObject *>(parent);

    qobject->setParent(qparent);
}

error *objectConnect(QObject_ *object, const char *signal, int signalLen, QQmlEngine_ *engine, void *func, int argsLen)
{
    QObject *qobject = reinterpret_cast<QObject *>(object);
    QQmlEngine *qengine = reinterpret_cast<QQmlEngine *>(engine);
    QByteArray qsignal(signal, signalLen);

    const QMetaObject *meta = qobject->metaObject();
    // Walk backwards so descendants have priority.
    for (int i = meta->methodCount()-1; i >= 0; i--) {
            QMetaMethod method = meta->method(i);
            if (method.methodType() == QMetaMethod::Signal) {
                QByteArray name = method.name();
                if (name.length() == signalLen && qstrncmp(name.constData(), signal, signalLen) == 0) {
                    if (method.parameterCount() < argsLen) {
                        // TODO Might continue looking to see if a different signal has the same name and enough arguments.
                        return errorf("signal \"%s\" has too few parameters for provided function", name.constData());
                    }
                    Connector *connector = new Connector(qobject, method, qengine, func, argsLen);
                    const QMetaObject *connmeta = connector->metaObject();
                    QObject::connect(qobject, method, connector, connmeta->method(connmeta->methodOffset()));
                    return 0;
                }
            }
    }
    // Cannot use constData here as the byte array is not null-terminated.
    return errorf("object does not expose a \"%s\" signal", qsignal.data());
}

QQmlContext_ *objectContext(QObject_ *object)
{
    return qmlContext(static_cast<QObject *>(object));
}

int objectIsComponent(QObject_ *object)
{
    QObject *qobject = static_cast<QObject *>(object);
    return qobject_cast<QQmlComponent*>(qobject) ? 1 : 0;
}

int objectIsWindow(QObject_ *object)
{
    QObject *qobject = static_cast<QObject *>(object);
    return qobject_cast<QQuickWindow*>(qobject) ? 1 : 0;
}

int objectIsView(QObject_ *object)
{
    QObject *qobject = static_cast<QObject *>(object);
    return qobject_cast<QQuickView*>(qobject) ? 1 : 0;
}

error *objectGoAddr(QObject_ *object, GoAddr **addr)
{
    QObject *qobject = static_cast<QObject *>(object);
    GoValueWrapper *goValue = qobject_cast<GoValueWrapper*>(qobject);
    if (goValue) {
        *addr = goValue->addr;
        return 0;
    }
    GoPaintedValueWrapper* goPaintedValue = qobject_cast<GoPaintedValueWrapper*>(qobject);
    if (goPaintedValue) {
        *addr = goPaintedValue->addr;
        return 0;
    }
    return errorf("QML object is not backed by a Go value");
}

QString_ *newString(const char *data, int len)
{
    // This will copy data only once.
    QByteArray ba = QByteArray::fromRawData(data, len);
    return new QString(ba);
}

void delString(QString_ *s)
{
    delete reinterpret_cast<QString *>(s);
}

GoValue_ *newGoValue(GoAddr *addr, GoTypeInfo *typeInfo, QObject_ *parent)
{
    QObject *qparent = reinterpret_cast<QObject *>(parent);
    if (typeInfo->paint) {
        return new GoPaintedValueWrapper(addr, typeInfo, qparent);
    }
    return new GoValueWrapper(addr, typeInfo, qparent);
}

void goValueActivate(GoValue_ *value, GoTypeInfo *typeInfo, int addrOffset)
{
    GoMemberInfo *fieldInfo = typeInfo->fields;
    for (int i = 0; i < typeInfo->fieldsLen; i++) { //todo: don't fucking iterate, look it up in an array
        if (fieldInfo->addrOffset == addrOffset) {
            if (typeInfo->paint) {
                static_cast<GoPaintedValueWrapper*>(value)->propertyChanged(i);
            } else {
                static_cast<GoValueWrapper*>(value)->propertyChanged(i);
            }
            return;
        }
        fieldInfo++;
    }

    // TODO Return an error; probably an unexported field.
}

/*{
    QQmlListReference ref = qvar->value<QQmlListReference>();
    if (ref.isValid() && ref.canCount() && ref.canAt()) {
        int len = ref.count();
        DataValue *dvlist = (DataValue *) malloc(sizeof(DataValue) * len);
        QVariant elem;
        for (int i = 0; i < len; i++) {
            elem.setValue(ref.at(i));
            packDataValue(&elem, &dvlist[i]);
        }
        value->dataType = DTValueList;
        value->len = len;
        *(DataValue**)(value->data) = dvlist;
        break;
    }
}
if (qstrncmp(qvar->typeName(), "QQmlListProperty<", 17) == 0) {
    QQmlListProperty<QObject> *list = reinterpret_cast<QQmlListProperty<QObject>*>(qvar->data());
    if (list->count && list->at) {
        int len = list->count(list);
        DataValue *dvlist = (DataValue *) malloc(sizeof(DataValue) * len);
        QVariant elem;
        for (int i = 0; i < len; i++) {
            elem.setValue(list->at(list, i));
            packDataValue(&elem, &dvlist[i]);
        }
        value->dataType = DTValueList;
        value->len = len;
        *(DataValue**)(value->data) = dvlist;
        break;
    }
}*/

QVariant unpackDataValue2(DataValue value, QQmlEngine* engine) {
//	qDebug() << "DT" << value.dataType;
    switch (value.dataType) {
    case DTString:
        return QVariant(QString::fromUtf8(*reinterpret_cast<char**>(value.data), value.len));   //todo: try returning {bool/int/...}
    case DTBool:
        return QVariant(*reinterpret_cast<bool*>(value.data));
    case DTNumber:
        return QVariant(*reinterpret_cast<double*>(value.data));
    case DTNumberI:
        return QVariant(*reinterpret_cast<int*>(value.data));
    case DTNumberU:
        return QVariant(*reinterpret_cast<uint*>(value.data));
    case DTUintptr:
        return QVariant::fromValue(*reinterpret_cast<void**>(value.data));
    case DTColor:
        return QVariant::fromValue(QColor::fromRgba(*reinterpret_cast<QRgb*>(value.data)));
    case DTTime:
        return QVariant::fromValue(QDateTime::fromMSecsSinceEpoch(*reinterpret_cast<qint64*>(value.data), Qt::UTC));
    case DTList:    qDebug() << "list";
    case DTMap:     qDebug() << "map";
    case DTObject:  qDebug() << "obj";
    case DTInvalid: qDebug() << "invalid";
        qDebug() << "Unpacking list/map/object/invalid";
        return QVariant::fromValue(unpackDataValueJS(value, engine));
    case DTListProperty:    //todo:
        panicf("DTListProperty handling unimplemented");
        return QVariant();
    default:
        panicf("unknown data type: %v", value.dataType);
	    return QVariant();
    }
    //return QVariant::fromValue(QJSValue(QJSValue::UndefinedValue));
}

QJSValue unpackDataValueJS(DataValue value, QQmlEngine* engine) {
    switch (value.dataType) {
    case DTString:
        return QJSValue(QString::fromUtf8(*reinterpret_cast<char**>(value.data), value.len));
    case DTBool:
        return QJSValue(*reinterpret_cast<bool*>(value.data));
    case DTNumber:
        return QJSValue(*reinterpret_cast<double*>(value.data));
    case DTNumberI:
        return QJSValue(*reinterpret_cast<int*>(value.data));
    case DTNumberU:
        return QJSValue(*reinterpret_cast<uint*>(value.data));
    case DTColor:
        return engine->toScriptValue(QColor::fromRgba(*reinterpret_cast<QRgb*>(value.data)));
    case DTTime:
        return engine->toScriptValue(QDateTime::fromMSecsSinceEpoch(*reinterpret_cast<qint64*>(value.data), Qt::UTC));
    case DTList: {
	        QJSValue array = engine->newArray(value.len);
	        for (int i = 0; i < value.len; i++) {
	            array.setProperty(i, unpackDataValueJS((*reinterpret_cast<DataValue**>(value.data))[i], engine));
	        }
	        free(*reinterpret_cast<void**>(value.data));
	        return array;
        }
    case DTMap: {
	        QJSValue object = engine->newObject();
	        for (int i = 0; i < value.len; i+=2) {
	            DataValue key = (*reinterpret_cast<DataValue**>(value.data))[i+0];
	            DataValue val = (*reinterpret_cast<DataValue**>(value.data))[i+1];
	            object.setProperty(QString::fromUtf8(*reinterpret_cast<char**>(key.data), key.len), unpackDataValueJS(val, engine));
	        }
	        free(*reinterpret_cast<void**>(value.data));
	        return object;
        }
    case DTObject:
        return engine->newQObject(*reinterpret_cast<QObject**>(value.data));
    case DTInvalid:
        return QJSValue(QJSValue::NullValue);
    default:
        panicf("unknown data type: %v", value.dataType);
        break;
    }

    return QJSValue(QJSValue::UndefinedValue);
}

DataValue packDataValue2(QVariant qvar) {
	DataValue ret{DTUnknown};

	auto type = qvar.userType();
	switch (type) {
	case QMetaType::QString: {
			auto str = qvar.toString().toUtf8();
	        ret.dataType = DTString;
	        *reinterpret_cast<char**>(ret.data) = local_strdup(str.constData());    //todo: request allocation in Go-land, memcpy data there and unsafe-construct a string
	        ret.len = str.size();
		}
		break;
	case QMetaType::Bool: {
			ret.dataType = DTBool;
	        *reinterpret_cast<bool*>(ret.data) = qvar.toBool();
		}
		break;
	case QMetaType::Double: {
			ret.dataType = DTNumber;
	        *reinterpret_cast<double*>(ret.data) = qvar.toDouble();
		}
		break;
	case QMetaType::Int: {
			ret.dataType = DTNumberI;
            *reinterpret_cast<int*>(ret.data) = qvar.toInt();
		}
		break;
	case QMetaType::UInt: {
			ret.dataType = DTNumberU;
			*reinterpret_cast<uint*>(ret.data) = qvar.toUInt();
		}
		break;
	case QMetaType::QColor: {
			ret.dataType = DTColor;
            *reinterpret_cast<quint32*>(ret.data) = qvar.value<QColor>().rgba();
		}
		break;
	case QMetaType::QDateTime: {
			ret.dataType = DTTime;
	        *reinterpret_cast<qint64*>(ret.data) = qvar.toDateTime().toMSecsSinceEpoch();
		}
		break;
	case QMetaType::QVariantList: {
			qDebug() << "QMetaType::QVariantList";
		}
		break;
	case QMetaType::QVariantMap: {
			qDebug() << "QMetaType::QVariantMap";
		}
		break;
	case QMetaType::VoidStar: {
			auto data = qvar.value<void*>();
            if (data) {
                ret.dataType = DTUintptr;
                *reinterpret_cast<void**>(ret.data) = data;
            } else {
                ret.dataType = DTInvalid;
            }
		}
		break;
	case QMetaType::UnknownType: {
			ret.dataType = DTInvalid;
	    }
	    break;
	case QMetaType::QObjectStar: {
			qDebug() << "QMetaType::QObjectStar";

			if (auto govalue = qvar.value<GoValueWrapper*>()) {
	            qDebug() << "GoPacking GoValue*";
	            ret.dataType = DTGoAddr;
	            *reinterpret_cast<void**>(ret.data) = govalue->addr;
	            break;
	        }

	        if (auto govalue = qvar.value<GoPaintedValueWrapper*>()) {
	            qDebug() << "GoPacking GoPaintedValueWrapper*";
	            ret.dataType = DTGoAddr;
	            *reinterpret_cast<void**>(ret.data) = govalue->addr;
	            break;
	        }
			ret.dataType = DTObject;
	        *reinterpret_cast<void**>(ret.data) = qvar.value<QObject*>();
	        break;
        }
    default: {
		    if (type == qMetaTypeId<QJSValue>()) {
		        auto val = qvar.value<QJSValue>();
		        if (val.isArray()) {
		            int len = val.property("length").toInt();
		            DataValue* dvlist = reinterpret_cast<DataValue*>(malloc(sizeof(DataValue) * len));
	                for (int i = 0; i < len; i++) {
	                    dvlist[i] = packDataValue2(QVariant::fromValue(val.property(i)));
	                }
		            ret.dataType = DTList;
		            *reinterpret_cast<DataValue**>(ret.data) = dvlist;
		            ret.len = len;
		            break;
		        }

                if (val.isQObject()) {
					ret = packDataValue2(QVariant::fromValue(val.toQObject()));
                    break;
                }

		        if (val.isObject()) {
		            int cap = 16;
		            DataValue* dvlist = reinterpret_cast<DataValue*>(malloc(sizeof(DataValue) * cap));
		            QJSValueIterator it(val);
		            int i = 0;
	                while (it.next()) {
	                    if (i >= cap) {
	                        cap = (cap*3+1)/2;
	                        dvlist = reinterpret_cast<DataValue*>(realloc(dvlist, sizeof(DataValue) * cap));
	                    }
	                    dvlist[i+0] = packDataValue2(QVariant::fromValue(it.name()));
	                    dvlist[i+1] = packDataValue2(QVariant::fromValue(it.value()));
	                    i += 2;
                    }
		            ret.dataType = DTMap;
		            *reinterpret_cast<DataValue**>(ret.data) = dvlist;
		            ret.len = i;
		            break;
		        }

		        ret = packDataValue2(val.toVariant());
		        break;
		    }

		    if (qvar.canConvert<QQmlListReference>()) { //if (type == qMetaTypeId<QQmlListReference>()) {
		        qDebug() << "GoPacking QQmlListRef";
		        auto ref = qvar.value<QQmlListReference>();
		        if (ref.isValid() && ref.canCount() && ref.canAt()) {
		            int len = ref.count();
		            DataValue* dvlist = reinterpret_cast<DataValue*>(malloc(sizeof(DataValue) * len));
		            for (int i = 0; i < len; i++) {
		                dvlist[i] = packDataValue2(QVariant::fromValue(ref.at(i)));
		            }
		            ret.dataType = DTList;
		            ret.len = len;
		            *reinterpret_cast<DataValue**>(ret.data) = dvlist;
		            break;
		        }
		    }

		    if (qvar.canConvert<QObject*>()) {
		        qvar.convert(QMetaType::QObjectStar);
		        ret = packDataValue2(qvar);
		        break;
		    }
		}
	}

	if (ret.dataType == DTUnknown) {
		qDebug() << "GoPacking failed" << qvar.type() << qvar.userType() << qvar.typeName();
	}
    return ret;    //undefined
}

QObject *listPropertyAt(QQmlListProperty<QObject> *list, int i)
{
    return reinterpret_cast<QObject *>(hookListPropertyAt(list->data, (intptr_t)list->dummy1, (intptr_t)list->dummy2, i));
}

int listPropertyCount(QQmlListProperty<QObject> *list)
{
    return hookListPropertyCount(list->data, (intptr_t)list->dummy1, (intptr_t)list->dummy2);
}

void listPropertyAppend(QQmlListProperty<QObject> *list, QObject *obj)
{
    hookListPropertyAppend(list->data, (intptr_t)list->dummy1, (intptr_t)list->dummy2, obj);
}

void listPropertyClear(QQmlListProperty<QObject> *list)
{
    hookListPropertyClear(list->data, (intptr_t)list->dummy1, (intptr_t)list->dummy2);
}

QQmlListProperty_ *newListProperty(GoAddr *addr, intptr_t reflectIndex, intptr_t setIndex)
{
    QQmlListProperty<QObject> *list = new QQmlListProperty<QObject>();
    list->object = (QObject*)1; //workaround: avoid an assert in Qt code (QQmlObjectCreator::setPropertyBinding: Q_ASSERT(_currentList.object))
    list->data = addr;
    list->dummy1 = (void*)reflectIndex;
    list->dummy2 = (void*)setIndex;
    list->at = listPropertyAt;
    list->count = listPropertyCount;
    list->append = listPropertyAppend;
    list->clear = listPropertyClear;
    return list;
}

void internalLogHandler(QtMsgType severity, const QMessageLogContext &context, const QString &text)
{
    QByteArray textba = text.toUtf8();
    const int fileLength = context.file ? strlen(context.file) : 0;
    LogMessage message = {severity, textba.constData(), textba.size(), context.file, fileLength, context.line};
    hookLogHandler(&message);
}

void installLogHandler()
{
    qInstallMessageHandler(internalLogHandler);
}


extern bool qRegisterResourceData(int version, const unsigned char *tree, const unsigned char *name, const unsigned char *data);
extern bool qUnregisterResourceData(int version, const unsigned char *tree, const unsigned char *name, const unsigned char *data);

void registerResourceData(int version, char *tree, char *name, char *data)
{
    qRegisterResourceData(version, (unsigned char*)tree, (unsigned char*)name, (unsigned char*)data);
}

void unregisterResourceData(int version, char *tree, char *name, char *data)
{
    qUnregisterResourceData(version, (unsigned char*)tree, (unsigned char*)name, (unsigned char*)data);
}

// vim:ts=4:sw=4:et:ft=cpp
