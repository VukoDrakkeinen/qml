package qml

// #include <stdlib.h>
// #include "capi.h"
import "C"

import (
	"bytes"
	"fmt"
	"image/color"
	"reflect"
	"strings"
	"time"
	"unicode"
	"unsafe"
)

var (
	ptrSize = C.size_t(unsafe.Sizeof(uintptr(0)))

	nilPtr     = unsafe.Pointer(uintptr(0))
	nilCharPtr = (*C.char)(nilPtr)

	typeString     = reflect.TypeOf("")
	typeBool       = reflect.TypeOf(false)
	typeInt        = reflect.TypeOf(int(0))
	typeUint       = reflect.TypeOf(uint(0))
	typeInt64      = reflect.TypeOf(int64(0))
	typeInt32      = reflect.TypeOf(int32(0))
	typeUint64     = reflect.TypeOf(uint64(0))
	typeUint32     = reflect.TypeOf(uint32(0))
	typeUintptr    = reflect.TypeOf(uintptr(0))
	typeFloat64    = reflect.TypeOf(float64(0))
	typeFloat32    = reflect.TypeOf(float32(0))
	typeIface      = reflect.TypeOf(new(interface{})).Elem()
	typeRGBA       = reflect.TypeOf(color.RGBA{})
	typeTime       = reflect.TypeOf(time.Time{})
	typeObjSlice   = reflect.TypeOf([]Object(nil))
	typeObject     = reflect.TypeOf([]Object(nil)).Elem()
	typePainter    = reflect.TypeOf(&Painter{})
	typeList       = reflect.TypeOf(&List{})
	typeMap        = reflect.TypeOf(&Map{})
	typeGenericMap = reflect.TypeOf(map[string]interface{}(nil))

	ifaceStringer = reflect.TypeOf((*fmt.Stringer)(nil)).Elem()
)

func kindToType(k reflect.Kind) reflect.Type {
	switch k {
	case reflect.String:
		return typeString
	case reflect.Bool:
		return typeBool
	case reflect.Int, reflect.Int8, reflect.Int16:
		return typeInt
	case reflect.Int32:
		return typeInt32
	case reflect.Int64:
		return typeInt64
	case reflect.Uint, reflect.Uint8, reflect.Uint16:
		return typeUint
	case reflect.Uint32:
		return typeUint32
	case reflect.Uint64:
		return typeUint64
	case reflect.Uintptr:
		return typeUintptr
	case reflect.Float32:
		return typeFloat32
	case reflect.Float64:
		return typeFloat64
	default:
		panic(fmt.Sprintf("Unhandled value kind %v passed to QML. Use other kinds or implement a Marshaller interface.", k))
	}
}

//TODO: docs
func arrayPtr(slice interface{}) (internal unsafe.Pointer) {
	val := reflect.ValueOf(slice)
	for kind := val.Kind(); kind == reflect.Interface || kind == reflect.Ptr; {
		val = val.Elem()
	}
	if val.Kind() == reflect.Slice {
		return unsafe.Pointer(val.Pointer())
	}
	panic("arrayPtr: invalid type, expected Slice, got " + val.Kind().String())
}

type Marshaller interface {
	MarshalQML() interface{}
}

type Unmarshaller interface {
	UnmarshalQML(data interface{}) error
}

// used by packDataValue to convert custom primitive types with no methods
func byKind(k reflect.Kind) reflect.Type {
	switch k {
	case reflect.String:
		return typeString
	case reflect.Bool:
		return typeBool
	case reflect.Int8, reflect.Int16, reflect.Int32:
		return typeInt32
	case reflect.Uint8, reflect.Uint16, reflect.Uint32:
		return typeUint32
	case reflect.Int:
		if intIs64 {
			return typeInt64
		} else {
			return typeInt32
		}
	case reflect.Uint:
		if intIs64 {
			return typeUint64
		} else {
			return typeUint32
		}
	case reflect.Int64:
		return typeInt64
	case reflect.Uint64:
		return typeUint64
	case reflect.Float32, reflect.Float64:
		return typeFloat64
	default:
		panic(fmt.Sprintf("Unhandled value kind %v passed to QML. Use other kinds or implement a Marshaller interface.", k))
	}
}

// packDataValue packs the provided Go value into a C.DataValue for
// shiping into C++ land.
//
// For simple types (bool, int, etc) value is converted into a
// native C++ value. For anything else, including cases when value
// has a type that has an underlying simple type, the Go value itself
// is encapsulated into a C++ wrapper so that field access and method
// calls work.
//
// This must be run from the main GUI thread due to the cases where
// calling wrapGoValue is necessary.
func packDataValue(value interface{}, engine *Engine, owner valueOwner) (dv C.DataValue) {
	data := unsafe.Pointer(&dv.data)
	if value == nil {
		return C.DataValue{dataType: C.DTInvalid}
	}
	switch value := value.(type) {
	case string:
		cstr, cstrlen := unsafeStringData(value)
		dv.dataType = C.DTString
		*(**C.char)(data) = cstr
		dv.len = cstrlen
	case bool:
		dv.dataType = C.DTBool
		*(*bool)(data) = value
	case int8:
		return packDataValue(int32(value), engine, owner)
	case int16:
		return packDataValue(int32(value), engine, owner)
	case int32:
		dv.dataType = C.DTNumberI
		*(*int32)(data) = value
	case int64:
		if int64(value) > 9007199254740991 || int64(value) < -9007199254740991 { //2^53-1
			panic("Attempted to pass an integer value that cannot be represented in JavaScript")
		}
		return packDataValue(float64(value), engine, owner)
	case uint8:
		return packDataValue(uint32(value), engine, owner)
	case uint16:
		return packDataValue(uint32(value), engine, owner)
	case uint32:
		dv.dataType = C.DTNumberU
		*(*uint32)(data) = value
	case uint64:
		if int64(value) > 9007199254740991 { //2^53-1
			panic("Attempted to pass an integer value that cannot be represented in JavaScript")
		}
		return packDataValue(float64(value), engine, owner)
	case float32:
		return packDataValue(float64(value), engine, owner)
	case float64:
		return C.DataValue{dataType: C.DTNumber, data: *(*[8]C.char)(unsafe.Pointer(&value))}
	case uintptr:
		dv.dataType = C.DTUintptr
		*(*uintptr)(data) = value
	//case []Object:	//todo: need some parent data
	//	dvalue.dataType = C.DTListProperty
	//	*(*unsafe.Pointer)(data) = C.newListProperty(foldp, C.intptr_t(reflectIndex), C.intptr_t(setIndex))
	case Object:
		dv.dataType = C.DTObject
		*(*uintptr)(data) = value.Addr()
	case color.RGBA:
		dv.dataType = C.DTColor
		*(*uint32)(data) = uint32(value.A)<<24 | uint32(value.R)<<16 | uint32(value.G)<<8 | uint32(value.B)
	case time.Time:
		sec := value.Unix()
		msec := sec*1000 + int64(value.Nanosecond()/1000)
		if msec/1000 < sec {
			panic("Attempted to pass a time value that cannot be represented in Qt")
		}
		return C.DataValue{dataType: C.DTTime, data: *(*[8]C.char)(unsafe.Pointer(&msec))}
	case Marshaller:
		return packDataValue(value.MarshalQML(), engine, owner)
	case reflect.Value:
		return packDataValue(value.Interface(), engine, owner)
	default:
		switch container := deref(reflect.ValueOf(value)); container.Kind() { //todo: a container wrapper, so we don't have to convert all bazillion values (requires an upstream Qt patch)
		case reflect.Slice:
			slen := container.Len()
			dvArray := C.malloc(C.size_t(C.sizeof_DataValue * slen))
			dvSlice := (*[1 << 30]C.DataValue)(dvArray)[:slen:slen]
			for i := 0; i < slen; i++ {
				dvSlice[i] = packDataValue(container.Index(i).Interface(), engine, jsOwner)
			}
			dv.dataType = C.DTList
			*(*unsafe.Pointer)(data) = dvArray
			dv.len = C.int(slen)
		case reflect.Map:
			keys := container.MapKeys()
			mlen := len(keys) * 2
			keyConvert := func(key reflect.Value) interface{} {
				return key.Interface()
			}
			if mlen > 0 {
				if key := keys[0]; key.Kind() == reflect.Struct { //if it's a struct...
					keyType := key.Type()
					if keyType.Implements(ifaceStringer) { //...that also implements a Stringer...
						keyConvert = func(key reflect.Value) interface{} {
							return key.Interface().(fmt.Stringer).String() //...then convert it into a string
						}
					} else {
						panic(fmt.Sprintf("Cannot convert %s (%#v) into string to use as a QML map key", keyType, key.Interface()))
					}
				}
			}
			dvArray := C.malloc(C.size_t(C.sizeof_DataValue * mlen))
			dvSlice := (*[1 << 30]C.DataValue)(dvArray)[:mlen:mlen]
			for i := 0; i < mlen; i += 2 {
				key := keys[i/2]
				dvSlice[i+0] = packDataValue(keyConvert(key), engine, jsOwner)
				dvSlice[i+1] = packDataValue(container.MapIndex(key).Interface(), engine, jsOwner)
			}
			dv.dataType = C.DTMap
			*(*unsafe.Pointer)(data) = dvArray
			dv.len = C.int(mlen)
		default:
			dv.dataType = C.DTObject
			if obj, ok := value.(Object); ok {
				*(*unsafe.Pointer)(data) = obj.Common().addr
			} else if vtype := container.Type(); vtype.Kind() == reflect.Struct || reflect.PtrTo(vtype).NumMethod() > 0 { //wrap only if the type has at least one method or field
				*(*unsafe.Pointer)(data) = wrapGoValue(engine, value, owner)
			} else {
				return packDataValue(container.Convert(byKind(vtype.Kind())).Interface(), engine, owner)
			}
		}
	}
	return dv
}

// unpackDataValue converts a value shipped by C++ into a native Go value.
//
// HEADS UP: This is considered safe to be run out of the main GUI thread.
//           If that changes, fix the call sites.
func unpackDataValue(dv C.DataValue, engine *Engine) interface{} { // TODO Handle byte slices.
	data := unsafe.Pointer(&dv.data)
	switch dv.dataType {
	case C.DTString:
		// TODO If we move all unpackDataValue calls to the GUI thread,
		// can we get rid of this allocation somehow?
		s := C.GoStringN(*(**C.char)(data), dv.len)
		C.free(unsafe.Pointer(*(**C.char)(data)))
		return s
	case C.DTBool:
		return *(*bool)(data)
	case C.DTNumber:
		return *(*float64)(data)
	case C.DTNumberI:
		return *(*int32)(data)
	case C.DTNumberU:
		return *(*uint32)(data)
	case C.DTUintptr:
		return *(*uintptr)(data)
	case C.DTColor:
		var c uint32 = *(*uint32)(data)
		return color.RGBA{byte(c >> 16), byte(c >> 8), byte(c), byte(c >> 24)}
	case C.DTTime:
		msec := *(*int64)(data)
		sec := msec / 1000
		nsec := msec % 1000 * 1000
		return time.Unix(sec, nsec)
	case C.DTGoAddr:
		// ObjectByName also does this fold conversion, to have access
		// to the cvalue. Perhaps the fold should be returned.
		fold := (*(**valueFold)(data))
		ensureEngine(engine.addr, unsafe.Pointer(fold))
		return fold.gvalue
	case C.DTInvalid:
		return nil
	case C.DTObject:
		// TODO Would be good to preserve identity on the Go side. See initGoType as well.
		obj := &Common{
			engine: engine,
			addr:   *(*unsafe.Pointer)(data),
		}
		if len(converters) > 0 {
			// TODO Embed the type name in DataValue to drop these calls.
			typeName := obj.TypeName()
			if typeName == "PlainObject" {
				typeName = strings.TrimRight(obj.String("plainType"), "&*")
				if strings.HasPrefix(typeName, "const ") {
					typeName = typeName[6:]
				}
			}
			if f, ok := converters[typeName]; ok {
				return f(engine, obj)
			}
		}
		return obj
	case C.DTList, C.DTMap:
		dvlist := (*[1 << 30]C.DataValue)(*(*unsafe.Pointer)(data))[:dv.len]
		containerData := make([]interface{}, len(dvlist))
		for i := range containerData {
			containerData[i] = unpackDataValue(dvlist[i], engine)
		}
		C.free(*(*unsafe.Pointer)(data))
		if dv.dataType == C.DTList {
			return &List{containerData}
		} else {
			return &Map{containerData}
		}
	}
	panic(fmt.Sprintf("unsupported data type: %v", dv.dataType))
}

// Compare against the specific types rather than their kind.
// Custom types may have methods that must be supported.
func dataTypeOf(typ reflect.Type) C.DataType {
	switch typ {
	case typeString:
		return C.DTString
	case typeBool:
		return C.DTBool
	case typeInt, typeInt32:
		return C.DTNumberI
	case typeUint, typeUint32:
		return C.DTNumberU
	case typeFloat32, typeFloat64, typeInt64, typeUint64:
		return C.DTNumber
	case typeIface:
		return C.DTAny
	case typeRGBA:
		return C.DTColor
	case typeTime:
		return C.DTTime
	case typeObjSlice:
		return C.DTListProperty
	}
	return C.DTObject
}

var typeInfoSize = C.size_t(unsafe.Sizeof(C.GoTypeInfo{}))
var memberInfoSize = C.size_t(unsafe.Sizeof(C.GoMemberInfo{}))

var typeInfoCache = make(map[reflect.Type]*C.GoTypeInfo)
var reflectIndices = make([][]int, 0, 256)

func appendLoweredName(buf []byte, name string) []byte {
	var prev rune
	var previ int
	for i, rune := range name {
		if !unicode.IsUpper(rune) {
			if previ == 0 || rune == '_' {
				prev = unicode.ToLower(prev)
			}
			buf = append(buf, string(prev)...)
			buf = append(buf, name[i:]...)
			return buf
		}
		if i > 0 {
			buf = append(buf, string(unicode.ToLower(prev))...)
		}
		previ, prev = i, rune
	}
	return append(buf, string(unicode.ToLower(prev))...)
}

func typeFields(t reflect.Type) (fields []reflect.StructField) {
	type fieldScan struct {
		t   reflect.Type
		idx []int
	}
	currentLvl := []fieldScan{}    //Holy hell, thanks, Go Team, for teaching me this straightforward way
	nextLvl := []fieldScan{{t: t}} //to transform certain cases of recursive code into iterative! -Vuko

	names := make(map[string]struct{})
	if t.Kind() == reflect.Struct {
		fields = make([]reflect.StructField, 0, t.NumField()*3/2)
	}

	for len(nextLvl) > 0 {
		currentLvl, nextLvl = nextLvl, currentLvl[:0]
		for _, scan := range currentLvl {
			t := scan.t
			if t.Kind() != reflect.Struct {
				continue
			}

			numField := t.NumField()
			for i := 0; i < numField; i++ {
				f := t.Field(i)
				if _, shadowed := names[f.Name]; shadowed {
					continue
				}
				names[f.Name] = struct{}{}

				idx := append(append(make([]int, 0, len(scan.idx)+1), scan.idx...), i)
				f.Index = idx
				fields = append(fields, f)
				if f.Anonymous {
					nextLvl = append(nextLvl, fieldScan{t: f.Type, idx: idx})
				}
			}
		}
	}
	return fields
}

func typeValueOfReflectIndex(t reflect.Type) (reflectIndex C.int) {
	return -C.int(t.Kind())
}

func callJavascriptValueOf(v reflect.Value, reflectIndex C.int) interface{} {
	v = deref(v)
	var t reflect.Type
	switch reflect.Kind(-reflectIndex) {
	case reflect.String:
		t = typeString
	case reflect.Bool:
		t = typeBool
	case reflect.Int:
		t = typeInt
	case reflect.Int64:
		t = typeInt64
	case reflect.Int32:
		t = typeInt32
	case reflect.Uint:
		t = typeUint
	case reflect.Uint64:
		t = typeUint64
	case reflect.Uint32:
		t = typeUint32
	case reflect.Uintptr:
		t = typeUintptr
	case reflect.Float32:
		t = typeFloat32
	case reflect.Float64:
		t = typeFloat64
	default:
		panic(fmt.Sprintf("Unhandled type %v in ValueOf: %v", v.Type(), v.Interface()))
	}
	fmt.Printf("jsValueOf: %v %[1]T\n", v.Convert(t).Interface())
	return v.Convert(t).Interface()
}

const (
	ToStringStringer = -C.int(reflect.UnsafePointer + 128 + iota)
	ToStringValueOfer
	ToStringAutoValueOf
)

func typeToStringReflectIndex(t reflect.Type) (reflectIndex C.int) { //todo: implements ValuefOfer
	if t.Implements(ifaceStringer) {
		return ToStringStringer
	} else {
		return -(-ToStringAutoValueOf | typeValueOfReflectIndex(t)<<8)
	}
}

func callJavascriptToString(v reflect.Value, reflectIndex C.int) string {
	v = deref(v)
	switch -(-reflectIndex & 0xFF) {
	case ToStringStringer:
		return v.Interface().(fmt.Stringer).String()
	case ToStringAutoValueOf:
		return fmt.Sprintf("%v", callJavascriptValueOf(v, -reflectIndex>>8))
	default:
		panic(fmt.Sprintf("Invalid reflect %v (%v | %v) index in jsToString", reflectIndex, -(-reflectIndex & 0xFF), -reflectIndex>>8))
	}
}

const mnameToString = "toString"
const msignatureToString = "toString()"
const mnameValueOf = "ValueOf"
const msignatureValueOf = "valueOf()"

func typeInfo(v interface{}) *C.GoTypeInfo {
	vt := reflect.TypeOf(v)
	for vt.Kind() == reflect.Ptr {
		vt = vt.Elem()
	}

	typeInfo := typeInfoCache[vt]
	if typeInfo != nil {
		return typeInfo
	}

	typeInfo = (*C.GoTypeInfo)(C.malloc(typeInfoSize))
	typeInfo.typeName = C.CString(vt.Name())
	typeInfo.metaObject = nilPtr
	typeInfo.paint = (*C.GoMemberInfo)(nilPtr)

	setters := make(map[string]int)
	getters := make(map[string]int)
	var hasOwnValueOf bool

	vtptr := reflect.PtrTo(vt)

	switch vt.Kind() {
	case reflect.Chan:
		panic("Trying to use a channel in QML? Does this even make sense...?") //todo: better messages
	case reflect.UnsafePointer:
		panic("UnsafePointer? Don't.") //todo: better.
	case reflect.Func, reflect.Complex64, reflect.Complex128:
		panic(fmt.Sprintf("Handling of %s (%#v) is incomplete; please report to the developers", vt, v))
	}

	fields := typeFields(vt) //gather info on all fields (embedded and anonymous included)
	numField := len(fields)
	numMethod := vtptr.NumMethod()
	privateFields := 0
	addedMethods := 0

	// struct { FooBar T; Baz T } => "fooBar\0baz\0"
	names := make([]byte, 0, 512)
	for _, field := range fields {
		if field.PkgPath != "" {
			privateFields++
			continue // not exported
		}
		names = append(appendLoweredName(names, field.Name), 0)
	}
	methodsIndices := make([]int, 0, numMethod)
	for i := 0; i < numMethod; i++ {
		method := vtptr.Method(i)
		if method.PkgPath != "" {
			continue
		}

		// Track setters and getters.
		if len(method.Name) > 3 && method.Name[:3] == "Set" && method.Type.NumIn() == 2 {
			setters[method.Name[3:]] = i
		} else if method.Type.NumIn() == 1 && method.Type.NumOut() == 1 {
			getters[method.Name] = i //possibly a getter
			hasOwnValueOf = hasOwnValueOf || method.Name == mnameValueOf
		} else {
			methodsIndices = append(methodsIndices, i) //definitely a method
		}
	}
	gettersIndices := make([]int, 0, len(getters))
	for methodName, i := range getters {
		if _, hasSetter := setters[methodName]; hasSetter {
			names = append(appendLoweredName(names, methodName), 0)
			gettersIndices = append(gettersIndices, i)
		} else {
			delete(getters, methodName)                //can't have a getter without a setter
			methodsIndices = append(methodsIndices, i) //so a method after all
		}
	}
	for _, i := range methodsIndices {
		names = append(appendLoweredName(names, vtptr.Method(i).Name), 0)
	}
	if !hasOwnValueOf && vt.Kind() != reflect.Struct {
		names = append(appendLoweredName(names, mnameValueOf), 0)
		addedMethods++
	}
	{
		names = append(append(names, []byte(mnameToString)...), 0)
		addedMethods++
	}

	typeInfo.memberNames = C.CString(string(names))

	// Assemble information on members.
	membersLen := numField - privateFields + len(getters) + len(methodsIndices) + addedMethods
	mInfosI := uintptr(0)
	mNamesI := uintptr(0)
	members := uintptr(C.malloc(memberInfoSize * C.size_t(membersLen)))
	mnames := uintptr(unsafe.Pointer(typeInfo.memberNames))
	nextMemberInfo := func(name string) *C.GoMemberInfo {
		memberInfo := (*C.GoMemberInfo)(unsafe.Pointer(members + uintptr(memberInfoSize)*mInfosI))
		memberInfo.memberName = (*C.char)(unsafe.Pointer(mnames + mNamesI))
		mInfosI += 1
		mNamesI += uintptr(len(name) + 1)
		return memberInfo
	}
	for _, field := range fields {
		if field.PkgPath != "" {
			continue // not exported
		}
		reflectIndices = append(reflectIndices, field.Index)
		memberInfo := nextMemberInfo(field.Name)
		memberInfo.memberType = dataTypeOf(field.Type)
		memberInfo.reflectIndex = C.int(len(reflectIndices) - 1)
		memberInfo.reflectGetIndex = -1
		memberInfo.addrOffset = C.int(field.Offset)
		if methodIndex, ok := setters[field.Name]; !ok {
			memberInfo.reflectSetIndex = -1
		} else {
			memberInfo.reflectSetIndex = C.int(methodIndex)
		}
	}
	for _, i := range gettersIndices {
		method := vtptr.Method(i)
		memberInfo := nextMemberInfo(method.Name)
		memberInfo.memberType = dataTypeOf(method.Type.Out(0))
		memberInfo.reflectIndex = -1
		memberInfo.reflectGetIndex = C.int(getters[method.Name])
		memberInfo.reflectSetIndex = C.int(setters[method.Name])
		memberInfo.addrOffset = 0
	}
	for _, i := range methodsIndices {
		method := vtptr.Method(i)
		memberInfo := nextMemberInfo(method.Name)
		memberInfo.memberType = C.DTMethod
		memberInfo.reflectIndex = C.int(i)
		memberInfo.reflectGetIndex = -1
		memberInfo.reflectSetIndex = -1
		memberInfo.addrOffset = 0
		signature, result := methodQtSignature(method) // TODO The signature data might be embedded in the same array as the member names.
		memberInfo.methodSignature = C.CString(signature)
		memberInfo.resultSignature = C.CString(result)
		// TODO Sort out methods with a variable number of arguments.
		memberInfo.numIn = C.int(method.Type.NumIn() - 1) // It's called while bound, so drop the receiver.
		memberInfo.numOut = C.int(method.Type.NumOut())

		if method.Name == "Paint" && memberInfo.numIn == 1 && memberInfo.numOut == 0 && method.Type.In(1) == typePainter {
			typeInfo.paint = memberInfo
		}
	}
	if !hasOwnValueOf && vt.Kind() != reflect.Struct {
		memberInfo := nextMemberInfo(mnameValueOf)
		memberInfo.memberType = C.DTMethod
		memberInfo.reflectIndex = typeValueOfReflectIndex(vt)
		memberInfo.reflectGetIndex = -1
		memberInfo.reflectSetIndex = -1
		memberInfo.addrOffset = 0
		memberInfo.methodSignature = C.CString(msignatureValueOf)
		memberInfo.resultSignature = C.CString("QVariant")
		memberInfo.numIn = 0
		memberInfo.numOut = 1
	}
	{
		memberInfo := nextMemberInfo(mnameToString)
		memberInfo.memberType = C.DTMethod
		memberInfo.reflectIndex = typeToStringReflectIndex(vt)
		memberInfo.reflectGetIndex = -1
		memberInfo.reflectSetIndex = -1
		memberInfo.addrOffset = 0
		memberInfo.methodSignature = C.CString(msignatureToString)
		memberInfo.resultSignature = C.CString("QVariant")
		memberInfo.numIn = 0
		memberInfo.numOut = 1
	}

	typeInfo.members = (*C.GoMemberInfo)(unsafe.Pointer(members))
	typeInfo.membersLen = C.int(membersLen)

	typeInfo.fields = typeInfo.members
	typeInfo.fieldsLen = C.int(numField - privateFields + len(getters))
	typeInfo.methods = (*C.GoMemberInfo)(unsafe.Pointer(members + uintptr(memberInfoSize)*uintptr(typeInfo.fieldsLen)))
	typeInfo.methodsLen = C.int(len(methodsIndices) + addedMethods)

	if int(mInfosI) != membersLen {
		panic("assert: used more space than allocated for member names")
	}
	if int(mNamesI) != len(names) {
		panic("assert: allocated names buffer doesn't match used space")
	}
	if typeInfo.fieldsLen+typeInfo.methodsLen != typeInfo.membersLen {
		panic("assert: members lengths are inconsistent")
	}

	typeInfoCache[vt] = typeInfo
	return typeInfo
}

func methodQtSignature(method reflect.Method) (signature, result string) {
	var buf bytes.Buffer
	for i, rune := range method.Name {
		if i == 0 {
			buf.WriteRune(unicode.ToLower(rune))
		} else {
			buf.WriteString(method.Name[i:])
			break
		}
	}
	buf.WriteByte('(')
	n := method.Type.NumIn()
	for i := 1; i < n; i++ {
		if i > 1 {
			buf.WriteByte(',')
		}
		buf.WriteString("QVariant")
	}
	buf.WriteByte(')')
	signature = buf.String()

	switch method.Type.NumOut() {
	case 0:
		// keep it as ""
	case 1:
		result = "QVariant"
	default:
		result = "QVariantList"
	}
	return signature, result
}

func hashable(value reflect.Value) (hashable bool) {
	return value.Type().Comparable()
}

// unsafeString returns a Go string backed by C data.
//
// If the C data is deallocated or moved, the string will be
// invalid and will crash the program if used. As such, the
// resulting string must only be used inside the implementation
// of the qml package and while the life time of the C data
// is guaranteed.
func unsafeString(data *C.char, size C.int) string {
	var s string
	sh := (*reflect.StringHeader)(unsafe.Pointer(&s))
	sh.Data = uintptr(unsafe.Pointer(data))
	sh.Len = int(size)
	return s
}

// unsafeStringData returns a C string backed by Go data. The C
// string is NOT null-terminated, so its length must be taken
// into account.
//
// If the s Go string is garbage collected, the returned C data
// will be invalid and will crash the program if used. As such,
// the resulting data must only be used inside the implementation
// of the qml package and while the life time of the Go string
// is guaranteed.
func unsafeStringData(s string) (*C.char, C.int) {
	return *(**C.char)(unsafe.Pointer(&s)), C.int(len(s))
}

// unsafeBytesData returns a C string backed by Go data. The C
// string is NOT null-terminated, so its length must be taken
// into account.
//
// If the array backing the b Go slice is garbage collected, the
// returned C data will be invalid and will crash the program if
// used. As such, the resulting data must only be used inside the
// implementation of the qml package and while the life time of
// the Go array is guaranteed.
func unsafeBytesData(b []byte) (*C.char, C.int) {
	return *(**C.char)(unsafe.Pointer(&b)), C.int(len(b))
}
