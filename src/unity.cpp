#include "unity.h"
#include "log.h"

namespace unity {

void* klassRenderer = nullptr;
void* klassMaterial = nullptr;
void* typeRenderer = nullptr;

namespace {

template <class T> struct Fn {
    T ptr = nullptr;
    void Set(void* p) { ptr = reinterpret_cast<T>(p); }
    explicit operator bool() const { return ptr != nullptr; }
};

using MI = const void*;

Fn<void (*)(void*, MI)> fDestroy;
Fn<Il2CppString* (*)(void*, MI)> fGetName;
Fn<Il2CppArray* (*)(void*, bool)> fFindObjectsOfType;           // icall (Type, includeInactive)
Fn<Il2CppArray* (*)(void*, int, int)> fFindObjectsByType;       // icall (Type, FindObjectsInactive, SortMode)

Fn<void* (*)(void*, MI)> fCompTransform, fCompGameObject, fGoTransform;
Fn<bool (*)(void*, MI)> fGoActiveSelf, fGoActiveInHierarchy;
Fn<void (*)(void*, bool, MI)> fGoSetActive;
Fn<void* (*)(void*, void*, MI)> fGoAddComponent, fGoGetComponent;
Fn<Il2CppArray* (*)(void*, void*, bool, bool, bool, bool, void*)> fGoGetComponentsInternal;  // icall

Fn<Vec3 (*)(void*, MI)> fGetPosition, fGetForward, fGetRight;
Fn<void (*)(void*, Vec3, MI)> fSetPosition;
Fn<Quat (*)(void*, MI)> fGetRotation, fGetLocalRotation;
Fn<void (*)(void*, Quat, MI)> fSetRotation, fSetLocalRotation;

Fn<void* (*)(MI)> fCameraMain;
Fn<Matrix4x4 (*)(void*, MI)> fWorldToCamera, fProjection;
Fn<float (*)(void*, MI)> fGetFov;
Fn<void (*)(void*, float, MI)> fSetFov;
Fn<void (*)(void*, bool, MI)> fSetOcclusion;

Fn<bool (*)(void*, MI)> fBehGetEnabled, fColGetEnabled, fRendGetEnabled;
Fn<void (*)(void*, bool, MI)> fBehSetEnabled, fColSetEnabled;

Fn<float (*)(MI)> fDeltaTime, fUnscaledDeltaTime, fGetTimeScale;
Fn<void (*)(float, MI)> fSetTimeScale;
Fn<bool (*)(int, MI)> fGetKey;
Fn<void (*)(int, MI)> fSetLockState;
Fn<int (*)(MI)> fGetLockState;
Fn<void (*)(bool, MI)> fSetCursorVisible;

Fn<Bounds (*)(void*, MI)> fRendBounds;
Fn<Il2CppArray* (*)(void*, MI)> fGetSharedMats;
Fn<void (*)(void*, Il2CppArray*, MI)> fSetSharedMats;
Fn<void* (*)(Il2CppString*, MI)> fShaderFind;
Fn<void (*)(void*, void*, MI)> fMaterialCtor;
Fn<void (*)(void*, Il2CppString*, Color, MI)> fMatSetColor;
Fn<void (*)(void*, Il2CppString*, float, MI)> fMatSetFloat;
Fn<void (*)(void*, int, MI)> fMatSetRenderQueue;
Fn<void (*)(void*, Il2CppString*, MI)> fMatEnableKeyword;

void* klassSkinned = nullptr;
void* typeMeshFilter = nullptr;
void* klassMeshFilter = nullptr;
Fn<void* (*)(void*, MI)> fSkinnedSharedMesh, fFilterSharedMesh;
Fn<bool (*)(void*)> fMeshReadable;  // icall
Fn<int (*)(void*, MI)> fMeshSubCount;
Fn<void (*)(void*, int, MI)> fMeshSetSubCount;
Fn<Il2CppArray* (*)(void*, MI)> fMeshTriangles;
Fn<void (*)(void*, Il2CppArray*, int, MI)> fMeshSetTriangles;

Fn<void (*)(Color*)> fGetAmbientInjected;  // icall
Fn<void (*)(Color, MI)> fSetAmbient;
Fn<int (*)(MI)> fGetAmbientMode;
Fn<void (*)(int, MI)> fSetAmbientMode;
Fn<bool (*)(MI)> fGetFog;
Fn<void (*)(bool, MI)> fSetFog;

Fn<bool (*)(void*, Vec3, MI)> fAgentWarp;
Fn<void (*)(void*, bool, MI)> fAgentSetStopped;
Fn<bool (*)(void*, MI)> fAgentGetStopped;

void* klassAudio = nullptr;
void* typeAudio = nullptr;
Fn<void (*)(void*, bool, MI)> fAudioStop;

const void* miMaterialCtor = nullptr;

} // namespace

bool Init() {
    using il2cpp::GetClass;
    using il2cpp::GetMethod;
    const char* core = "UnityEngine.CoreModule.dll";

    void* cObject = GetClass(core, "UnityEngine", "Object");
    void* cComponent = GetClass(core, "UnityEngine", "Component");
    void* cGameObject = GetClass(core, "UnityEngine", "GameObject");
    void* cTransform = GetClass(core, "UnityEngine", "Transform");
    void* cCamera = GetClass(core, "UnityEngine", "Camera");
    void* cBehaviour = GetClass(core, "UnityEngine", "Behaviour");
    void* cTime = GetClass(core, "UnityEngine", "Time");
    void* cCursor = GetClass(core, "UnityEngine", "Cursor");
    void* cShader = GetClass(core, "UnityEngine", "Shader");
    void* cRenderSettings = GetClass(core, "UnityEngine", "RenderSettings");
    void* cCollider = GetClass("UnityEngine.PhysicsModule.dll", "UnityEngine", "Collider");
    void* cInput = GetClass("UnityEngine.InputLegacyModule.dll", "UnityEngine", "Input");
    void* cAgent = GetClass("UnityEngine.AIModule.dll", "UnityEngine.AI", "NavMeshAgent");
    klassRenderer = GetClass(core, "UnityEngine", "Renderer");
    klassMaterial = GetClass(core, "UnityEngine", "Material");
    klassAudio = GetClass("UnityEngine.AudioModule.dll", "UnityEngine", "AudioSource");
    fAudioStop.Set(GetMethod(klassAudio, "Stop", 1, { "System.Boolean" }));

    fDestroy.Set(GetMethod(cObject, "Destroy", 1));
    fGetName.Set(GetMethod(cObject, "get_name", 0));
    fFindObjectsOfType.Set(il2cpp::ResolveICall("UnityEngine.Object::FindObjectsOfType"));
    if (!fFindObjectsOfType)
        fFindObjectsByType.Set(il2cpp::ResolveICall(
            "UnityEngine.Object::FindObjectsByType"));

    fCompTransform.Set(GetMethod(cComponent, "get_transform", 0));
    fCompGameObject.Set(GetMethod(cComponent, "get_gameObject", 0));
    fGoTransform.Set(GetMethod(cGameObject, "get_transform", 0));
    fGoActiveSelf.Set(GetMethod(cGameObject, "get_activeSelf", 0));
    fGoActiveInHierarchy.Set(GetMethod(cGameObject, "get_activeInHierarchy", 0));
    fGoSetActive.Set(GetMethod(cGameObject, "SetActive", 1));
    fGoAddComponent.Set(GetMethod(cGameObject, "AddComponent", 1, { "System.Type" }));
    fGoGetComponent.Set(GetMethod(cGameObject, "GetComponent", 1, { "System.Type" }));
    fGoGetComponentsInternal.Set(il2cpp::ResolveICall(
        "UnityEngine.GameObject::GetComponentsInternal"));

    fGetPosition.Set(GetMethod(cTransform, "get_position", 0));
    fSetPosition.Set(GetMethod(cTransform, "set_position", 1));
    fGetForward.Set(GetMethod(cTransform, "get_forward", 0));
    fGetRight.Set(GetMethod(cTransform, "get_right", 0));
    fGetRotation.Set(GetMethod(cTransform, "get_rotation", 0));
    fSetRotation.Set(GetMethod(cTransform, "set_rotation", 1));
    fGetLocalRotation.Set(GetMethod(cTransform, "get_localRotation", 0));
    fSetLocalRotation.Set(GetMethod(cTransform, "set_localRotation", 1));

    fCameraMain.Set(GetMethod(cCamera, "get_main", 0));
    fWorldToCamera.Set(GetMethod(cCamera, "get_worldToCameraMatrix", 0));
    fProjection.Set(GetMethod(cCamera, "get_projectionMatrix", 0));
    fGetFov.Set(GetMethod(cCamera, "get_fieldOfView", 0));
    fSetFov.Set(GetMethod(cCamera, "set_fieldOfView", 1));
    fSetOcclusion.Set(GetMethod(cCamera, "set_useOcclusionCulling", 1));

    fBehGetEnabled.Set(GetMethod(cBehaviour, "get_enabled", 0));
    fBehSetEnabled.Set(GetMethod(cBehaviour, "set_enabled", 1));
    fColGetEnabled.Set(GetMethod(cCollider, "get_enabled", 0));
    fColSetEnabled.Set(GetMethod(cCollider, "set_enabled", 1));
    fRendGetEnabled.Set(GetMethod(klassRenderer, "get_enabled", 0));

    fDeltaTime.Set(GetMethod(cTime, "get_deltaTime", 0));
    fUnscaledDeltaTime.Set(GetMethod(cTime, "get_unscaledDeltaTime", 0));
    fGetTimeScale.Set(GetMethod(cTime, "get_timeScale", 0));
    fSetTimeScale.Set(GetMethod(cTime, "set_timeScale", 1));
    fGetKey.Set(GetMethod(cInput, "GetKey", 1, { "UnityEngine.KeyCode" }));
    fSetLockState.Set(GetMethod(cCursor, "set_lockState", 1));
    fGetLockState.Set(GetMethod(cCursor, "get_lockState", 0));
    fSetCursorVisible.Set(GetMethod(cCursor, "set_visible", 1));

    fRendBounds.Set(GetMethod(klassRenderer, "get_bounds", 0));
    fGetSharedMats.Set(GetMethod(klassRenderer, "get_sharedMaterials", 0));
    fSetSharedMats.Set(GetMethod(klassRenderer, "set_sharedMaterials", 1));
    fShaderFind.Set(GetMethod(cShader, "Find", 1));
    miMaterialCtor = il2cpp::GetMethodInfo(klassMaterial, ".ctor", 1, { "UnityEngine.Shader" });
    if (miMaterialCtor) fMaterialCtor.Set(*reinterpret_cast<void* const*>(miMaterialCtor));
    fMatSetColor.Set(GetMethod(klassMaterial, "SetColor", 2, { "System.String", "UnityEngine.Color" }));
    fMatSetFloat.Set(GetMethod(klassMaterial, "SetFloat", 2, { "System.String", "System.Single" }));
    fMatSetRenderQueue.Set(GetMethod(klassMaterial, "set_renderQueue", 1));
    fMatEnableKeyword.Set(GetMethod(klassMaterial, "EnableKeyword", 1, { "System.String" }));

    void* cMesh = GetClass(core, "UnityEngine", "Mesh");
    klassSkinned = GetClass(core, "UnityEngine", "SkinnedMeshRenderer");
    klassMeshFilter = GetClass(core, "UnityEngine", "MeshFilter");
    fSkinnedSharedMesh.Set(GetMethod(klassSkinned, "get_sharedMesh", 0));
    fFilterSharedMesh.Set(GetMethod(klassMeshFilter, "get_sharedMesh", 0));
    fMeshReadable.Set(il2cpp::ResolveICall("UnityEngine.Mesh::get_isReadable"));
    fMeshSubCount.Set(GetMethod(cMesh, "get_subMeshCount", 0));
    fMeshSetSubCount.Set(GetMethod(cMesh, "set_subMeshCount", 1));
    fMeshTriangles.Set(GetMethod(cMesh, "get_triangles", 0));
    fMeshSetTriangles.Set(GetMethod(cMesh, "SetTriangles", 2, { "System.Int32[]", "System.Int32" }));

    fGetAmbientInjected.Set(il2cpp::ResolveICall("UnityEngine.RenderSettings::get_ambientLight_Injected"));
    fSetAmbient.Set(GetMethod(cRenderSettings, "set_ambientLight", 1));
    fGetAmbientMode.Set(GetMethod(cRenderSettings, "get_ambientMode", 0));
    fSetAmbientMode.Set(GetMethod(cRenderSettings, "set_ambientMode", 1));
    fGetFog.Set(GetMethod(cRenderSettings, "get_fog", 0));
    fSetFog.Set(GetMethod(cRenderSettings, "set_fog", 1));

    fAgentWarp.Set(GetMethod(cAgent, "Warp", 1));
    fAgentSetStopped.Set(GetMethod(cAgent, "set_isStopped", 1));
    fAgentGetStopped.Set(GetMethod(cAgent, "get_isStopped", 0));

    bool critical = fCompTransform && fGetPosition && fSetPosition && fCameraMain && fWorldToCamera && fProjection &&
                    (fFindObjectsOfType || fFindObjectsByType) && fGoSetActive && fBehSetEnabled;
    LOG("unity::Init critical=%d findObjects=%p/%p getComponents=%p", critical, fFindObjectsOfType.ptr,
        fFindObjectsByType.ptr, fGoGetComponentsInternal.ptr);
    return critical;
}

bool InitTypes() {
    typeRenderer = il2cpp::TypeObject(klassRenderer);
    typeMeshFilter = il2cpp::TypeObject(klassMeshFilter);
    typeAudio = il2cpp::TypeObject(klassAudio);
    return typeRenderer != nullptr;
}

void Destroy(void* obj) { if (fDestroy && il2cpp::Alive(obj)) fDestroy.ptr(obj, nullptr); }
std::string GetName(void* obj) { return fGetName && il2cpp::Alive(obj) ? il2cpp::ToUtf8(fGetName.ptr(obj, nullptr)) : std::string(); }

Il2CppArray* FindObjectsOfType(void* systemType) {
    if (!systemType) return nullptr;
    if (fFindObjectsOfType) return fFindObjectsOfType.ptr(systemType, false);
    if (fFindObjectsByType) return fFindObjectsByType.ptr(systemType, 0, 0);
    return nullptr;
}

void* GetTransform(void* c) { return il2cpp::Alive(c) ? fCompTransform.ptr(c, nullptr) : nullptr; }
void* GetGameObject(void* c) { return il2cpp::Alive(c) ? fCompGameObject.ptr(c, nullptr) : nullptr; }
void* GO_GetTransform(void* go) { return il2cpp::Alive(go) ? fGoTransform.ptr(go, nullptr) : nullptr; }
bool GO_ActiveSelf(void* go) { return il2cpp::Alive(go) && fGoActiveSelf.ptr(go, nullptr); }
bool GO_ActiveInHierarchy(void* go) { return il2cpp::Alive(go) && fGoActiveInHierarchy.ptr(go, nullptr); }
void GO_SetActive(void* go, bool v) { if (il2cpp::Alive(go)) fGoSetActive.ptr(go, v, nullptr); }
void* GO_AddComponent(void* go, void* t) { return fGoAddComponent && il2cpp::Alive(go) && t ? fGoAddComponent.ptr(go, t, nullptr) : nullptr; }
void* GO_GetComponent(void* go, void* t) { return fGoGetComponent && il2cpp::Alive(go) && t ? fGoGetComponent.ptr(go, t, nullptr) : nullptr; }

Il2CppArray* GO_GetComponentsInChildren(void* go, void* t, bool includeInactive) {
    if (!fGoGetComponentsInternal || !il2cpp::Alive(go) || !t) return nullptr;
    return fGoGetComponentsInternal.ptr(go, t, true, true, includeInactive, false, nullptr);
}

Vec3 GetPosition(void* t) { return il2cpp::Alive(t) ? fGetPosition.ptr(t, nullptr) : Vec3{}; }
void SetPosition(void* t, Vec3 p) { if (il2cpp::Alive(t)) fSetPosition.ptr(t, p, nullptr); }
Vec3 GetForward(void* t) { return il2cpp::Alive(t) ? fGetForward.ptr(t, nullptr) : Vec3{ 0, 0, 1 }; }
Vec3 GetRight(void* t) { return il2cpp::Alive(t) ? fGetRight.ptr(t, nullptr) : Vec3{ 1, 0, 0 }; }

Quat GetRotation(void* t) { return fGetRotation && il2cpp::Alive(t) ? fGetRotation.ptr(t, nullptr) : Quat{ 0, 0, 0, 1 }; }
void SetRotation(void* t, Quat q) { if (fSetRotation && il2cpp::Alive(t)) fSetRotation.ptr(t, q, nullptr); }
Quat GetLocalRotation(void* t) { return fGetLocalRotation && il2cpp::Alive(t) ? fGetLocalRotation.ptr(t, nullptr) : Quat{ 0, 0, 0, 1 }; }
void SetLocalRotation(void* t, Quat q) { if (fSetLocalRotation && il2cpp::Alive(t)) fSetLocalRotation.ptr(t, q, nullptr); }

void* MainCamera() { return fCameraMain.ptr(nullptr); }
Matrix4x4 WorldToCameraMatrix(void* cam) { return fWorldToCamera.ptr(cam, nullptr); }
Matrix4x4 ProjectionMatrix(void* cam) { return fProjection.ptr(cam, nullptr); }
float GetFov(void* cam) { return fGetFov && il2cpp::Alive(cam) ? fGetFov.ptr(cam, nullptr) : 60.f; }
void SetFov(void* cam, float fov) { if (fSetFov && il2cpp::Alive(cam)) fSetFov.ptr(cam, fov, nullptr); }
void SetOcclusionCulling(void* cam, bool on) { if (fSetOcclusion && il2cpp::Alive(cam)) fSetOcclusion.ptr(cam, on, nullptr); }

bool GetEnabled(void* b) { return fBehGetEnabled && il2cpp::Alive(b) && fBehGetEnabled.ptr(b, nullptr); }
void SetEnabled(void* b, bool v) { if (il2cpp::Alive(b)) fBehSetEnabled.ptr(b, v, nullptr); }
bool ColliderGetEnabled(void* c) { return fColGetEnabled && il2cpp::Alive(c) && fColGetEnabled.ptr(c, nullptr); }
void ColliderSetEnabled(void* c, bool v) { if (fColSetEnabled && il2cpp::Alive(c)) fColSetEnabled.ptr(c, v, nullptr); }

float DeltaTime() { return fDeltaTime ? fDeltaTime.ptr(nullptr) : 0.016f; }
float UnscaledDeltaTime() { return fUnscaledDeltaTime ? fUnscaledDeltaTime.ptr(nullptr) : 0.016f; }
float GetTimeScale() { return fGetTimeScale ? fGetTimeScale.ptr(nullptr) : 1.f; }
void SetTimeScale(float v) { if (fSetTimeScale) fSetTimeScale.ptr(v, nullptr); }
bool GetKey(int k) { return fGetKey && fGetKey.ptr(k, nullptr); }
void SetCursorLock(int mode) { if (fSetLockState) fSetLockState.ptr(mode, nullptr); }
int GetCursorLock() { return fGetLockState ? fGetLockState.ptr(nullptr) : 0; }
void SetCursorVisible(bool v) { if (fSetCursorVisible) fSetCursorVisible.ptr(v, nullptr); }

bool RendererEnabled(void* r) { return fRendGetEnabled && il2cpp::Alive(r) && fRendGetEnabled.ptr(r, nullptr); }
Bounds RendererBounds(void* r) { return fRendBounds && il2cpp::Alive(r) ? fRendBounds.ptr(r, nullptr) : Bounds{}; }
Il2CppArray* GetSharedMaterials(void* r) { return fGetSharedMats && il2cpp::Alive(r) ? fGetSharedMats.ptr(r, nullptr) : nullptr; }
void SetSharedMaterials(void* r, Il2CppArray* m) { if (fSetSharedMats && il2cpp::Alive(r)) fSetSharedMats.ptr(r, m, nullptr); }
void* FindShader(const char* name) { return fShaderFind ? fShaderFind.ptr(il2cpp::NewString(name), nullptr) : nullptr; }

void* NewMaterial(void* shader) {
    if (!fMaterialCtor || !shader) return nullptr;
    void* mat = il2cpp::NewObject(klassMaterial);
    if (mat) fMaterialCtor.ptr(mat, shader, miMaterialCtor);
    return mat;
}

void MaterialSetColor(void* m, const char* p, Color c) { if (fMatSetColor && il2cpp::Alive(m)) fMatSetColor.ptr(m, il2cpp::NewString(p), c, nullptr); }
void MaterialSetFloat(void* m, const char* p, float v) { if (fMatSetFloat && il2cpp::Alive(m)) fMatSetFloat.ptr(m, il2cpp::NewString(p), v, nullptr); }
void MaterialSetRenderQueue(void* m, int q) { if (fMatSetRenderQueue && il2cpp::Alive(m)) fMatSetRenderQueue.ptr(m, q, nullptr); }

void MaterialEnableKeyword(void* m, const char* k) { if (fMatEnableKeyword && il2cpp::Alive(m)) fMatEnableKeyword.ptr(m, il2cpp::NewString(k), nullptr); }

void* RendererMesh(void* r) {
    if (!il2cpp::Alive(r)) return nullptr;
    if (reinterpret_cast<Il2CppObject*>(r)->klass == klassSkinned) return fSkinnedSharedMesh ? fSkinnedSharedMesh.ptr(r, nullptr) : nullptr;
    void* filter = GO_GetComponent(GetGameObject(r), typeMeshFilter);
    return filter && fFilterSharedMesh ? fFilterSharedMesh.ptr(filter, nullptr) : nullptr;
}

bool MeshReadable(void* mesh) { return fMeshReadable && il2cpp::Alive(mesh) && fMeshReadable.ptr(mesh); }
int MeshSubMeshCount(void* mesh) { return fMeshSubCount && il2cpp::Alive(mesh) ? fMeshSubCount.ptr(mesh, nullptr) : 0; }
void MeshSetSubMeshCount(void* mesh, int c) { if (fMeshSetSubCount && il2cpp::Alive(mesh)) fMeshSetSubCount.ptr(mesh, c, nullptr); }
Il2CppArray* MeshTriangles(void* mesh) { return fMeshTriangles && il2cpp::Alive(mesh) ? fMeshTriangles.ptr(mesh, nullptr) : nullptr; }
void MeshSetTriangles(void* mesh, Il2CppArray* t, int s) { if (fMeshSetTriangles && il2cpp::Alive(mesh)) fMeshSetTriangles.ptr(mesh, t, s, nullptr); }

void StopAudio(void* component) {
    void* src = fAudioStop ? GO_GetComponent(GetGameObject(component), typeAudio) : nullptr;
    if (il2cpp::Alive(src)) fAudioStop.ptr(src, true, nullptr);
}

bool HasAmbientGetter() { return fGetAmbientInjected.ptr != nullptr; }
Color GetAmbientLight() { Color c{}; if (fGetAmbientInjected) fGetAmbientInjected.ptr(&c); return c; }
void SetAmbientLight(Color c) { if (fSetAmbient) fSetAmbient.ptr(c, nullptr); }
int GetAmbientMode() { return fGetAmbientMode ? fGetAmbientMode.ptr(nullptr) : 0; }
void SetAmbientMode(int m) { if (fSetAmbientMode) fSetAmbientMode.ptr(m, nullptr); }
bool GetFog() { return fGetFog && fGetFog.ptr(nullptr); }
void SetFog(bool f) { if (fSetFog) fSetFog.ptr(f, nullptr); }

bool AgentWarp(void* a, Vec3 p) { return fAgentWarp && il2cpp::Alive(a) && fAgentWarp.ptr(a, p, nullptr); }
void AgentSetStopped(void* a, bool s) { if (fAgentSetStopped && il2cpp::Alive(a)) fAgentSetStopped.ptr(a, s, nullptr); }
bool AgentGetStopped(void* a) { return fAgentGetStopped && il2cpp::Alive(a) && fAgentGetStopped.ptr(a, nullptr); }

} // namespace unity
