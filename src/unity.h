#pragma once
#include "il2cpp.h"

// Thin wrappers over Unity engine methods, resolved by name at runtime.
// All of them must be called from the Unity main thread.
namespace unity {

bool Init();        // resolve classes and methods (any thread)
bool InitTypes();   // create System.Type objects (main thread, after Init)

extern void* klassRenderer;
extern void* klassMaterial;
extern void* typeRenderer;

// UnityEngine.Object
void Destroy(void* obj);
std::string GetName(void* obj);
Il2CppArray* FindObjectsOfType(void* systemType);

// Component / GameObject
void* GetTransform(void* component);
void* GetGameObject(void* component);
void* GO_GetTransform(void* go);
bool GO_ActiveSelf(void* go);
bool GO_ActiveInHierarchy(void* go);
void GO_SetActive(void* go, bool value);
void* GO_AddComponent(void* go, void* systemType);
void* GO_GetComponent(void* go, void* systemType);
Il2CppArray* GO_GetComponentsInChildren(void* go, void* systemType, bool includeInactive);

// Transform
Vec3 GetPosition(void* transform);
void SetPosition(void* transform, Vec3 pos);
Vec3 GetForward(void* transform);
Vec3 GetRight(void* transform);
Quat GetRotation(void* transform);
void SetRotation(void* transform, Quat q);
void SetLocalRotation(void* transform, Quat q);
Quat GetLocalRotation(void* transform);

// Camera
void* MainCamera();
Matrix4x4 WorldToCameraMatrix(void* cam);
Matrix4x4 ProjectionMatrix(void* cam);
float GetFov(void* cam);
void SetFov(void* cam, float fov);
void SetOcclusionCulling(void* cam, bool on);

// Behaviour / Collider
bool GetEnabled(void* behaviour);
void SetEnabled(void* behaviour, bool value);
bool ColliderGetEnabled(void* collider);
void ColliderSetEnabled(void* collider, bool value);

// Time / Input / Cursor
float DeltaTime();
float UnscaledDeltaTime();
float GetTimeScale();
void SetTimeScale(float value);
bool GetKey(int keyCode);
void SetCursorLock(int mode);
int GetCursorLock();
void SetCursorVisible(bool visible);

// Rendering
bool RendererEnabled(void* renderer);
Bounds RendererBounds(void* renderer);
Il2CppArray* GetSharedMaterials(void* renderer);
void SetSharedMaterials(void* renderer, Il2CppArray* materials);
void* FindShader(const char* name);
void* NewMaterial(void* shader);
void MaterialSetColor(void* mat, const char* prop, Color c);
void MaterialSetFloat(void* mat, const char* prop, float v);
void MaterialSetRenderQueue(void* mat, int queue);
void MaterialEnableKeyword(void* mat, const char* keyword);

// Meshes (for combining sub-meshes so overlay passes cover the whole model)
void* RendererMesh(void* renderer);       // SkinnedMeshRenderer.sharedMesh or MeshFilter.sharedMesh
bool MeshReadable(void* mesh);
int MeshSubMeshCount(void* mesh);
void MeshSetSubMeshCount(void* mesh, int count);
Il2CppArray* MeshTriangles(void* mesh);
void MeshSetTriangles(void* mesh, Il2CppArray* tris, int submesh);

// RenderSettings
bool HasAmbientGetter();
Color GetAmbientLight();
void SetAmbientLight(Color c);
int GetAmbientMode();
void SetAmbientMode(int mode);
bool GetFog();
void SetFog(bool fog);

// NavMeshAgent
bool AgentWarp(void* agent, Vec3 pos);
void AgentSetStopped(void* agent, bool stopped);
bool AgentGetStopped(void* agent);

// Audio
void StopAudio(void* component);  // stops every sound (one-shots included) of the AudioSource on that object

// Keycodes from UnityEngine.KeyCode used by the cheat
namespace key {
constexpr int Space = 32, A = 97, D = 100, E = 101, Q = 113, S = 115, W = 119;
constexpr int LeftShift = 304, LeftControl = 306;
}

} // namespace unity
