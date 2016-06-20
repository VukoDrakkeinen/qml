#include "govaluetype.h"
#include <vector>
#include <cstdint>
#include <type_traits>
#include <cstdarg>
#include <sys/mman.h>
#include <unistd.h>
#include <unordered_map>

//WARNING: terrible hacks ahead!
//todo: try replacing with V8 engine code generation

static constexpr const auto dummyAddr0 = 0xA0A0A0A0A0A0A0A0;
static constexpr const auto dummyAddr1 = 0xB0B0B0B0B0B0B0B0;
static constexpr const auto dummyAddr2 = 0xC0C0C0C0C0C0C0C0;
static constexpr const auto dummyAddr3 = 0xD0D0D0D0D0D0D0D0;
//x86
static constexpr const uint8_t CALL = 0xE8;
static constexpr const uint8_t JMP  = 0xE9;
static constexpr const uint8_t LEA  = 0x8D;
//static constexpr const uint8_t OSOR = 0xE8;	//operand-size override (from default 32 bits to 16 bits)
static constexpr const uint8_t NOP  = 0x90;
static constexpr const uint8_t RET  = 0xC3;
static constexpr const uint8_t RETN = 0xC2; //ret, pop N_16 bytes from the stack
//static constexpr const uint8_t REL8[] = {0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0xE0, 0xE1, 0xE2, 0xE3, 0xEB};
static constexpr const uint8_t REL[] = {CALL, JMP};   // rel16/rel32
static constexpr const uint32_t NOP4 = 0x90909090;
//ARM
//todo

template<typename T>
auto assembleConstructor(GoTypeInfo* typeInfo, GoTypeSpec_* spec, uint8_t* sourceConstructor0, uint8_t* sourceConstructor1) {
//	qDebug() << "Building constructor with addresses:" << (void*) typeInfo << spec;

    auto fp0 = sourceConstructor0;
    auto fp1 = sourceConstructor1;

    int funcSize = 0;
    std::vector<int> offsets0;
    std::vector<int> offsets1;
    std::vector<int> relOffsets;
    std::unordered_map<int, bool> unhandled;
    {
        bool nopsEncountered = false;
        int i = 0;
        while (true) {
            if (fp0[i] == static_cast<uint8_t>(dummyAddr0) && fp1[i] == static_cast<uint8_t>(dummyAddr2)) {
                if (*reinterpret_cast<void**>(fp0+i) == reinterpret_cast<void*>(dummyAddr0)) {
                    offsets0.push_back(i);
                    i += sizeof(void*);
                    continue;
                }
            } else if (fp0[i] == static_cast<uint8_t>(dummyAddr1) && fp1[i] == static_cast<uint8_t>(dummyAddr3)) {
                if (*reinterpret_cast<void**>(fp0+i) == reinterpret_cast<void*>(dummyAddr1)) {
                    offsets1.push_back(i);
                    i += sizeof(void*);
                    continue;
                }
            } else if (fp0[i] == NOP && fp1[i] == NOP) {
                if (*reinterpret_cast<uint32_t*>(fp0+i) == NOP4) {
                    nopsEncountered = true;
                    i += 4;
                    continue;
                }
            } else if (nopsEncountered && fp0[i] == RET && fp1[i] == RET) {
                funcSize = i+1;
                break;
            } else if (nopsEncountered && fp0[i] == RETN && fp1[i] == RETN) {
                funcSize = i+3;	//16 bits of operand data
                break;
            } else {
                for (uint8_t rel : REL) { //todo: rel16
                    if (fp0[i] == rel && fp1[i] == rel && (*reinterpret_cast<int32_t*>(fp0+i+1) - *reinterpret_cast<int32_t*>(fp1+i+1)) == (fp1-fp0)) {
                        relOffsets.push_back(i+1);
                        i += 4;
                        goto mainLoopContinue;
                    }
                }
                if (fp0[i] == LEA && fp1[i] == LEA && (*reinterpret_cast<int32_t*>(fp0+i+2) - *reinterpret_cast<int32_t*>(fp1+i+2)) == (fp1-fp0)) {
                    relOffsets.push_back(i+2);
                    i += 5;
                    continue;
                }
                if (fp0[i] != fp1[i]) {
                    unhandled[i] = true;
                }
            }

            mainLoopContinue:
            i++;
        }
    }

//    for (int i : offsets0) {
//        qDebug() << "O0:" << i;
//    }
//    for (int i : offsets1) {
//        qDebug() << "O1:" << i;
//    }
//    qDebug() << "FS:" << funcSize;
//    for (int i : relOffsets) {
//        qDebug() << "RR:" << i;
//    }

	auto newConstructor = new uint8_t[funcSize];
	std::copy(fp0, fp0+funcSize, newConstructor);
	for (int i : offsets0) {
//		qDebug() << "Off0 from" << *reinterpret_cast<void**>(newConstructor+i);
        *reinterpret_cast<void**>(newConstructor+i) = typeInfo;
//        qDebug() << "       to" << *reinterpret_cast<void**>(newConstructor+i);
    }
    for (int i : offsets1) {
//        qDebug() << "Off1 from" << *reinterpret_cast<void**>(newConstructor+i);
        *reinterpret_cast<void**>(newConstructor+i) = spec;
//        qDebug() << "       to" << *reinterpret_cast<void**>(newConstructor+i);
    }
	auto diff = fp0-newConstructor;
	for (int i : relOffsets) {
		*reinterpret_cast<int32_t*>(newConstructor+i) += diff;
	}

//	{
//		auto dbg = qDebug();
//		dbg << "FP0:" << hex;
//		for (int i = 0; i < funcSize; i++) {
//			auto n = (int)fp0[i];
//			if (n <= 0x0F) {
//				dbg.nospace();
//				dbg << '0' << n;
//				dbg.space();
//			} else {
//				dbg << n;
//			}
//		}
//	}
//	{
//        auto dbg = qDebug();
//        dbg << "FP1:" << hex;
//        for (int i = 0; i < funcSize; i++) {
//            auto n = (int)fp1[i];
//            if (n <= 0x0F) {
//                dbg.nospace();
//                dbg << '0' << n;
//                dbg.space();
//            } else {
//                dbg << n;
//            }
//        }
//    }
//    {
//        auto dbg = qDebug();
//        dbg << "NEW:" << hex;
//        for (int i = 0; i < funcSize; i++) {
//            auto n = (int)newConstructor[i];
//            if (n <= 0x0F) {
//                dbg.nospace();
//                dbg << '0' << n;
//                dbg.space();
//            } else {
//                dbg << n;
//            }
//        }
//    }
//    {
//        auto dbg = qDebug();
//        dbg << "DFF:" << hex;
//        for (int i = 0; i < funcSize; i++) {
//            if (fp0[i] != fp1[i]) {
//                dbg << "**";
//            } else {
//                dbg << "  ";
//            }
//        }
//    }
//    {
//        auto dbg = qDebug();
//        dbg << "UHL:" << hex;
//        for (int i = 0; i < funcSize/2; i++) {
//            if (unhandled.count(i) > 0) {
//                dbg << "!!";
//            } else {
//                dbg << "  ";
//            }
//        }
//    }

	auto pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
	auto pageAlignedAddr = reinterpret_cast<void*>(reinterpret_cast<size_t>(newConstructor) / pageSize * pageSize);
	size_t protectedBytesCount = newConstructor - reinterpret_cast<uint8_t*>(funcSize);
	mprotect(pageAlignedAddr, protectedBytesCount, PROT_READ | PROT_WRITE | PROT_EXEC);

	{
		int howMany;
		if ((howMany = unhandled.size()) != 0) {
//			qDebug() << "Unhandled assembly difference encountered!" << howMany << '\n';
			delete[] newConstructor;
	        return static_cast<void*>(nullptr);
		}
	}

	return reinterpret_cast<void*>(newConstructor);
}

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#define NOINLINE __attribute__ ((noinline))
#elif defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#warning "Unknown compiler. Using GCC's noinline attribute."
#define NOINLINE __attribute__ ((noinline))
#endif

template<typename T>
void NOINLINE typeConstructor_Impl(void* memory, GoTypeInfo* typeInfo, GoTypeSpec_* spec) {
	new (memory) T(typeInfo, spec);
}

template<typename T>
void typeConstructor_Source0(void* memory) {
	typeConstructor_Impl<T>(memory, reinterpret_cast<GoTypeInfo*>(dummyAddr0), reinterpret_cast<void*>(dummyAddr1));
	asm volatile ("nop\n nop\n nop\n nop");
}

template<typename T>
void typeConstructor_Source1(void* memory) {
	typeConstructor_Impl<T>(memory, reinterpret_cast<GoTypeInfo*>(dummyAddr2), reinterpret_cast<void*>(dummyAddr3));
	asm volatile ("nop\n nop\n nop\n nop");
}

template <typename T>
auto constructorForType(GoTypeInfo* typeInfo, GoTypeSpec_* spec) {
//	qDebug() << "Assembling constructor for a simple type";
	auto sourceConstructor0 = reinterpret_cast<uint8_t*>(&typeConstructor_Source0<T>);
	auto sourceConstructor1 = reinterpret_cast<uint8_t*>(&typeConstructor_Source1<T>);
	return reinterpret_cast<void (*)(void*)>(assembleConstructor<T>(typeInfo, spec, sourceConstructor0, sourceConstructor1));
}

template<typename T>
QObject* NOINLINE singletonConstructor_Impl(QQmlEngine* qmlEngine, QJSEngine* jsEngine, GoTypeInfo* typeInfo, GoTypeSpec_* spec) {
	QObject* singleton = new T(typeInfo, spec);
    QQmlEngine::setContextForObject(singleton, qmlEngine->rootContext());
    return singleton;
}

template<typename T>
QObject* singletonConstructor_Source0(QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
	auto singleton = singletonConstructor_Impl<T>(qmlEngine, jsEngine, reinterpret_cast<GoTypeInfo*>(dummyAddr0), reinterpret_cast<GoTypeSpec_*>(dummyAddr1));
	asm volatile ("nop\n nop\n nop\n nop");
    return singleton;
}

template<typename T>
QObject* singletonConstructor_Source1(QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
	auto singleton = singletonConstructor_Impl<T>(qmlEngine, jsEngine, reinterpret_cast<GoTypeInfo*>(dummyAddr2), reinterpret_cast<GoTypeSpec_*>(dummyAddr3));
    asm volatile ("nop\n nop\n nop\n nop");
    return singleton;
}

template <typename T>
auto constructorForSingleton(GoTypeInfo* typeInfo, GoTypeSpec_* spec) {
//	qDebug() << "Assembling constructor for a singleton";
	auto sourceConstructor0 = reinterpret_cast<uint8_t*>(&singletonConstructor_Source0<T>);
	auto sourceConstructor1 = reinterpret_cast<uint8_t*>(&singletonConstructor_Source1<T>);
	return reinterpret_cast<QObject* (*)(QQmlEngine*, QJSEngine*)>(assembleConstructor<T>(typeInfo, spec, sourceConstructor0, sourceConstructor1));
}

template <typename T>
class GoValueQMLDestructible : public T {
public:
	GoValueQMLDestructible(GoTypeInfo* typeInfo, GoTypeSpec_* spec) : T(typeInfo, spec) {}
	virtual ~GoValueQMLDestructible() {
		QQmlPrivate::qdeclarativeelement_destructor(this);
	}
};

template<typename T>
QQmlPrivate::RegisterType makeRegisterType(const char* uri, int versionMajor, int versionMinor, const char* qmlName, GoTypeInfo* typeInfo, GoTypeSpec_* spec) {
	char* typeName = typeInfo->typeName;
	int len = strlen(typeName)-1;  //without null-terminator
    auto pointerName = QByteArray::fromRawData(typeName, len).append("*");
    auto listName = QByteArray::fromRawData("QQmlListProperty<", 17).append(QByteArray::fromRawData(typeName, len)).append(">");

	return QQmlPrivate::RegisterType{
        0,  //version

        qRegisterNormalizedMetaType<T*>(pointerName.constData()),   //typeId
        qRegisterNormalizedMetaType<QQmlListProperty<T>>(listName.constData()), //listId
        sizeof(GoValueQMLDestructible<T>), constructorForType<GoValueQMLDestructible<T>>(typeInfo, spec),  //size, creation func
        QString(),  //creation forbidden reason

        uri, versionMajor, versionMinor, qmlName, metaObjectFor(typeInfo),

        nullptr,    //attached properties func
        nullptr,    //attached properties metaobject

        QQmlPrivate::StaticCastSelector<T, QQmlParserStatus>::cast(),
        QQmlPrivate::StaticCastSelector<T, QQmlPropertyValueSource>::cast(),
        QQmlPrivate::StaticCastSelector<T, QQmlPropertyValueInterceptor>::cast(),

        nullptr, nullptr,   //extension creation func, extension metaobject

        nullptr,  //custom parser
        0   //revision
    };
}

int registerType(const char* uri, int versionMajor, int versionMinor, const char* qmlName, GoTypeInfo* typeInfo, GoTypeSpec_* spec) {
	QQmlPrivate::RegisterType type;
	if (!typeInfo->paint) {
		type = makeRegisterType<GoValueType>(uri, versionMajor, versionMinor, qmlName, typeInfo, spec);
    } else {
		type = makeRegisterType<GoPaintedValueType>(uri, versionMajor, versionMinor, qmlName, typeInfo, spec);
    }

	return QQmlPrivate::qmlregister(QQmlPrivate::TypeRegistration, &type);
}

template<typename T>
QQmlPrivate::RegisterSingletonType makeRegisterSingletonType(const char* uri, int versionMajor, int versionMinor, const char* qmlName, GoTypeInfo* typeInfo, GoTypeSpec_* spec) {
	char* typeName = typeInfo->typeName;
	int len = strlen(typeName)-1;  //without null-terminator
    auto pointerName = QByteArray::fromRawData(typeName, len).append("*");

	return QQmlPrivate::RegisterSingletonType{
        2, //version

        uri, versionMajor, versionMinor, qmlName,

        nullptr, constructorForSingleton<T>(typeInfo, spec), metaObjectFor(typeInfo), qRegisterNormalizedMetaType<T*>(pointerName.constData()), 0  //scriptAPI, qobjectAPI, metaobject, typeId, revision
    };
}

int registerSingleton(const char* uri, int versionMajor, int versionMinor, const char* qmlName, GoTypeInfo* typeInfo, GoTypeSpec_* spec) {
	QQmlPrivate::RegisterSingletonType api;
    if (!typeInfo->paint) {
        api = makeRegisterSingletonType<GoValueType>(uri, versionMajor, versionMinor, qmlName, typeInfo, spec);
    } else {
        api = makeRegisterSingletonType<GoPaintedValueType>(uri, versionMajor, versionMinor, qmlName, typeInfo, spec);
    }

    return QQmlPrivate::qmlregister(QQmlPrivate::SingletonRegistration, &api);
}

#undef NOINLINE

// vim:sw=4:st=4:et:ft=cpp
