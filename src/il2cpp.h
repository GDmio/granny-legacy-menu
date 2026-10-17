#pragma once
#include <cstdint>
#include <string>
#include <initializer_list>

// ---------------------------------------------------------------------------
// Basic Unity value types (POD, so MSVC x64 ABI matches IL2CPP generated code)
// ---------------------------------------------------------------------------
struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };
struct Color { float r, g, b, a; };
struct Quat { float x, y, z, w; };
struct Matrix4x4 { float m[16]; };          // column-major: m[col * 4 + row]
struct Bounds { Vec3 center; Vec3 extents; };

inline Vec3 operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 operator*(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
float Length(Vec3 v);

struct Il2CppObject { void* klass; void* monitor; };
struct Il2CppString { Il2CppObject obj; int32_t length; wchar_t chars[1]; };
struct Il2CppArray { Il2CppObject obj; void* bounds; uintptr_t max_length; void* items[1]; };

template <class T> inline T& FieldAt(void* obj, int off) { return *reinterpret_cast<T*>(reinterpret_cast<char*>(obj) + off); }

namespace il2cpp {

bool Init();

void* GetClass(const char* assembly, const char* ns, const char* name);
// Exact overload resolution: if 'types' is non-empty it must match full parameter type names
// (e.g. "System.String", "UnityEngine.Color").
const void* GetMethodInfo(void* klass, const char* name, int argc, std::initializer_list<const char*> types = {});
void* GetMethod(void* klass, const char* name, int argc, std::initializer_list<const char*> types = {});
int GetFieldOffset(void* klass, const char* name);
// Quiet variants for optional members (no log spam when absent)
const void* FindMethodInfo(void* klass, const char* name, int argc);
int FindFieldOffset(void* klass, const char* name);
void* FindField(void* klass, const char* name);
void StaticFieldValue(void* field, void* out);
bool StaticsReady(void* klass);    // static storage allocated (class initialised)
// Calls fn(name, offset) for every instance field declared by the class
void ForEachField(void* klass, void (*fn)(const char* name, int offset, void* ctx), void* ctx);
void* ResolveICall(const char* name);

void* TypeObject(void* klass);                // System.Type for a class
Il2CppString* NewString(const char* utf8);
std::string ToUtf8(Il2CppString* s);
void* NewObject(void* klass);
Il2CppArray* NewArray(void* elemClass, uintptr_t count);
uint32_t GCHandleNew(void* obj);
void GCHandleFree(uint32_t handle);
const char* ClassName(void* obj);             // name of an object's class
void AttachThread();
void DetachThread();

// Unity "fake null": a UnityEngine.Object whose native counterpart died has m_CachedPtr == 0
inline bool Alive(void* unityObj) { return unityObj && *reinterpret_cast<void**>(reinterpret_cast<char*>(unityObj) + 0x10); }

inline uintptr_t ArrayLen(Il2CppArray* a) { return a ? a->max_length : 0; }
inline void* ArrayAt(Il2CppArray* a, uintptr_t i) { return a->items[i]; }

} // namespace il2cpp
