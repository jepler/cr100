/* This is a Python wrapper of the vt100 headless library */

/*

   occurrences of 'xx' should be changed to something reasonable for your
   module. If your module is named foo your sourcefile should be named
   foomodule.c.

   You will probably want to delete all references to 'x_attr' and add
   your own types of attributes instead.  Maybe you want to name your
   local variables other than 'self'.  If your object type is needed in
   other files, you'll have to create a file "foobarobject.h"; see
   floatobject.h for an example. */


#include <stddef.h>

#include "Python.h"
#include "structmember.h"

#include "lw_terminal_vt100.h"
#include "hl_vt100.h"

typedef struct {
    PyObject_HEAD
    struct vt100_headless *obj;
    PyObject *changed_callback;
} VT100Object;

static PyTypeObject VT100_Type;

#define VT100Object_Check(v)      Py_IS_TYPE(v, &VT100_Type)


VT100Object **allocated;
size_t allocated_size;

/* VT100 methods */

PyDoc_STRVAR(vt100_headless_fork_doc,
"fork(progname, argv)\n\
\n\
Fork a process in a new PTY handled by an headless VT100 emulator.");

static PyObject *
VT100_fork(VT100Object *self, PyObject *args)
{
    char *progname;
    PyObject *pyargv;
    const char **argv;
    int argc;
    int i;

    if (!PyArg_ParseTuple(args, "sO:fork", &progname, &pyargv))
        return NULL;
    if (!PyList_Check(pyargv))
    {
        PyErr_SetString(PyExc_TypeError, "not a list");
        return NULL;
    }
    argc = PyList_Size(pyargv);
    for (i = 0; i < argc; i++)
    {
        PyObject *o = PyList_GetItem(pyargv, i);
        if (!PyUnicode_Check(o))
        {
            PyErr_SetString(PyExc_TypeError, "argv list must contain strings");
            return NULL;
        }
    }
    argv = PyMem_Calloc(argc + 1, sizeof(char *));
    for (i = 0; i < argc; i++)
        argv[i] = PyUnicode_AsUTF8(PyList_GetItem(pyargv, i));
    argv[i] = NULL;
    vt100_headless_fork(self->obj, progname, (char **)argv);
    PyMem_Free(argv);
    Py_RETURN_NONE;
}

PyDoc_STRVAR(vt100_headless_getlines_doc,
"getlines()\n\
\n\
Get a list of lines as currently seen by the emulator.");

static PyObject *
VT100_getlines(VT100Object *self, PyObject *Py_UNUSED(ignored))
{
    const char **lines;
    PyObject *result;

    lines = vt100_headless_getlines(self->obj);
    result = PyList_New(0);
    if (result == NULL)
        return NULL;
    for (unsigned int i = 0; i < self->obj->term->height; i++)
        PyList_Append(result, PyUnicode_FromStringAndSize(lines[i], self->obj->term->width));
    return result;
}

PyDoc_STRVAR(vt100_headless_main_loop_doc,
"main_loop()\n\
\n\
Enter the emulator main loop.");

static PyObject *
VT100_main_loop(VT100Object *self, PyObject *Py_UNUSED(ignored))
{
    vt100_headless_main_loop(self->obj);
    if (PyErr_Occurred())
        return NULL;
    Py_RETURN_NONE;
}


PyDoc_STRVAR(vt100_headless_stop_doc,
"stop()\n\
\n\
Stop emulator main loop.");

static PyObject *
VT100_stop(VT100Object *self, PyObject *Py_UNUSED(ignored))
{
    vt100_headless_stop(self->obj);
    Py_RETURN_NONE;
}


static int
vt100_add_to_allocated(VT100Object *obj)
{
    for (size_t i = 0; i < allocated_size; i++)
    {
        if (allocated[i] == NULL)
        {
            allocated[i] = obj;
            return 0;
        }
    }
    /* Out of allocated memory, realloc. */
    allocated_size *= 2;
    allocated = PyMem_Realloc(allocated, allocated_size * sizeof(VT100Object*));
    if (allocated == NULL)
    {
        PyErr_SetString(PyExc_MemoryError, "cannot allocate vt100 emulator");
        return -1;
    }
    return vt100_add_to_allocated(obj);
}

static int
vt100_del_from_allocated(VT100Object *obj)
{
    for (size_t i = 0; i < allocated_size; i++)
    {
        if (allocated[i] == obj)
        {
            allocated[i] = NULL;
            return 0;
        }
    }
    return -1;
}

VT100Object *
vt100_find_in_allocated(struct vt100_headless *obj)
{
    for (size_t i = 0; i < allocated_size; i++)
        if (allocated[i]->obj == obj)
            return allocated[i];
    return NULL;
}

void
hl_vt100_changed_cb(struct vt100_headless *this)
{
    VT100Object *obj;
    PyObject *result;

    obj = vt100_find_in_allocated(this);
    if (obj->changed_callback != NULL && obj->changed_callback != Py_None)
    {
        result = PyObject_CallNoArgs(obj->changed_callback);
        if (result == NULL)
            this->should_quit = 1;
    }
}

static int
VT100_init(VT100Object *self, PyObject *args, PyObject *kwds)
{
    self->obj = new_vt100_headless();
    vt100_add_to_allocated(self);
    self->obj->changed = hl_vt100_changed_cb;
    if (self->obj == NULL)
    {
        PyErr_SetString(PyExc_MemoryError, "cannot allocate vt100 emulator");
        return -1;
    }
    return 0;
}

static PyObject *
VT100_getwidth(VT100Object *self, void *closure)
{
    return PyLong_FromUnsignedLong(self->obj->term->width);
}

static PyObject *
VT100_getheight(VT100Object *self, void *closure)
{
    return PyLong_FromUnsignedLong(self->obj->term->height);
}

static void
VT100_dealloc(VT100Object *self)
{
    vt100_del_from_allocated(self);
    Py_XDECREF(self->changed_callback);
    delete_vt100_headless(self->obj);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyMethodDef VT100_methods[] = {
    {"fork",            (PyCFunction)VT100_fork,  METH_VARARGS, vt100_headless_fork_doc},
    {"getlines",        (PyCFunction)VT100_getlines,  METH_NOARGS, vt100_headless_getlines_doc},
    {"main_loop",        (PyCFunction)VT100_main_loop,  METH_NOARGS, vt100_headless_main_loop_doc},
    {"stop",        (PyCFunction)VT100_stop,  METH_NOARGS, vt100_headless_stop_doc},
    {NULL,              NULL}           /* sentinel */
};


static PyMemberDef VT100_members[] = {
    {"changed_callback", T_OBJECT_EX, offsetof(VT100Object, changed_callback), 0,
     "Changed Callback"},
    {NULL}  /* Sentinel */
};

static PyGetSetDef VT100_getsetters[] = {
    {"width", (getter) VT100_getwidth, NULL, "Terminal width", NULL},
    {"height", (getter) VT100_getheight, NULL, "Terminal height", NULL},
    {NULL}  /* Sentinel */
};

static PyTypeObject VT100_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "hl_vt100.vt100_headless",
    .tp_basicsize = sizeof(VT100Object),
    .tp_dealloc = (destructor)VT100_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_methods = VT100_methods,
    .tp_init = (initproc)VT100_init,
    .tp_new = PyType_GenericNew,
    .tp_members = VT100_members,
    .tp_getset = VT100_getsetters,
};

PyDoc_STRVAR(module_doc,
"Headless VT100 Terminal Emulator.");

static struct PyModuleDef hl_vt100_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "hl_vt100",
    .m_doc = module_doc,
};

PyMODINIT_FUNC
PyInit_hl_vt100(void)
{
    PyObject *m;

    m = PyModule_Create(&hl_vt100_module);
    allocated = PyMem_Calloc(4096, sizeof(VT100Object*));
    allocated_size = 4096;
    if (m == NULL)
        return NULL;
    if (PyModule_AddType(m, &VT100_Type) < 0) {
        Py_DECREF(m);
        return NULL;
    }
    return m;
}
