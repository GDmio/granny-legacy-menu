#include "il2cpp.h"
#include "log.h"
#include <Windows.h>
#include <cmath>
#include <cstring>

float Length(Vec3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

namespace {

using domain_get_t = void* (*)();
using domain_get_assemblies_t = void** (*)(void* domain, size_t* size);
using assembly_get_image_t = void* (*)(void* assembly);
using image_get_name_t = const char* (*)(void* image);
using class_from_name_t = void* (*)(void* image, const char* ns, const char* name);
using class_get_methods_t = const void* (*)(void* klass, void** iter);
using method_get_name_t = const char* (*)(const void* method);
using method_get_param_count_t = uint32_t (*)(const void* method);
using method_get_param_t = const void* (*)(const void* method, uint32_t index);
using type_get_name_t = char* (*)(const void* type);
using class_get_field_from_name_t = void* (*)(void* klass, const char* name);
using field_get_offset_t = size_t (*)(void* field);
using field_static_get_value_t = void (*)(void* field, void* value);
using resolve_icall_t = void* (*)(const char* name);
using class_get_type_t = const void* (*)(void* klass);
using type_get_object_t = void* (*)(const void* type);
using string_new_t = Il2CppString* (*)(const char* str);
using object_new_t = void* (*)(void* klass);
using array_new_t = Il2CppArray* (*)(void* elemClass, uintptr_t length);
using gchandle_new_t = uint32_t (*)(void* obj, bool pinned);
using gchandle_free_t = void (*)(uint32_t handle);
using thread_attach_t = void* (*)(void* domain);
using thread_detach_t = void (*)(void* thread);
using thread_current_t = void* (*)();
using class_get_name_t = const char* (*)(void* klass);
using free_t = void (*)(void* ptr);
using class_get_static_field_data_t = void* (*)(void* klass);
using class_get_fields_t = void* (*)(void* klass, void** iter);
using field_get_name_t = const char* (*)(void* field);
using field_get_flags_t = int (*)(void* field);

domain_get_t p_domain_get;
domain_get_assemblies_t p_domain_get_assemblies;
assembly_get_image_t p_assembly_get_image;
image_get_name_t p_image_get_name;
class_from_name_t p_class_from_name;
class_get_methods_t p_class_get_methods;
method_get_name_t p_method_get_name;
method_get_param_count_t p_method_get_param_count;
method_get_param_t p_method_get_param;
type_get_name_t p_type_get_name;
class_get_field_from_name_t p_class_get_field_from_name;
field_get_offset_t p_field_get_offset;
field_static_get_value_t p_field_static_get_value;
resolve_icall_t p_resolve_icall;
class_get_type_t p_class_get_type;
type_get_object_t p_type_get_object;
string_new_t p_string_new;
object_new_t p_object_new;
array_new_t p_array_new;
gchandle_new_t p_gchandle_new;
gchandle_free_t p_gchandle_free;
thread_attach_t p_thread_attach;
thread_detach_t p_thread_detach;
thread_current_t p_thread_current;
class_get_name_t p_class_get_name;
free_t p_free;
class_get_static_field_data_t p_class_get_static_field_data;
class_get_fields_t p_class_get_fields;
field_get_name_t p_field_get_name;
field_get_flags_t p_field_get_flags;

void* g_attachedThread = nullptr;

template <class T> bool Load(HMODULE mod, T& out, const char* name) {
    out = reinterpret_cast<T>(GetProcAddress(mod, name));
    if (!out) LOG("il2cpp export missing: %s", name);
    return out != nullptr;
}

} // namespace

namespace il2cpp {

bool Init() {
    HMODULE ga = GetModuleHandleW(L"GameAssembly.dll");
    if (!ga) return false;
    bool ok = true;
    ok &= Load(ga, p_domain_get, "il2cpp_domain_get");
    ok &= Load(ga, p_domain_get_assemblies, "il2cpp_domain_get_assemblies");
    ok &= Load(ga, p_assembly_get_image, "il2cpp_assembly_get_image");
    ok &= Load(ga, p_image_get_name, "il2cpp_image_get_name");
    ok &= Load(ga, p_class_from_name, "il2cpp_class_from_name");
    ok &= Load(ga, p_class_get_methods, "il2cpp_class_get_methods");
    ok &= Load(ga, p_method_get_name, "il2cpp_method_get_name");
    ok &= Load(ga, p_method_get_param_count, "il2cpp_method_get_param_count");
    ok &= Load(ga, p_method_get_param, "il2cpp_method_get_param");
    ok &= Load(ga, p_type_get_name, "il2cpp_type_get_name");
    ok &= Load(ga, p_class_get_field_from_name, "il2cpp_class_get_field_from_name");
    ok &= Load(ga, p_field_get_offset, "il2cpp_field_get_offset");
    ok &= Load(ga, p_field_static_get_value, "il2cpp_field_static_get_value");
    ok &= Load(ga, p_resolve_icall, "il2cpp_resolve_icall");
    ok &= Load(ga, p_class_get_type, "il2cpp_class_get_type");
    ok &= Load(ga, p_type_get_object, "il2cpp_type_get_object");
    ok &= Load(ga, p_string_new, "il2cpp_string_new");
    ok &= Load(ga, p_object_new, "il2cpp_object_new");
    ok &= Load(ga, p_array_new, "il2cpp_array_new");
    ok &= Load(ga, p_gchandle_new, "il2cpp_gchandle_new");
    ok &= Load(ga, p_gchandle_free, "il2cpp_gchandle_free");
    ok &= Load(ga, p_thread_attach, "il2cpp_thread_attach");
    ok &= Load(ga, p_class_get_name, "il2cpp_class_get_name");
    Load(ga, p_thread_detach, "il2cpp_thread_detach");
    Load(ga, p_thread_current, "il2cpp_thread_current");
    Load(ga, p_free, "il2cpp_free");
    Load(ga, p_class_get_static_field_data, "il2cpp_class_get_static_field_data");
    Load(ga, p_class_get_fields, "il2cpp_class_get_fields");
    Load(ga, p_field_get_name, "il2cpp_field_get_name");
    Load(ga, p_field_get_flags, "il2cpp_field_get_flags");
    return ok;
}

void AttachThread() {
    if (p_thread_current && p_thread_current()) return;
    g_attachedThread = p_thread_attach(p_domain_get());
}

void DetachThread() {
    if (g_attachedThread && p_thread_detach) p_thread_detach(g_attachedThread);
    g_attachedThread = nullptr;
}

void* GetClass(const char* assembly, const char* ns, const char* name) {
    size_t count = 0;
    void** assemblies = p_domain_get_assemblies(p_domain_get(), &count);
    for (size_t i = 0; i < count; ++i) {
        void* image = p_assembly_get_image(assemblies[i]);
        const char* imageName = p_image_get_name(image);
        if (assembly && _stricmp(imageName, assembly) != 0) continue;
        if (void* klass = p_class_from_name(image, ns, name)) return klass;
    }
    LOG("class not found: %s / %s.%s", assembly ? assembly : "*", ns, name);
    return nullptr;
}

namespace {
const void* LookupMethod(void* klass, const char* name, int argc, std::initializer_list<const char*> types) {
    if (!klass) return nullptr;
    void* iter = nullptr;
    while (const void* method = p_class_get_methods(klass, &iter)) {
        if (std::strcmp(p_method_get_name(method), name) != 0) continue;
        if (argc >= 0 && static_cast<int>(p_method_get_param_count(method)) != argc) continue;
        if (types.size()) {
            bool match = true;
            uint32_t idx = 0;
            for (const char* expected : types) {
                char* tn = p_type_get_name(p_method_get_param(method, idx++));
                if (!tn || std::strcmp(tn, expected) != 0) match = false;
                if (tn && p_free) p_free(tn);
                if (!match) break;
            }
            if (!match) continue;
        }
        return method;
    }
    return nullptr;
}
} // namespace

const void* GetMethodInfo(void* klass, const char* name, int argc, std::initializer_list<const char*> types) {
    const void* mi = LookupMethod(klass, name, argc, types);
    if (!mi) LOG("method not found: %s.%s/%d", klass ? p_class_get_name(klass) : "<null>", name, argc);
    return mi;
}

const void* FindMethodInfo(void* klass, const char* name, int argc) { return LookupMethod(klass, name, argc, {}); }

int FindFieldOffset(void* klass, const char* name) {
    void* field = FindField(klass, name);
    return field ? static_cast<int>(p_field_get_offset(field)) : -1;
}

void* FindField(void* klass, const char* name) { return klass ? p_class_get_field_from_name(klass, name) : nullptr; }

void StaticFieldValue(void* field, void* out) { p_field_static_get_value(field, out); }

void ForEachField(void* klass, void (*fn)(const char*, int, void*), void* ctx) {
    if (!klass || !p_class_get_fields || !p_field_get_name) return;
    constexpr int kStatic = 0x10;  // FIELD_ATTRIBUTE_STATIC
    void* iter = nullptr;
    while (void* field = p_class_get_fields(klass, &iter)) {
        if (p_field_get_flags && (p_field_get_flags(field) & kStatic)) continue;
        fn(p_field_get_name(field), static_cast<int>(p_field_get_offset(field)), ctx);
    }
}

bool StaticsReady(void* klass) {
    return klass && p_class_get_static_field_data && p_class_get_static_field_data(klass) != nullptr;
}

void* GetMethod(void* klass, const char* name, int argc, std::initializer_list<const char*> types) {
    const void* mi = GetMethodInfo(klass, name, argc, types);
    return mi ? *reinterpret_cast<void* const*>(mi) : nullptr;  // MethodInfo::methodPointer is the first field
}

int GetFieldOffset(void* klass, const char* name) {
    if (!klass) {
        LOG("field %s: class missing", name);
        return -1;
    }
    void* field = p_class_get_field_from_name(klass, name);
    if (!field) {
        LOG("field not found: %s.%s", p_class_get_name(klass), name);
        return -1;
    }
    return static_cast<int>(p_field_get_offset(field));
}

void* ResolveICall(const char* name) {
    void* p = p_resolve_icall(name);
    if (!p) LOG("icall not found: %s", name);
    return p;
}

void* TypeObject(void* klass) { return klass ? p_type_get_object(p_class_get_type(klass)) : nullptr; }
Il2CppString* NewString(const char* utf8) { return p_string_new(utf8); }
void* NewObject(void* klass) { return p_object_new(klass); }
Il2CppArray* NewArray(void* elemClass, uintptr_t count) { return p_array_new(elemClass, count); }
uint32_t GCHandleNew(void* obj) { return obj ? p_gchandle_new(obj, false) : 0; }
void GCHandleFree(uint32_t handle) { if (handle) p_gchandle_free(handle); }
const char* ClassName(void* obj) { return obj ? p_class_get_name(reinterpret_cast<Il2CppObject*>(obj)->klass) : ""; }

std::string ToUtf8(Il2CppString* s) {
    if (!s || s->length <= 0) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s->chars, s->length, nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s->chars, s->length, out.data(), len, nullptr, nullptr);
    return out;
}

} // namespace il2cpp
