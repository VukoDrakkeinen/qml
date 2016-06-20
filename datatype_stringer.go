package qml

// #include "capi.h"
import "C"

import "fmt"

var (
	dataTypeName = [...]string{
		"UnknownInvalid", "StringBoolNumberNumberINumberUUintptrColorTime", "ListMap", "AnyMethod",
	}
	dataTypeIndex = [...][]uint8{{7, 14}, {6, 10, 16, 23, 30, 32, 37, 42, 46}, {4, 7}, {3, 9}}
)

//todo: write a test for this
func (dt C.DataType) String() string {
	group := 0
	var ranges = [...]C.DataType{10, 100, 200, 300}
	for group := len(dataTypeIndex) - 1; group >= 0; group-- {
		r := ranges[group]
		if dt >= r {
			if dt >= C.DataType(len(dataTypeIndex[group]))+r {
				return fmt.Sprintf("C.DataValue(%d)", int(dt))
			}
			dt -= r
			break
		}
	}
	hi := dataTypeIndex[group][int(dt)]
	lo := uint8(0)
	if dt > C.DataType(0) {
		lo = dataTypeIndex[group][int(dt)-1]
	}
	return dataTypeName[group][lo:hi]
}
