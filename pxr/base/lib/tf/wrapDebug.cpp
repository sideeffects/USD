//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "pxr/base/tf/debug.h"
#include "pxr/base/tf/pyUtils.h"

#include <boost/python/class.hpp>
#include <boost/python/object.hpp>

using namespace boost::python;

PXR_NAMESPACE_USING_DIRECTIVE

void PyErr_Check(void *X)
{
    if (X == nullptr) {
	if (!PyErr_Occurred())
	    TfPyThrowRuntimeError("Could not allocate memory for object");

	PyObject *ptype, *pvalue, *ptraceback, *bytes;
	PyErr_Fetch(&ptype, &pvalue, &ptraceback);

	if (pvalue == nullptr)
	    return;

	if (PyUnicode_Check(pvalue)) {
	    bytes = PyUnicode_AsEncodedString(pvalue, "UTF-8", "strict");

	    if (bytes == nullptr)
	        return;

	    char *pStrErrorMessage = PyBytes_AS_STRING(bytes);
	    TfPyThrowRuntimeError(pStrErrorMessage);
	}
    }
}

namespace {

static void
_SetOutputFile(object const &file)
{
    FILE *fp = nullptr;

#if PY_MAJOR_VERSION < 3
    fp = PyFile_AsFile(file.ptr());

    if (!fp)
        TfPyThrowTypeError("expected file object");

    // On Windows the FILE* for sys.__stdout__ and __stderr__ will not
    // match stdout and stderr if there's redirection but output will
    // go to the same handle.  To satisfy TfDebug::SetOutputFile() we
    // translate the FILE pointers here.
    if (fp != stdout && fp != stderr) {
        object sys(handle<>(PyImport_ImportModule("sys")));
        if (PyFile_AsFile(object(sys.attr("__stdout__")).ptr()) == fp) {
            fp = stdout;
        }
        else if (PyFile_AsFile(object(sys.attr("__stderr__")).ptr()) == fp) {
            fp = stderr;
        }
    }
#else
    // Every file-like open in Python 3 inherits from io.IOBase, so we
    // check if the object subclasses it.
    PyObject *module = PyImport_ImportModule("io");
    PyErr_Check((void*)&module);

    PyObject *moduleDict = PyModule_GetDict(module);
    PyErr_Check((void*)&moduleDict);

    PyObject *baseclass = PyDict_GetItemString(moduleDict, "IOBase");
    PyErr_Check((void*)&baseclass);

    if (!PyObject_IsInstance(file.ptr(), baseclass))
        TfPyThrowTypeError("expected file object");

    Py_DECREF(baseclass);

    object sys(handle<>(PyImport_ImportModule("sys")));
    if (object(sys.attr("__stdout__")).ptr() == file.ptr()) {
       fp = stdout;
    }
    else if (object(sys.attr("__stderr__")).ptr() == file.ptr()) {
       fp = stderr;
    }
    else {
        if (PyObject_HasAttrString(file.ptr(), "fileno")) {
            long fd = PyLong_AsLong(PyObject_CallMethod(file.ptr(), "fileno", NULL));

            if (fd == 1) {
                fp = stdout;
            }
            else if (fd == 2) {
                fp = stderr;
            }
        }
    }
#endif

    TfDebug::SetOutputFile(fp);
}

} // anonymous namespace 

void wrapDebug()
{
    typedef TfDebug This;

    class_<This>("Debug", no_init)
        .def("SetDebugSymbolsByName", &This::SetDebugSymbolsByName,
             ( arg("pattern"), arg("value") ))
        .staticmethod("SetDebugSymbolsByName")

        .def("IsDebugSymbolNameEnabled", &This::IsDebugSymbolNameEnabled)
        .staticmethod("IsDebugSymbolNameEnabled")

        .def("GetDebugSymbolDescriptions", &This::GetDebugSymbolDescriptions)
        .staticmethod("GetDebugSymbolDescriptions")

        .def("GetDebugSymbolNames", &This::GetDebugSymbolNames)
        .staticmethod("GetDebugSymbolNames")

        .def("GetDebugSymbolDescription", &This::GetDebugSymbolDescription)
        .staticmethod("GetDebugSymbolDescription")

        .def("SetOutputFile", _SetOutputFile)
        .staticmethod("SetOutputFile")

        ;
}
