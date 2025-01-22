#ifndef BERTRAND_PYTHON_CORE_EXCEPT_H
#define BERTRAND_PYTHON_CORE_EXCEPT_H

#include "declarations.h"
#include "object.h"
#include "ops.h"


namespace bertrand {


namespace impl {

    /* Get the current thread state and assert that it does not have an active
    exception. */
    inline PyThreadState* assert_no_active_python_exception() {
        PyThreadState* tstate = PyThreadState_Get();
        if (!tstate) {
            throw AssertionError(
                "Exception::to_python() called without an active Python interpreter"
            );
        }
        if (tstate->current_exception) {
            Object str = steal<Object>(PyObject_Repr(tstate->current_exception));
            if (str.is(nullptr)) {
                Exception::from_python();
            }
            Py_ssize_t len;
            const char* message = PyUnicode_AsUTF8AndSize(
                ptr(str),
                &len
            );
            if (message == nullptr) {
                Exception::from_python();
            }

            throw AssertionError(
                "Exception::to_python() called while an active Python exception "
                "already exists for the current interpreter:\n\n" +
                std::string(message, len)
            );
        }
        return tstate;
    }

    /* A wrapper around an existing Python exception that allows it to be handled as an
    equivalent C++ exception.  This inherits from both `Object` and the templated
    exception type, meaning it can be treated polymorphically in both directions.
    Under normal use, the user should not be aware that this class even exists,
    although it does optimize the case where an exception originates from Python,
    propagates through C++, and then is returned to Python by retaining the original
    Python object without any additional allocations. */
    template <meta::inherits<Exception> T> requires (!meta::is_qualified<T>)
    struct py_err;

    template <meta::inherits<Exception> T>
    py_err(T) -> py_err<T>;

    /* Holds the tables needed to translate C++ exceptions to python and vice versa
    for a particular interpreter. */
    struct ExceptionTable {
        ExceptionTable() {
            static constexpr auto to_string = [](const Object& obj) {
                Py_ssize_t len;
                const char* data = PyUnicode_AsUTF8AndSize(
                    ptr(obj),
                    &len
                );
                if (data == nullptr) {
                    Exception::from_python();
                }
                return std::string(data, len);
            };

            register_from_python<Exception>(borrow<Object>(PyExc_Exception));
            register_from_python<ArithmeticError>(borrow<Object>(PyExc_ArithmeticError));
            register_from_python<FloatingPointError>(borrow<Object>(PyExc_FloatingPointError));
            register_from_python<OverflowError>(borrow<Object>(PyExc_OverflowError));
            register_from_python<ZeroDivisionError>(borrow<Object>(PyExc_ZeroDivisionError));
            register_from_python<AssertionError>(borrow<Object>(PyExc_AssertionError));
            register_from_python<AttributeError>(borrow<Object>(PyExc_AttributeError));
            register_from_python<BufferError>(borrow<Object>(PyExc_BufferError));
            register_from_python<EOFError>(borrow<Object>(PyExc_EOFError));
            register_from_python<ImportError>(borrow<Object>(PyExc_ImportError));
            register_from_python<ModuleNotFoundError>(borrow<Object>(PyExc_ModuleNotFoundError));
            register_from_python<LookupError>(borrow<Object>(PyExc_LookupError));
            register_from_python<IndexError>(borrow<Object>(PyExc_IndexError));
            register_from_python<KeyError>(borrow<Object>(PyExc_KeyError));
            register_from_python<MemoryError>(borrow<Object>(PyExc_MemoryError));
            register_from_python<NameError>(borrow<Object>(PyExc_NameError));
            register_from_python<UnboundLocalError>(borrow<Object>(PyExc_UnboundLocalError));
            register_from_python<OSError>(borrow<Object>(PyExc_OSError));
            register_from_python<BlockingIOError>(borrow<Object>(PyExc_BlockingIOError));
            register_from_python<ChildProcessError>(borrow<Object>(PyExc_ChildProcessError));
            register_from_python<ConnectionError>(borrow<Object>(PyExc_ConnectionError));
            register_from_python<BrokenPipeError>(borrow<Object>(PyExc_BrokenPipeError));
            register_from_python<ConnectionAbortedError>(borrow<Object>(PyExc_ConnectionAbortedError));
            register_from_python<ConnectionRefusedError>(borrow<Object>(PyExc_ConnectionRefusedError));
            register_from_python<ConnectionResetError>(borrow<Object>(PyExc_ConnectionResetError));
            register_from_python<FileExistsError>(borrow<Object>(PyExc_FileExistsError));
            register_from_python<FileNotFoundError>(borrow<Object>(PyExc_FileNotFoundError));
            register_from_python<InterruptedError>(borrow<Object>(PyExc_InterruptedError));
            register_from_python<IsADirectoryError>(borrow<Object>(PyExc_IsADirectoryError));
            register_from_python<NotADirectoryError>(borrow<Object>(PyExc_NotADirectoryError));
            register_from_python<PermissionError>(borrow<Object>(PyExc_PermissionError));
            register_from_python<ProcessLookupError>(borrow<Object>(PyExc_ProcessLookupError));
            register_from_python<TimeoutError>(borrow<Object>(PyExc_TimeoutError));
            register_from_python<ReferenceError>(borrow<Object>(PyExc_ReferenceError));
            register_from_python<RuntimeError>(borrow<Object>(PyExc_RuntimeError));
            register_from_python<NotImplementedError>(borrow<Object>(PyExc_NotImplementedError));
            register_from_python<RecursionError>(borrow<Object>(PyExc_RecursionError));
            register_from_python<StopAsyncIteration>(borrow<Object>(PyExc_StopAsyncIteration));
            register_from_python<StopIteration>(borrow<Object>(PyExc_StopIteration));
            register_from_python<SyntaxError>(borrow<Object>(PyExc_SyntaxError));
            register_from_python<IndentationError>(borrow<Object>(PyExc_IndentationError));
            register_from_python<TabError>(borrow<Object>(PyExc_TabError));
            register_from_python<SystemError>(borrow<Object>(PyExc_SystemError));
            register_from_python<TypeError>(borrow<Object>(PyExc_TypeError));
            register_from_python<ValueError>(borrow<Object>(PyExc_ValueError));
            register_from_python<UnicodeError>(borrow<Object>(PyExc_UnicodeError));
            register_from_python<UnicodeDecodeError>(
                borrow<Object>(PyExc_UnicodeDecodeError),
                [](Object exception) -> void {
                    Object encoding = steal<Object>(PyUnicodeDecodeError_GetEncoding(
                        ptr(exception)
                    ));
                    if (encoding.is(nullptr)) {
                        Exception::from_python();
                    }
                    Object object = steal<Object>(PyUnicodeDecodeError_GetObject(
                        ptr(exception)
                    ));
                    if (object.is(nullptr)) {
                        Exception::from_python();
                    }
                    Py_ssize_t start;
                    Py_ssize_t end;
                    if (
                        PyUnicodeDecodeError_GetStart(ptr(exception), &start) ||
                        PyUnicodeDecodeError_GetEnd(ptr(exception), &end)
                    ) {
                        Exception::from_python();
                    }
                    Object reason = steal<Object>(PyUnicodeDecodeError_GetReason(
                        ptr(exception)
                    ));
                    if (reason.is(nullptr)) {
                        Exception::from_python();
                    }
                    throw UnicodeDecodeError(
                        to_string(encoding),
                        to_string(object),
                        start,
                        end,
                        to_string(reason)
                    );
                }
            );
            register_from_python<UnicodeEncodeError>(
                borrow<Object>(PyExc_UnicodeEncodeError),
                [](Object exception) -> void {
                    Object encoding = steal<Object>(PyUnicodeEncodeError_GetEncoding(
                        ptr(exception)
                    ));
                    if (encoding.is(nullptr)) {
                        Exception::from_python();
                    }
                    Object object = steal<Object>(PyUnicodeEncodeError_GetObject(
                        ptr(exception)
                    ));
                    if (object.is(nullptr)) {
                        Exception::from_python();
                    }
                    Py_ssize_t start;
                    Py_ssize_t end;
                    if (
                        PyUnicodeEncodeError_GetStart(ptr(exception), &start) ||
                        PyUnicodeEncodeError_GetEnd(ptr(exception), &end)
                    ) {
                        Exception::from_python();
                    }
                    Object reason = steal<Object>(PyUnicodeEncodeError_GetReason(
                        ptr(exception)
                    ));
                    if (reason.is(nullptr)) {
                        Exception::from_python();
                    }
                    throw UnicodeEncodeError(
                        to_string(encoding),
                        to_string(object),
                        start,
                        end,
                        to_string(reason)
                    );
                }
            );
            register_from_python<UnicodeTranslateError>(
                borrow<Object>(PyExc_UnicodeTranslateError),
                [](Object exception) -> void {
                    Object object = steal<Object>(PyUnicodeTranslateError_GetObject(
                        ptr(exception)
                    ));
                    if (object.is(nullptr)) {
                        Exception::from_python();
                    }
                    Py_ssize_t start;
                    Py_ssize_t end;
                    if (
                        PyUnicodeTranslateError_GetStart(ptr(exception), &start) ||
                        PyUnicodeTranslateError_GetEnd(ptr(exception), &end)
                    ) {
                        Exception::from_python();
                    }
                    Object reason = steal<Object>(PyUnicodeTranslateError_GetReason(
                        ptr(exception)
                    ));
                    throw UnicodeTranslateError(
                        to_string(object),
                        start,
                        end,
                        to_string(reason)
                    );
                }
            );

            register_to_python<Exception>();
            register_to_python<ArithmeticError>();
            register_to_python<FloatingPointError>();
            register_to_python<OverflowError>();
            register_to_python<ZeroDivisionError>();
            register_to_python<AssertionError>();
            register_to_python<AttributeError>();
            register_to_python<BufferError>();
            register_to_python<EOFError>();
            register_to_python<ImportError>();
            register_to_python<ModuleNotFoundError>();
            register_to_python<LookupError>();
            register_to_python<IndexError>();
            register_to_python<KeyError>();
            register_to_python<MemoryError>();
            register_to_python<NameError>();
            register_to_python<UnboundLocalError>();
            register_to_python<OSError>();
            register_to_python<BlockingIOError>();
            register_to_python<ChildProcessError>();
            register_to_python<ConnectionError>();
            register_to_python<BrokenPipeError>();
            register_to_python<ConnectionAbortedError>();
            register_to_python<ConnectionRefusedError>();
            register_to_python<ConnectionResetError>();
            register_to_python<FileExistsError>();
            register_to_python<FileNotFoundError>();
            register_to_python<InterruptedError>();
            register_to_python<IsADirectoryError>();
            register_to_python<NotADirectoryError>();
            register_to_python<PermissionError>();
            register_to_python<ProcessLookupError>();
            register_to_python<TimeoutError>();
            register_to_python<ReferenceError>();
            register_to_python<RuntimeError>();
            register_to_python<NotImplementedError>();
            register_to_python<RecursionError>();
            register_to_python<StopAsyncIteration>();
            register_to_python<StopIteration>();
            register_to_python<SyntaxError>();
            register_to_python<IndentationError>();
            register_to_python<TabError>();
            register_to_python<SystemError>();
            register_to_python<TypeError>();
            register_to_python<ValueError>();
            register_to_python<UnicodeError>();
            register_to_python<UnicodeDecodeError>(
                [](const Exception& exception) -> Object {
                    const UnicodeDecodeError& exc =
                        static_cast<const UnicodeDecodeError&>(exception);
                    Object result = steal<Object>(PyObject_CallFunction(
                        PyExc_UnicodeDecodeError,
                        "ssnns",
                        exc.encoding.data(),
                        exc.object.data(),
                        exc.start,
                        exc.end,
                        exc.reason.data()
                    ));
                    if (result.is(nullptr)) {
                        Exception::from_python();
                    }
                    return result;
                }
            );
            register_to_python<UnicodeEncodeError>(
                [](const Exception& exception) -> Object {
                    const UnicodeEncodeError& exc =
                        static_cast<const UnicodeEncodeError&>(exception);
                    Object result = steal<Object>(PyObject_CallFunction(
                        PyExc_UnicodeEncodeError,
                        "ssnns",
                        exc.encoding.data(),
                        exc.object.data(),
                        exc.start,
                        exc.end,
                        exc.reason.data()
                    ));
                    if (result.is(nullptr)) {
                        Exception::from_python();
                    }
                    return result;
                }
            );
            register_to_python<UnicodeTranslateError>(
                [](const Exception& exception) -> Object {
                    const UnicodeTranslateError& exc =
                        static_cast<const UnicodeTranslateError&>(exception);
                    Object result = steal<Object>(PyObject_CallFunction(
                        PyExc_UnicodeTranslateError,
                        "snns",
                        exc.object.data(),
                        exc.start,
                        exc.end,
                        exc.reason.data()
                    ));
                    if (result.is(nullptr)) {
                        Exception::from_python();
                    }
                    return result;
                }
            );
        }

        /* A map relating every bertrand exception type to its equivalent Python
        type. */
        std::unordered_map<std::type_index, Object> types;

        /* A map holding function pointers that take an arbitrary bertrand exception
        and convert it into an equivalent Python exception.  A program is malformed and
        will immediately exit if a subclass of `bertrand::Exception` is passed to
        Python without a corresponding entry in this map. */
        std::unordered_map<std::type_index, Object(*)(const Exception&)> to_python;

        /* A map holding function pointers that take a Python exception of a particular
        C++ type and re-throw it as a matching `py_err<T>` wrapper.  The result can
        then be caught and handled via ordinary semantics.  The value stored in this
        map is always identical to `from_python.at(types.at(typeid(T)))` - this map is
        just a shortcut to avoid the intermediate lookup.  It is used to implement the
        `borrow()` and `steal()` constructors for `py_err<T>`. */
        std::unordered_map<std::type_index, void(*)(Object)> to_cpp;

        /* A map holding function pointers that take an arbitrary Python exception
        object directly from the interpreter and re-throw it as a corresponding
        `py_err<T>` wrapper, which can be caught in C++ according to Python semantics.
        A program is malformed and will immediately exit if a Python exception is
        caught in C++ whose type is not present in this map. */
        std::unordered_map<Object, void(*)(Object)> from_python;

        /* Insert an `Exception::from_python()` hook for this exception type into the
        global map. */
        template <meta::inherits<Exception> T> requires (!meta::is_qualified<T>)
        void register_from_python(
            Object type,
            void(*callback)(Object) = simple_from_python<T>
        ) {
            types.emplace(typeid(T), type);
            types.emplace(typeid(py_err<T>), type);
            to_cpp.emplace(typeid(T), callback);
            to_cpp.emplace(typeid(py_err<T>), callback);
            from_python.emplace(
                type,
                [](Object exception) {
                    throw steal<py_err<T>>(release(exception));
                }
            );
        }

        /* Insert an `Exception::to_python()` hook for this exception type into the
        global map. */
        template <meta::inherits<Exception> T> requires (!meta::is_qualified<T>)
        void register_to_python(
            Object(*callback)(const Exception&) = simple_to_python<T>
        ) {
            to_python.emplace(typeid(T), callback);
            to_python.emplace(
                typeid(py_err<T>),
                [](const Exception& exception) -> Object {
                    return reinterpret_cast<const py_err<T>&>(exception);
                }
            );
        }

        /* Clear all Python handlers for this exception type. */
        template <meta::inherits<Exception> T> requires (!meta::is_qualified<T>)
        void unregister() {
            auto node = types.extract(typeid(T));
            if (node) {
                to_python.erase(node.key());
                to_cpp.erase(node.key());
                from_python.erase(node.mapped());
            }
            node = types.extract(typeid(py_err<T>));
            if (node) {
                to_python.erase(node.key());
                to_cpp.erase(node.key());
                from_python.erase(node.mapped());
            }
        }

        /* The default `Exception::from_python()` handler that will be registered if no
        explicit override is provided.  This will simply reinterpret the current Python
        error as a corresponding `py_err<T>` exception wrapper, which can be caught using
        typical bertrand. */
        template <typename T>
        [[noreturn]] static void simple_from_python(Object exception);

        /* The default `Exception::to_python()` handler that will be registered if no
        explicit override is provided.  This simply calls the exception's Python
        constructor with the raw text of the error message, and then */
        template <typename T>
        static Object simple_to_python(const Exception& exception);
    };

    /* Stores the global map of exception tables for each Python interpreter.  The
    lifecycle of and access to these entries is managed by the constructor and
    destructor of the `bertrand` module, so users shouldn't need to worry about
    them. */
    inline std::unordered_map<PyInterpreterState*, ExceptionTable> exception_table;

    /* A wrapper around an existing Python exception that allows it to be handled as an
    equivalent C++ exception.  This inherits from both `Object` and the templated
    exception type, meaning it can be treated polymorphically in both directions.
    Under normal use, the user should not be aware that this class even exists,
    although it does optimize the case where an exception originates from Python,
    propagates through C++, and then is returned to Python by retaining the original
    Python object without any additional allocations. */
    template <meta::inherits<Exception> T> requires (!meta::is_qualified<T>)
    struct py_err : T, Object {

        /* `borrow()` constructor.  Also converts the Python exception into an
        instance of `T` by consulting the exception tables. */
        py_err(PyObject* p, borrowed_t t) :
            T([](Object p) {
                auto table = exception_table.find(PyInterpreterState_Get());
                if (table == exception_table.end()) {
                    throw AssertionError(
                        "no exception table found for the current Python interpreter"
                    );
                }

                auto it = table->second.to_cpp.find(typeid(T));
                if (it == table->second.to_cpp.end()) {
                    throw AssertionError(
                        "no exception handler registered for '" + type_name<T> + "'"
                    );
                }
                try {
                    it->second(std::move(p));
                } catch (T&& exc) {
                    return T(std::move(exc).trim_before().skip());
                } catch (...) {
                    throw;
                }
                throw AssertionError(
                    "handler must throw an exception of type '" + type_name<T> + "'"
                );
            }(borrow<Object>(p))),
            Object(p, t)
        {}

        /* `steal()` constructor.  Also converts the Python exception into an instance
        of `T` by consulting the exception tables. */
        py_err(PyObject* p, stolen_t t) :
            T([](Object p) {
                auto table = exception_table.find(PyInterpreterState_Get());
                if (table == exception_table.end()) {
                    throw AssertionError(
                        "no exception table found for the current Python interpreter"
                    );
                }
                auto it = table->second.to_cpp.find(typeid(T));
                if (it == table->second.to_cpp.end()) {
                    throw AssertionError(
                        "no exception handler registered for '" + type_name<T> + "'"
                    );
                }
                try {
                    it->second(std::move(p));
                } catch (T&& exc) {
                    return T(std::move(exc).trim_before().skip());
                } catch (...) {
                    throw;
                }
                throw AssertionError(
                    "handler must throw an exception of type '" + type_name<T> + "'"
                );
            }(borrow<Object>(p))),
            Object(p, t)
        {}

        /* Forwarding constructor.  Directly constructs an instance of `T`, and then
        converts that instance into an equivalent Python form by consulting the
        exception tables. */
        template <typename... Args> requires (std::constructible_from<T, Args...>)
        explicit py_err(Args&&... args) :
            T(std::forward<Args>(args)...),
            Object([](const T& exc) {
                auto table = exception_table.find(PyInterpreterState_Get());
                if (table == exception_table.end()) {
                    throw AssertionError(
                        "no exception table found for the current Python interpreter"
                    );
                }
                auto it = table->second.to_python.find(typeid(T));
                if (it == table->second.to_python.end()) {
                    throw AssertionError(
                        "no Python exception type registered for C++ exception of type '" +
                        type_name<T> + "'"
                    );
                }
                return it->second(exc);
            }(static_cast<const T&>(*this)))
        {}

        /* A type index for this exception wrapper, which can be searched in the global
        exception tables to find a corresponding callback. */
        virtual std::type_index type() const noexcept override {
            return typeid(py_err);
        }

        /* The full exception diagnostic, including a traceback that interleaves the
        original Python trace with a continuation trace in C++. */
        constexpr virtual const char* what() const noexcept override {
            if (T::m_what.empty()) {
                T::m_what = "Traceback (most recent call last):\n";

                // insert C++ traceback
                if (const cpptrace::stacktrace* trace = this->trace()) {
                    for (size_t i = trace->frames.size(); i-- > 0;) {
                        T::m_what += T::format_frame(trace->frames[i]);
                    }
                }

                // continue with Python traceback
                Object tb = steal<Object>(PyException_GetTraceback(ptr(*this)));
                while (!tb.is(nullptr)) {
                    PyFrameObject* frame = reinterpret_cast<PyTracebackObject*>(
                        ptr(tb)
                    )->tb_frame;
                    if (!frame) {
                        break;
                    }
                    Object code = steal<Object>(reinterpret_cast<PyObject*>(
                        PyFrame_GetCode(frame)
                    ));
                    if (code.is(nullptr)) {
                        break;
                    }
                    Py_ssize_t len;
                    const char* str = PyUnicode_AsUTF8AndSize(
                        reinterpret_cast<PyCodeObject*>(
                            ptr(code)
                        )->co_filename,
                        &len
                    );
                    if (!str) {
                        Exception::from_python();
                    }
                    T::m_what += "    File \"" + std::string(str, len) + "\", line ";
                    T::m_what += std::to_string(PyFrame_GetLineNumber(frame));
                    T::m_what += ", in ";
                    str = PyUnicode_AsUTF8AndSize(
                        reinterpret_cast<PyCodeObject*>(
                            ptr(code)
                        )->co_name,
                        &len
                    );
                    if (!str) {
                        Exception::from_python();
                    }
                    T::m_what += std::string(str, len);
                }

                // exception message
                T::m_what += T::name();
                T::m_what += ": ";
                T::m_what += T::message();
            }
            return T::m_what.data();
        }
    };

    /* Append a C++ stack trace to a Python traceback, which can be attached to a
    newly-constructed exception object.  Steals a reference to `head` if it is given,
    and returns a new reference to the updated traceback, which may be null if no new
    frames were generated and `head` is null. */
    inline PyTracebackObject* build_traceback(
        const cpptrace::stacktrace& trace,
        PyTracebackObject* head = nullptr
    ) {
        PyThreadState* tstate = PyThreadState_Get();
        Object globals = steal<Object>(PyDict_New());
        if (globals.is(nullptr)) {
            Exception::from_python();
        }
        for (const cpptrace::stacktrace_frame& frame : trace) {
            size_t line = frame.line.value_or(0);
            PyCodeObject* code = PyCode_NewEmpty(
                frame.filename.c_str(),
                frame.symbol.c_str(),
                line
            );
            if (!code) {
                Exception::from_python();
            }
            PyFrameObject* py_frame = PyFrame_New(
                tstate,
                code,
                ptr(globals),
                nullptr
            );
            Py_DECREF(code);
            if (!py_frame) {
                Exception::from_python();
            }
            py_frame->f_lineno = line;
            PyTracebackObject* tb = PyObject_GC_New(
                PyTracebackObject,
                &PyTraceBack_Type
            );
            if (tb == nullptr) {
                Py_DECREF(py_frame);
                throw MemoryError();
            }
            tb->tb_next = head;
            tb->tb_frame = py_frame;  // steals reference
            tb->tb_lasti = PyFrame_GetLasti(tb->tb_frame) * sizeof(_Py_CODEUNIT);
            tb->tb_lineno = PyFrame_GetLineNumber(tb->tb_frame);
            PyObject_GC_Track(tb);
            head = tb;
        }
        return head;
    }

    template <typename T>
    [[noreturn]] void ExceptionTable::simple_from_python(Object exception) {
        Object args = steal<Object>(PyException_GetArgs(ptr(exception)));
        if (args.is(nullptr)) {
            Exception::from_python();
        }
        PyObject* message;
        if (
            PyTuple_GET_SIZE(ptr(args)) != 1 ||
            !PyUnicode_Check(message = PyTuple_GET_ITEM(ptr(args), 0))
        ) {
            throw AssertionError(
                "Python exception must take a single string argument or "
                "register a custom from_python() handler: '" + type_name<T> + "'"
            );
        }
        Py_ssize_t len;
        const char* text = PyUnicode_AsUTF8AndSize(message, &len);
        if (text == nullptr) {
            Exception::from_python();
        }
        throw T(std::string(text, len));
    }

    template <typename T>
    Object ExceptionTable::simple_to_python(const Exception& exception) {
        // look up equivalent Python type for T
        auto table = exception_table.find(PyInterpreterState_Get());
        if (table == exception_table.end()) {
            throw AssertionError(
                "no exception table found for the current Python interpreter"
            );
        }
        auto it = table->second.types.find(typeid(T));
        if (it == table->second.types.end()) {
            throw AssertionError(
                "no Python exception type registered for C++ exception of type '" +
                type_name<T> + "'"
            );
        }

        // convert C++ exception message to a Python string
        Object message = steal<Object>(PyUnicode_FromStringAndSize(
            exception.message().data(),
            exception.message().size()
        ));
        if (message.is(nullptr)) {
            Exception::from_python();
        }

        // call the Python constructor with the converted message
        Object value = steal<Object>(PyObject_CallOneArg(
            ptr(it->second),
            ptr(message)
        ));
        if (value.is(nullptr)) {
            Exception::from_python();
        }
        return value;
    }

}


namespace meta::detail {
    template <typename T>
    inline constexpr bool builtin_type<impl::py_err<T>> = true;
}


inline void Exception::to_python() noexcept {
    constexpr auto raise = [](const Exception& exc) {
        PyThreadState* tstate = impl::assert_no_active_python_exception();
        auto table = impl::exception_table.find(tstate->interp);
        if (table == impl::exception_table.end()) {
            throw AssertionError(
                "no exception table found for the current Python interpreter"
            );
        }
        auto it = table->second.to_python.find(exc.type());
        if (it == table->second.to_python.end()) {
            std::cerr << "no to_python() handler for exception of type '"
                      << exc.name() << "'";
            std::exit(1);
        }
        Object value = it->second(exc);
        if (auto traceback = exc.trace()) {
            PyObject* existing = PyException_GetTraceback(ptr(value));  // new reference
            Object tb = steal<Object>(reinterpret_cast<PyObject*>(
                impl::build_traceback(
                    *traceback,
                    reinterpret_cast<PyTracebackObject*>(existing)  // steals a reference
                ))
            );
            if (!tb.is(existing)) {
                PyException_SetTraceback(ptr(value), ptr(tb));
            }
        }
        PyErr_SetRaisedException(release(value));  // steals a reference
    };

    try {
        throw;
    } catch (const Exception& exc) {
        raise(exc.trim_after(1));
    } catch (const std::exception& e) {
        raise(Exception(e.what()).trim_after(1));
    } catch (...) {
        raise(Exception("unknown C++ exception").trim_after(1));
    }
}


[[noreturn]] inline void Exception::from_python() {
    Object exception = steal<Object>(PyErr_GetRaisedException());
    if (exception.is(nullptr)) {
        throw AssertionError(
            "Exception::from_python() called without an active Python exception "
            "for the current interpreter"
        );
    }
    PyTypeObject* type = Py_TYPE(ptr(exception));
    if (!type) {
        throw AssertionError(
            "Exception::from_python() could not determine the type of Python "
            "exception being raised"
        );
    }
    auto table = impl::exception_table.find(PyInterpreterState_Get());
    if (table == impl::exception_table.end()) {
        throw AssertionError(
            "no exception table found for the current Python interpreter"
        );
    }
    auto it = table->second.from_python.find(
        borrow<Object>(reinterpret_cast<PyObject*>(type))
    );
    if (it == table->second.from_python.end()) {
        throw AssertionError(
            "no from_python() handler for Python exception of type '" +
            demangle(type->tp_name) + "'"
        );
    }
    it->second(std::move(exception));  // throws py_err<T>
    throw AssertionError(
        "from_python() handler must throw an exception of type 'py_err<T>'"
    );
}


///////////////////////////
////    STACK TRACE    ////
///////////////////////////


/// TODO: tracebacks should be either deleted or moved later on in the process.


// struct Traceback;


// template <>
// struct interface<Traceback> {
//     [[nodiscard]] std::string to_string(this const auto& self);
// };


// /* A cross-language traceback that records an accurate call stack of a mixed Python/C++
// application. */
// struct Traceback : Object, interface<Traceback> {
//     struct __python__ : cls<__python__, Traceback>, PyTracebackObject {
//         static Type<Traceback> __import__();
//     };

//     Traceback(PyObject* p, borrowed_t t) : Object(p, t) {}
//     Traceback(PyObject* p, stolen_t t) : Object(p, t) {}

//     template <typename T = Traceback> requires (__initializer__<T>::enable)
//     [[clang::noinline]] Traceback(
//         std::initializer_list<typename __initializer__<T>::type> init
//     ) : Object(__initializer__<T>{}(init)) {}

//     template <typename... Args> requires (implicit_ctor<Traceback>::template enable<Args...>)
//     [[clang::noinline]] Traceback(Args&&... args) : Object(
//         implicit_ctor<Traceback>{},
//         std::forward<Args>(args)...
//     ) {}

//     template <typename... Args> requires (explicit_ctor<Traceback>::template enable<Args...>)
//     [[clang::noinline]] explicit Traceback(Args&&... args) : Object(
//         explicit_ctor<Traceback>{},
//         std::forward<Args>(args)...
//     ) {}

// };


// template <>
// struct interface<Type<Traceback>> {
//     [[nodiscard]] static std::string to_string(const auto& self) {
//         return self.to_string();
//     }
// };


// template <meta::is<cpptrace::stacktrace> T>
// struct __cast__<T>                                          : returns<Traceback> {};


// /* Converting a `cpptrace::stacktrace_frame` into a Python frame object will synthesize
// an interpreter frame with an empty bytecode object. */
// template <meta::is<cpptrace::stacktrace> T>
// struct __cast__<T, Traceback>                               : returns<Traceback> {
//     static auto operator()(const cpptrace::stacktrace& trace) {
//         // Traceback objects are stored in a singly-linked list, with the most recent
//         // frame at the end of the list and the least frame at the beginning.  As a
//         // result, we need to build them from the inside out, starting with C++ frames.
//         PyTracebackObject* front = impl::build_traceback(trace);

//         // continue with the Python frames, again starting with the most recent
//         PyFrameObject* frame = reinterpret_cast<PyFrameObject*>(
//             Py_XNewRef(PyEval_GetFrame())
//         );
//         while (frame != nullptr) {
//             PyTracebackObject* tb = PyObject_GC_New(
//                 PyTracebackObject,
//                 &PyTraceBack_Type
//             );
//             if (tb == nullptr) {
//                 Py_DECREF(frame);
//                 Py_DECREF(front);
//                 throw std::runtime_error(
//                     "could not create Python traceback object - failed to allocate "
//                     "PyTraceBackObject"
//                 );
//             }
//             tb->tb_next = front;
//             tb->tb_frame = frame;
//             tb->tb_lasti = PyFrame_GetLasti(tb->tb_frame) * sizeof(_Py_CODEUNIT);
//             tb->tb_lineno = PyFrame_GetLineNumber(tb->tb_frame);
//             PyObject_GC_Track(tb);
//             front = tb;
//             frame = PyFrame_GetBack(frame);
//         }

//         return steal<Traceback>(reinterpret_cast<PyObject*>(front));
//     }
// };


// /* Default initializing a Traceback object retrieves a trace to the current frame,
// inserting C++ frames where necessary. */
// template <>
// struct __init__<Traceback>                                  : returns<Traceback> {
//     [[clang::noinline]] static auto operator()() {
//         return Traceback(cpptrace::generate_trace(1));
//     }
// };


// /* Providing an explicit integer will skip that number of frames from either the least
// recent frame (if positive or zero) or the most recent (if negative).  Positive integers
// will produce a traceback with at most the given length, and negative integers will
// reduce the length by at most the given value. */
// template <std::convertible_to<int> T>
// struct __init__<Traceback, T>                               : returns<Traceback> {
//     static auto operator()(int skip) {
//         // if skip is zero, then the result will be empty by definition
//         if (skip == 0) {
//             return steal<Traceback>(nullptr);
//         }

//         // compute the full traceback to account for mixed C++ and Python frames
//         Traceback trace(cpptrace::generate_trace(1));
//         PyTracebackObject* curr = reinterpret_cast<PyTracebackObject*>(ptr(trace));

//         // if skip is negative, we need to skip the most recent frames, which are
//         // stored at the tail of the list.  Since we don't know the exact length of the
//         // list, we can use a 2-pointer approach wherein the second pointer trails the
//         // first by the given skip value.  When the first pointer reaches the end of
//         // the list, the second pointer will be at the new terminal frame.
//         if (skip < 0) {
//             PyTracebackObject* offset = curr;
//             for (int i = 0; i > skip; ++i) {
//                 // the traceback may be shorter than the skip value, in which case we
//                 // return an empty traceback
//                 if (curr == nullptr) {
//                     return steal<Traceback>(nullptr);
//                 }
//                 curr = curr->tb_next;
//             }

//             while (curr != nullptr) {
//                 curr = curr->tb_next;
//                 offset = offset->tb_next;
//             }

//             // the offset pointer is now at the terminal frame, so we can safely remove
//             // any subsequent frames.  Decrementing the reference count of the next
//             // frame will garbage collect the remainder of the list.
//             curr = offset->tb_next;
//             offset->tb_next = nullptr;
//             Py_DECREF(curr);
//             return trace;
//         }

//         // if skip is positive, then we clear from the head, which is much simpler
//         PyTracebackObject* prev = nullptr;
//         for (int i = 0; i < skip; ++i) {
//             // the traceback may be shorter than the skip value, in which case we return
//             // the original traceback
//             if (curr == nullptr) {
//                 return trace;
//             }
//             prev = curr;
//             curr = curr->tb_next;
//         }
//         prev->tb_next = nullptr;
//         Py_DECREF(curr);
//         return trace;
//     }
// };


// /* len(Traceback) yields the overall depth of the stack trace, including both C++ and
// Python frames. */
// template <meta::is<Traceback> Self>
// struct __len__<Self>                                        : returns<size_t> {
//     static auto operator()(const Traceback& self) {
//         PyTracebackObject* tb = reinterpret_cast<PyTracebackObject*>(ptr(self));
//         size_t count = 0;
//         while (tb != nullptr) {
//             ++count;
//             tb = tb->tb_next;
//         }
//         return count;
//     }
// };


// /* Iterating over the frames yields them in least recent -> most recent order. */
// template <meta::is<Traceback> Self>
// struct __iter__<Self>                                       : returns<Frame> {
//     using iterator_category = std::forward_iterator_tag;
//     using difference_type = std::ptrdiff_t;
//     using value_type = Frame;
//     using reference = Frame&;
//     using pointer = Frame*;

//     Traceback traceback;
//     PyTracebackObject* curr;

//     __iter__(const Traceback& self) :
//         traceback(self),
//         curr(reinterpret_cast<PyTracebackObject*>(ptr(traceback)))
//     {}
//     __iter__(Traceback&& self) :
//         traceback(std::move(self)),
//         curr(reinterpret_cast<PyTracebackObject*>(ptr(traceback)))
//     {}
//     __iter__(const __iter__& other) : traceback(other.traceback), curr(other.curr) {}
//     __iter__(__iter__&& other) : traceback(std::move(other.traceback)), curr(other.curr) {
//         other.curr = nullptr;
//     }

//     __iter__& operator=(const __iter__& other) {
//         if (&other != this) {
//             traceback = other.traceback;
//             curr = other.curr;
//         }
//         return *this;
//     }

//     __iter__& operator=(__iter__&& other) {
//         if (&other != this) {
//             traceback = std::move(other.traceback);
//             curr = other.curr;
//             other.curr = nullptr;
//         }
//         return *this;
//     }

//     [[nodiscard]] Frame operator*() const;

//     __iter__& operator++() {
//         if (curr != nullptr) {
//             curr = curr->tb_next;
//         }
//         return *this;
//     }

//     __iter__ operator++(int) {
//         __iter__ copy(*this);
//         if (curr != nullptr) {
//             curr = curr->tb_next;
//         }
//         return copy;
//     }

//     [[nodiscard]] friend bool operator==(const __iter__& self, sentinel) {
//         return self.curr == nullptr;
//     }

//     [[nodiscard]] friend bool operator==(sentinel, const __iter__& self) {
//         return self.curr == nullptr;
//     }

//     [[nodiscard]] friend bool operator!=(const __iter__& self, sentinel) {
//         return self.curr != nullptr;
//     }

//     [[nodiscard]] friend bool operator!=(sentinel, const __iter__& self) {
//         return self.curr != nullptr;
//     }
// };


// /* Reverse iterating over the frames yields them in most recent -> least recent order. */
// template <meta::is<Traceback> Self>
// struct __reversed__<Self>                                   : returns<Traceback> {
//     using iterator_category = std::forward_iterator_tag;
//     using difference_type = std::ptrdiff_t;
//     using value_type = Frame;
//     using reference = Frame&;
//     using pointer = Frame*;

//     Traceback traceback;
//     std::vector<PyTracebackObject*> frames;
//     Py_ssize_t index;

//     __reversed__(const Traceback& self) : traceback(self) {
//         PyTracebackObject* curr = reinterpret_cast<PyTracebackObject*>(
//             ptr(traceback)
//         );
//         while (curr != nullptr) {
//             frames.push_back(curr);
//             curr = curr->tb_next;
//         }
//         index = std::ssize(frames) - 1;
//     }

//     __reversed__(Traceback&& self) : traceback(std::move(self)) {
//         PyTracebackObject* curr = reinterpret_cast<PyTracebackObject*>(
//             ptr(traceback)
//         );
//         while (curr != nullptr) {
//             frames.push_back(curr);
//             curr = curr->tb_next;
//         }
//         index = std::ssize(frames) - 1;
//     }

//     __reversed__(const __reversed__& other) :
//         traceback(other.traceback),
//         frames(other.frames),
//         index(other.index)
//     {}

//     __reversed__(__reversed__&& other) :
//         traceback(std::move(other.traceback)),
//         frames(std::move(other.frames)),
//         index(other.index)
//     {
//         other.index = -1;
//     }

//     __reversed__& operator=(const __reversed__& other) {
//         if (&other != this) {
//             traceback = other.traceback;
//             frames = other.frames;
//             index = other.index;
//         }
//         return *this;
//     }

//     __reversed__& operator=(__reversed__&& other) {
//         if (&other != this) {
//             traceback = std::move(other.traceback);
//             frames = std::move(other.frames);
//             index = other.index;
//             other.index = -1;
//         }
//         return *this;
//     }

//     [[nodiscard]] value_type operator*() const;

//     __reversed__& operator++() {
//         if (index >= 0) {
//             --index;
//         }
//         return *this;
//     }

//     __reversed__ operator++(int) {
//         __reversed__ copy(*this);
//         if (index >= 0) {
//             --index;
//         }
//         return copy;
//     }

//     [[nodiscard]] friend bool operator==(const __reversed__& self, sentinel) {
//         return self.index == -1;
//     }

//     [[nodiscard]] friend bool operator==(sentinel, const __reversed__& self) {
//         return self.index == -1;
//     }

//     [[nodiscard]] friend bool operator!=(const __reversed__& self, sentinel) {
//         return self.index != -1;
//     }

//     [[nodiscard]] friend bool operator!=(sentinel, const __reversed__& self) {
//         return self.index != -1;
//     }
// };


}  // namespace bertrand


#endif
