#include "../src/vm.c"

#ifndef TM2C_C
#define TM2C_C

#define LEVEL_ERROR 0
#define LOG(level, msg, lineno, func_name, value) /* x */

// 操作栈相关的宏函数
#define MP2C_PUSH(x)  *(++top) = (x);
#define MP2C_POP()    *(top--)
#define MP2C_TOP()    (top[0])
#define MP2C_FIRST()  (top[0])
#define MP2C_SECOND() (top[-1])
#define MP2C_THIDRD() (top[-2])

MpObj obj_call_narg(MpObj callee, int n, MpObj* args);

MpObj obj_LT(MpObj left, MpObj right) {
    return number_obj(mp_cmp(left, right) < 0);
}

MpObj obj_LE(MpObj left, MpObj right) {
    return number_obj(mp_cmp(left, right) <= 0);
}

MpObj obj_GT(MpObj left, MpObj right) {
    return number_obj(mp_cmp(left, right) > 0);
}

MpObj obj_GE(MpObj left, MpObj right) {
    return number_obj(mp_cmp(left, right) >= 0);
}

MpObj obj_EQEQ(MpObj left, MpObj right) {
    return number_obj(is_obj_equals(left, right));
}

MpObj obj_LTEQ(MpObj left, MpObj right) {
    return number_obj(mp_cmp(left, right) <= 0);
}

void gc_local_add(MpObj object) {
    gc_track(object);
}

void gc_check_native_call(int size, MpObj returnMpObj) {
    gc_track(returnMpObj);
    // TODO check memory
}


MpObj mp_call(MpObj func, int args, ...) {
    int i = 0;
    va_list ap;
    va_start(ap, args);
    arg_start();
    for (i = 0; i < args; i++) {
        arg_push(va_arg(ap, MpObj));
    }
    va_end(ap);
    // mp_printf("at line %d, try to call %o with %d args\n", lineno, func_get_name_obj(func), args);
    // *self* will be resolved in obj_call
    MpObj ret;
    if (IS_DICT(func)) {
        ret = class_new(func);
        MpObj *_fnc = dict_get_by_str(ret, "__init__");
        if (_fnc != NULL) {
            func = *_fnc;
        } else {
            goto mp_call_end;
        }
    }

    if (IS_FUNC(func)) {
        RESOLVE_METHOD_SELF(func);
        /* call native */
        if (GET_FUNCTION(func)->native != NULL) {
            ret = GET_FUNCTION(func)->native();
            goto mp_call_end;
        } else {
            mp_raise("can not call py function from tm2c");
        }
    } 

    mp_raise("File %o, line=%d: obj_call:invalid object %o", GET_FUNCTION_FILE(tm->frame->fnc), 
        tm->frame->lineno, func);

    mp_call_end:
    gc_local_add(ret);
    return ret;
}

/**
 * call native function
 * @param fn, native function
 * @args, number of arguments
 * @...,  arguments
 * 
 * eg. add('test', wrapper(1))
 *  --> mp_call_native(add, 'test', mp_call_native(wrapper, 1), mp_call_native(wrapper, 2))
 *  <==> t1 = mp_call_native(wrapper, 1)  <local> = ['test', 1, 2]
 *       t1 = mp_call_native(wrapper, 2)  <local> = ['test', 1, 2]
 *       mp_call_native(add, t1, t2)      <local> = ['test', 1, 2]
 */
MpObj mp_call_native(MpObj (*fn)(), int args, ...) {
    int i = 0;
    va_list ap;
    MpObj obj_arg;

    va_start(ap, args);
    arg_start();
    for (i = 0; i < args; i++) {
        obj_arg = va_arg(ap, MpObj);
        arg_push(obj_arg);
    }
    va_end(ap);

    // record the length to restore
    int size = tm->local_obj_list->len; 

    MpObj ret = fn(); // execute native function

    gc_check_native_call(size, ret);

    return ret;
}

/**
 * optimization for mp_call_native
 * @since 2016-08-27
 */
MpObj mp_call_native_0(MpObj (*fn)()) {
    arg_start();
    int size = tm->local_obj_list->len;
    MpObj ret = fn();
    gc_check_native_call(size, ret);
    return NONE_OBJECT;
}

MpObj mp_call_native_1(MpObj (*fn)(), MpObj arg1) {
    arg_start();
    arg_push(arg1);
    int size = tm->local_obj_list->len;
    MpObj ret = fn();
    gc_check_native_call(size, ret);
    return NONE_OBJECT;
}

MpObj mp_call_native_2(MpObj (*fn)(), MpObj arg1, MpObj arg2) {
    arg_start();
    arg_push(arg1);
    arg_push(arg2);
    int size = tm->local_obj_list->len;
    MpObj ret = fn();
    gc_check_native_call(size, ret);
    return NONE_OBJECT;
}

/**
 * call native function with debug info
 * @param lineno current lineno in python file
 * @param func_name function name
 */
MpObj mp_call_native_debug(int lineno, char* func_name, MpObj (*fn)(), int args, ...) {
    int i = 0;
    va_list ap;
    MpObj obj_arg;

    tm->frame->lineno = lineno;
    LOG(LEVEL_ERROR, "call,%d,%s,start", lineno, func_name, 0);
    va_start(ap, args);
    arg_start();
    for (i = 0; i < args; i++) {
        obj_arg = va_arg(ap, MpObj);
        arg_push(obj_arg);
    }
    va_end(ap);

    // mp_inspect_obj(get_mp_local_list());

    // record the length to restore
    int size = tm->local_obj_list->len; 
    MpObj ret = fn(); // execute native function
    LOG(LEVEL_ERROR, "call,%d,%s,end", lineno, func_name, 0);
    // if (size != tm->local_obj_list->len) {
        // if size changed, there must be new allocated objects
        // gc_native_call_sweep(size, ret); // check whether need full gc.
    // }

    // mp_inspect_obj(get_mp_local_list());
    return ret;
}

void def_func(MpObj globals, MpObj name, MpObj (*native)()) {
    MpObj func = func_new(NONE_OBJECT, NONE_OBJECT, native);
    GET_FUNCTION(func)->name = name;
    obj_set(globals,name, func);
}

void def_native_method(MpObj dict, MpObj name, MpObj (*native)()) {
    MpObj func = func_new(NONE_OBJECT, NONE_OBJECT, native);
    MpObj method = method_new(func, dict);
    obj_set(dict, name, method);
}

MpObj mp_take_arg() {
    return arg_take_obj("getarg");
}

void mp_def_mod(char* fname, MpObj mod) {
    MpObj o_name = string_from_cstr(fname);
    obj_set(tm->modules, o_name, mod);
}

MpObj mp_import(MpObj globals, MpObj mod_name) {
    MpObj mod = obj_get(tm->modules, mod_name);
    obj_set(globals, mod_name, mod);
    return mod;
}

// void mp_import_all(MpObj globals, MpObj mod_name) {
//     int b_has = mp_is_in(mod_name, tm->modules);
//     if (b_has) {
//         MpObj mod_value = obj_get(tm->modules, mod_name);
//         // do something here.
//         mp_call_native_2(dict_builtin_update, globals, mod_value);
//     }
// }

/**
 * convert argv to list
 * @param n arguments count
 */
MpObj argv_to_list(int n, ...) {
    va_list ap;
    int i = 0;
    MpObj list = list_new(n);
    va_start(ap, n);
    for (i = 0; i < n; i++) {
        MpObj item = va_arg(ap, MpObj);
        obj_append(list, item);
    }
    va_end(ap);
    return list;
}

/**
 * convert argv to dict
 * @param n arguments count
 */
MpObj argv_to_dict(int n, ...) {
    va_list ap;
    int i = 0;
    MpObj dict = dict_new();
    va_start(ap, n);
    for (i = 0; i < n; i+=2) {
        MpObj key = va_arg(ap, MpObj);
        MpObj val = va_arg(ap, MpObj);
        obj_set(dict, key, val);
    }
    va_end(ap);
    return dict;
}


/** 
 * run c function
 * @param mod_name mocked module name
 */
int mp2c_run_func(int argc, char* argv[], char* mod_name, MpNativeFunc func) {
    int ret = vm_init(argc, argv);
    int i;
    if (ret != 0) { 
        return ret;
    }
    tm->local_obj_list = list_new_untracked(100);
    /* use first frame */
    int code = setjmp(tm->frames->buf);
    if (code == 0) {
        MpObj sys   = GET_DICT_ATTR(tm->modules, "sys");
        MpObj _argv = GET_DICT_ATTR(sys, "argv");
        list_insert(GET_LIST(_argv), 0, string_new(mod_name));

        func();
        
        gc_full();
        // printf("tm->max_allocated = %d\n", tm->max_allocated);
        // printf("tm->allocated = %d\n", tm->allocated);
        // fgetc(stdin);

    } else if (code == 1){
        mp_traceback();
    } else if (code == 2){
        
    }
    vm_destroy();
    return 0;
}

/**
 * get attribute by char array
 * @since 2016-09-02
 */
MpObj tm2c_get(MpObj obj, char* key) {
    MpObj obj_key = string_new(key);
    return obj_get(obj, obj_key);
}

/**
 * get attribute by char array
 * @since 2016-09-02
 */
void tm2c_set(MpObj obj, char* key, MpObj value) {
    MpObj obj_key = string_new(key);
    obj_set(obj, obj_key, value);
}

// float tm2c_numval(MpObj num) {
//     return GET_NUM(num);
// }

MpObj obj_call_narg(MpObj callee, int n, MpObj* args) {
    arg_set_arguments(args, n);
    return obj_call(callee);
}


MpObj mp2c_def_func(MpObj module, char* func_name, MpNativeFunc natvie_func) {
    mp_assert_type(module, TYPE_MODULE, "mp2c_def_func");

    MpObj globals  = obj_get_globals_from_module(module);
    MpObj func_obj = func_new(module, NONE_OBJECT, natvie_func);
    obj_set_by_cstr(globals, func_name, func_obj);
    return func_obj;
}

MpObj mp2c_load_global(MpObj globals, char* name) {
    mp_assert_type(globals, TYPE_DICT, "mp2c_load_global");
    MpObj name_obj = string_static(name);

    int idx = dict_get0(GET_DICT(globals), name_obj);

    if (idx == -1) {
        idx = dict_get0(GET_DICT(tm->builtins), name_obj);
        if (idx == -1) {
            mp_raise("NameError: name %o is not defined", name_obj);
        } else {
            return GET_DICT(tm->builtins)->nodes[idx].val;
        }
    } else {
        return GET_DICT(globals)->nodes[idx].val;
    }

    return NONE_OBJECT;
}

#endif /* TM2C_C */