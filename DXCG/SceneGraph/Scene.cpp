#include "SceneGraph/Scene.h"
#include "Graphics/ResourceManager.h"
#include "Core/MathHelper.h"

#include <Windows.h>   // OutputDebugStringA

using namespace DirectX;

namespace
{
    // 모델 파일명(확장자 제외)과 같아야 한다. ModelLoader가 geo->Name을 이걸로 정한다.
    const char* kCharacterGeoName = "Ely By K.Atienza";
}

void Scene::Build(ResourceManager& resources)
{
    BuildLights();
    BuildRenderItems(resources);
}

void Scene::BuildLights()
{
    auto mainDirectionalLight = std::make_unique<Light>();
    mainDirectionalLight->Type = LightType::Directional;
    mainDirectionalLight->Direction = { 0.57735f, -0.57735f, 0.57735f };
    mainDirectionalLight->Strength = { 0.8f, 0.8f, 0.8f };

    auto pointLight1 = std::make_unique<Light>();
    pointLight1->Type = LightType::Point;
    pointLight1->Position = { 0.0f, 10.0f, 0.0f };
    pointLight1->Strength = { 0.8f, 0.8f, 0.8f };

    GameObject* go = CreateGameObject("mainDirectionalLight");
    go->GetTransform().SetRotation(MathHelper::QuaternionFromDirection(mainDirectionalLight->Direction));

    // CreateLight가 소유권을 가져가므로, 반환된 포인터를 참조용으로 쓴다.
    // (move 이후에는 mainDirectionalLight가 비어 있다)
    mMainLight = CreateLight("Directional", mainDirectionalLight);
    go->LightData = mMainLight;

    go = CreateGameObject("pointLight1");
    Light* point = CreateLight("Point", pointLight1);
    go->LightData = point;
    go->GetTransform().SetPosition(point->Position);
}

void Scene::BuildRenderItems(ResourceManager& resources)
{
    UINT objCBIndex = 0;

    MeshGeometry* charGeo = resources.GetGeometry(kCharacterGeoName);
    const LoadedModel& model = resources.GetCharacterModel();

    // 모델 로드에 실패하면 charGeo가 null이다. 여기서 걸러내지 않으면
    // 아래 DrawArgs 접근에서 널 역참조로 죽는다.
    if (charGeo == nullptr)
    {
        OutputDebugStringA("[Scene] character geometry not found, skipping\n");
    }
    else
    {
        // 서브메시 하나당 RenderItem 하나
        for (const LoadedSubmesh& sub : model.Submeshes)
        {
            auto ritem = std::make_unique<RenderItem>();

            ritem->Name = sub.Name;
            XMStoreFloat4x4(&ritem->World, XMMatrixIdentity());

            ritem->ObjectCBIndex = objCBIndex++;
            ritem->Geo = charGeo;
            ritem->Mat = resources.GetMaterial(model.Materials[sub.MaterialIndex].Name);
            ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

            const SubmeshGeometry& sm = charGeo->DrawArgs[sub.Name];
            ritem->IndexCount = sm.IndexCount;
            ritem->StartIndexLocation = sm.StartIndexLocation;
            ritem->BaseVertexLocation = sm.BaseVertexLocation;
            ritem->Bounds = sm.Bounds;
            ritem->NumFramesDirty = MaxFrameResource;

            GameObject* go = CreateGameObject(sub.Name);
            go->Render = ritem.get();
            go->GetTransform().SetPosition(XMFLOAT3(0.0f, -1.0f, 0.0f));
            go->GetTransform().SetScale(XMFLOAT3(0.01f, 0.01f, 0.01f));   // Mixamo 모델은 센티미터 단위

            CreateRenderItem(ritem);
        }
    }

    // 바닥. objCBIndex를 이어받는다 (ObjectCB 슬롯이 0부터 빈틈없이 유일해야 함)
    MeshGeometry* groundGeo = resources.GetGeometry("groundGeo");
    if (groundGeo == nullptr)
    {
        OutputDebugStringA("[Scene] groundGeo not found, skipping\n");
        return;
    }

    auto groundRitem = std::make_unique<RenderItem>();

    groundRitem->Name = "ground";
    XMStoreFloat4x4(&groundRitem->World, XMMatrixIdentity());

    groundRitem->ObjectCBIndex = objCBIndex++;
    groundRitem->Geo = groundGeo;
    groundRitem->Mat = resources.GetMaterial("wood");
    groundRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    const SubmeshGeometry& grid = groundGeo->DrawArgs["grid"];
    groundRitem->IndexCount = grid.IndexCount;
    groundRitem->StartIndexLocation = grid.StartIndexLocation;
    groundRitem->BaseVertexLocation = grid.BaseVertexLocation;

    groundRitem->NumFramesDirty = MaxFrameResource;

    groundRitem->Bounds.Center = XMFLOAT3(0.0f, -1.0f, 0.0f);
    groundRitem->Bounds.Extents = XMFLOAT3(10.0f, 0.01f, 10.0f);

    GameObject* go = CreateGameObject(groundRitem->Name);
    go->Render = groundRitem.get();
    go->GetTransform().SetPosition(XMFLOAT3(0.0f, 0.0f, 0.0f));

    CreateRenderItem(groundRitem);
}
