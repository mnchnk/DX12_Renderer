#include "Graphics/Renderer.h"
#include <memory>
#include <array>
#include <d3d12.h>
#include "Graphics/d3dx12.h"
#include <DirectXMath.h>
#include <DirectXColors.h>
#include "Graphics/GraphicsDevice.h"
#include "Graphics/CommandQueue.h"
#include "Graphics/SwapChain.h"
#include "Graphics/Util.h"
#include "Graphics/Vertex.h"
#include "SceneGraph/GameObject.h"
#include "Asset/ModelLoader.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

namespace
{
    // ImGui가 SRV 슬롯이 필요할 때 부르는 콜백.
    // 멤버 함수를 직접 넘길 수 없어서, UserData로 받은 Renderer로 위임한다.
    void ImGuiSrvAlloc(ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
    {
        static_cast<Renderer*>(info->UserData)->AllocImGuiSrv(outCpu, outGpu);
    }

    void ImGuiSrvFree(ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE)
    {
        static_cast<Renderer*>(info->UserData)->FreeImGuiSrv(cpu);
    }
}

Renderer::~Renderer()
{
    // GPU가 ImGui 리소스(폰트 텍스처, 정점 버퍼)를 아직 쓰고 있을 수 있으니 먼저 대기.
    if (mCommandQueue)
        mCommandQueue->FlushCommandQueue();

    if (ImGui::GetCurrentContext())
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
}

void Renderer::AllocImGuiSrv(D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
{
    // 슬롯이 모자라면 ImGuiSrvCount를 늘려야 한다.
    assert(!mImGuiFreeSlots.empty() && "ImGui SRV 슬롯 부족");

    UINT slot = mImGuiFreeSlots.back();
    mImGuiFreeSlots.pop_back();

    *outCpu = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        mSrvHeap->GetCPUDescriptorHandleForHeapStart(), slot, mSrvDescSize);
    *outGpu = CD3DX12_GPU_DESCRIPTOR_HANDLE(
        mSrvHeap->GetGPUDescriptorHandleForHeapStart(), slot, mSrvDescSize);
}

void Renderer::FreeImGuiSrv(D3D12_CPU_DESCRIPTOR_HANDLE cpu)
{
    // 핸들 주소를 역산해서 슬롯 번호를 구한다.
    D3D12_CPU_DESCRIPTOR_HANDLE start = mSrvHeap->GetCPUDescriptorHandleForHeapStart();
    UINT slot = (UINT)((cpu.ptr - start.ptr) / mSrvDescSize);

    mImGuiFreeSlots.push_back(slot);
}

bool Renderer::InitializeImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(mHWnd);

    ImGui_ImplDX12_InitInfo info = {};
    info.Device = mGraphicsDevice->GetDevice();
    info.CommandQueue = mCommandQueue->GetCommandQueue();  // 폰트 텍스처 업로드용
    info.NumFramesInFlight = MaxFrameResource;             // 우리 프레임 리소스 개수와 맞춘다
    info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;           // 백버퍼 포맷과 일치해야 함
    info.SrvDescriptorHeap = mSrvHeap.Get();
    info.SrvDescriptorAllocFn = ImGuiSrvAlloc;
    info.SrvDescriptorFreeFn = ImGuiSrvFree;
    info.UserData = this;

    return ImGui_ImplDX12_Init(&info);
}

void Renderer::BuildDebugUI()
{
    ImGui::Begin("Debug");

    ImGui::Text("%.1f FPS (%.3f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
    ImGui::Text("draw items: %d", (int)mRenderItemsByType[RenderItemType::Opaque].size());

    if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen))
    {
        AnimationPlayer& anim = mScene->GetAnimation();

        if (anim.IsPlaying())
        {
            float t = anim.GetTimeSeconds();
            if (ImGui::SliderFloat("Time", &t, 0.0f, 20.0f))
                anim.SetTimeSeconds(t);

            bool loop = anim.IsLooping();
            if (ImGui::Checkbox("Loop", &loop))
                anim.SetLooping(loop);
        }
        else
        {
            ImGui::TextDisabled("no clip");
        }

        ImGui::Checkbox("Show Skeleton", &mShowSkeleton);
        if (mShowSkeleton)
            ImGui::SliderFloat("Axis Length", &mSkeletonAxisLength, 0.01f, 0.3f);
    }

    if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // 방향광 방향. SyncLights가 Transform에서 방향을 뽑으므로,
        // 여기서 바꾼 값을 GameObject의 회전에 다시 반영한다.
        if (ImGui::SliderFloat3("Direction", &mLightDirection.x, -1.0f, 1.0f))
        {
            for (auto& go : mScene->GetGameObjects())
            {
                if (go->LightData && go->LightData->Type == LightType::Directional)
                    go->GetTransform().SetRotation(MathHelper::QuaternionFromDirection(mLightDirection));
            }
        }
    }

    if (ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (auto& go : mScene->GetGameObjects())
        {
            if (!go->Render) continue;

            ImGui::PushID(go.get());
            if (ImGui::TreeNode(go->GetName().c_str()))
            {
                Transform& tr = go->GetTransform();

                XMFLOAT3 pos = tr.GetPosition();
                if (ImGui::DragFloat3("Position", &pos.x, 0.05f))
                    tr.SetPosition(pos);

                XMFLOAT3 scale = tr.GetScale();
                if (ImGui::DragFloat3("Scale", &scale.x, 0.001f, 0.0001f, 100.0f))
                    tr.SetScale(scale);

                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Shadow Map"))
    {
        // 그림자맵을 그대로 화면에 띄운다. 전치 버그 같은 건 이걸 보면 바로 보인다.
        CD3DX12_GPU_DESCRIPTOR_HANDLE shadowSrv(
            mSrvHeap->GetGPUDescriptorHandleForHeapStart(), mShadowSrvIndex, mSrvDescSize);

        ImGui::Image((ImTextureID)shadowSrv.ptr, ImVec2(256, 256));
    }

    ImGui::End();
}

bool Renderer::Initialize()
{
    mGraphicsDevice = std::make_unique<GraphicsDevice>();
    mCommandQueue = std::make_unique<CommandQueue>();
    mSwapChain = std::make_unique<SwapChain>();

	mGraphicsDevice->Initialize();
	mCommandQueue->Initialize(mGraphicsDevice.get());
	mSwapChain->Initialize(mGraphicsDevice.get(), mCommandQueue.get(), mHWnd, mClientWidth, mClientHeight);

    mScreenViewport.TopLeftX = 0.0f;
    mScreenViewport.TopLeftY = 0.0f;
    mScreenViewport.Width = static_cast<float>(mClientWidth);
    mScreenViewport.Height = static_cast<float>(mClientHeight);
    mScreenViewport.MinDepth = 0.0f; 
    mScreenViewport.MaxDepth = 1.0f; 

    mScissorRect.left = 0;
    mScissorRect.top = 0;
    mScissorRect.right = mClientWidth;
    mScissorRect.bottom = mClientHeight;
    
    ThrowIfFailed(mCommandQueue->GetCommandList()->Reset(mCommandQueue->GetCommandAllocator(), nullptr));
    
    mShadowMap = std::make_unique<ShadowMap>(mGraphicsDevice->GetDevice(), mClientWidth, mClientHeight);
    mScene = std::make_unique<Scene>();

    mResources = std::make_unique<ResourceManager>();
    mResources->Initialize(mGraphicsDevice->GetDevice(), mCommandQueue->GetCommandList());
    mScene->Build(*mResources);
    BuildRenderItemsByType();

    // The root signature and the SRV heap both need the final texture count,
    // so they must come after LoadTextures().
    // InitializeImGui는 SRV 힙에서 슬롯을 받아가므로 반드시 힙 생성 이후여야 한다.
    if (!(
        InitializeRootSignature() &&
        InitializeDescriptorHeaps() &&
        InitializeImGui() &&
        InitializeShadersAndInputLayout() &&
        InitializePSOs()
        )) return false;

    InitializeFrameResource();

    mMainCamera.SetPosition(0.0f, 0.0f, -5.0f);
    mMainCamera.SetLens(0.25f * XM_PI, static_cast<float>(mClientWidth) / mClientHeight, 1.0f, 1000.0f);

    ID3D12GraphicsCommandList* cmdList = mCommandQueue->GetCommandList();
    ThrowIfFailed(cmdList->Close());
    ID3D12CommandList* cmdsLists[] = { cmdList };
    mCommandQueue->GetCommandQueue()->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    mCommandQueue->FlushCommandQueue();

    return true;
}

bool Renderer::InitializeFrameResource()
{

    // 본 팔레트는 캐릭터 하나당 한 벌. 스킨드 캐릭터가 없어도 0개짜리 버퍼는
    // 만들 수 없으므로 최소 1은 확보한다.
    const UINT skinnedCount = (mScene->GetSkinnedCount() > 0) ? mScene->GetSkinnedCount() : 1;

    for (int i = 0; i < MaxFrameResource; i++)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(
            mGraphicsDevice->GetDevice(),
            1,                                              // passCount
            (UINT)mScene->GetAllRenderItems().size(),       // objectCount
            (UINT)mResources->GetMaterialCount(),           // materialCount
            skinnedCount));
    }

    mCurrFrameResourceIndex = 0;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();
    return true;
}

bool Renderer::InitializeRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, -1, 2, 0); // 두 번째 인자가 이 range에 들어갈 SRV 개수 (-1 = unbounded)

    CD3DX12_DESCRIPTOR_RANGE shadowTable;
    shadowTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0);

    CD3DX12_ROOT_PARAMETER slotRootParameter[7];

    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsConstantBufferView(1);
    slotRootParameter[2].InitAsShaderResourceView(0, 0);
    slotRootParameter[3].InitAsDescriptorTable(1, &shadowTable);
    slotRootParameter[4].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);

    // 본 팔레트(cbSkinned, register b2).
    // 루트 파라미터 인덱스(5)와 HLSL register 번호(b2)는 서로 달라도 된다.
    // 기존 번호를 밀지 않으려고 맨 뒤에 붙였다.
    slotRootParameter[5].InitAsConstantBufferView(2);

    // 뼈대 디버그용 (cbBoneDebug, register b3)
    slotRootParameter[6].InitAsConstantBufferView(3);

    auto staticSamplers = GetStaticSamplers();

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(7, slotRootParameter,
        (UINT)staticSamplers.size(), staticSamplers.data(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

      ThrowIfFailed(mGraphicsDevice->GetDevice()->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
    return true;
}

bool Renderer::InitializeDescriptorHeaps()
{
    UINT texCount = mResources->GetTextureManager()->GetTextureCount();
    mSrvDescSize = mGraphicsDevice->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    UINT srvDescSize = mSrvDescSize;

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    // 텍스처들 + 그림자맵 1 + ImGui 예약분
    srvHeapDesc.NumDescriptors = texCount + 1 + ImGuiSrvCount;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mGraphicsDevice->GetDevice()->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvHeap)));

    // DSV heap for the shadow map. Not shader visible - the output merger reads
    // this one, so it never goes through SetDescriptorHeaps and does not
    // conflict with the SRV heap above.
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ThrowIfFailed(mGraphicsDevice->GetDevice()->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mShadowDsvHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE hCpu(mSrvHeap->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_GPU_DESCRIPTOR_HANDLE hGpu(mSrvHeap->GetGPUDescriptorHandleForHeapStart());

    // [0 .. texCount-1] textures
    mResources->GetTextureManager()->InitializeDescriptor(mGraphicsDevice->GetDevice(), hCpu, srvDescSize);

    // [texCount] shadow map
    mShadowSrvIndex = texCount;
    mShadowMap->BuildDescriptor(mGraphicsDevice->GetDevice(),
        CD3DX12_CPU_DESCRIPTOR_HANDLE(hCpu, texCount, srvDescSize),
        CD3DX12_GPU_DESCRIPTOR_HANDLE(hGpu, texCount, srvDescSize),
        mShadowDsvHeap->GetCPUDescriptorHandleForHeapStart());

    // [texCount+1 .. texCount+ImGuiSrvCount] ImGui가 가져다 쓸 슬롯 풀
    mImGuiSrvStart = texCount + 1;
    mImGuiFreeSlots.clear();
    for (UINT i = 0; i < ImGuiSrvCount; ++i)
        mImGuiFreeSlots.push_back(mImGuiSrvStart + i);

    return true;
}

bool Renderer::InitializeShadersAndInputLayout()
{

    const D3D_SHADER_MACRO skinnedDefines[] =
    {
        "SKINNED", "1",
        NULL, NULL          // 배열 끝 표시. 빠뜨리면 컴파일러가 계속 읽는다
    };

    mShaders["standardVS"] = CompileShader(L"Shader\\Default.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["skinnedVS"] = CompileShader(L"Shader\\Default.hlsl", skinnedDefines, "VS", "vs_5_1");
    mShaders["PBRPS"] = CompileShader(L"Shader\\Default.hlsl", nullptr, "PS", "ps_5_1");

    mInputLayouts["default"] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        // 스키닝용. 지금 셰이더는 안 쓰지만 Vertex 구조체와 오프셋을 맞춰둔다.
        // (입력 레이아웃이 셰이더보다 많은 요소를 가져도 문제없다)
        { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT,  0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 60, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    mShaders["shadowVS"] = CompileShader(L"Shader\\ShadowVS.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["skinnedShadowVS"] = CompileShader(L"Shader\\ShadowVS.hlsl", skinnedDefines, "VS", "vs_5_1");

    // 뼈대 디버그. 정점 버퍼 없이 GS로 선을 만들어낸다.
    mShaders["boneDebugVS"] = CompileShader(L"Shader\\BoneDebug.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["boneDebugGS"] = CompileShader(L"Shader\\BoneDebug.hlsl", nullptr, "GS", "gs_5_1");
    mShaders["boneDebugPS"] = CompileShader(L"Shader\\BoneDebug.hlsl", nullptr, "PS", "ps_5_1");

    mInputLayouts["shadow"] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    return true;
}

bool Renderer::InitializePSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { mInputLayouts["default"].data(), (UINT)mInputLayouts["default"].size()};
    psoDesc.pRootSignature = mRootSignature.Get();
    psoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()), mShaders["standardVS"]->GetBufferSize() };
    psoDesc.PS = { reinterpret_cast<BYTE*>(mShaders["PBRPS"]->GetBufferPointer()), mShaders["PBRPS"]->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    ThrowIfFailed(mGraphicsDevice->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = {};
    shadowPsoDesc.InputLayout = { mInputLayouts["shadow"].data(), (UINT)mInputLayouts["shadow"].size()};
    shadowPsoDesc.pRootSignature = mRootSignature.Get();

    shadowPsoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["shadowVS"]->GetBufferPointer()), mShaders["shadowVS"]->GetBufferSize() };
    shadowPsoDesc.PS = { nullptr, 0 };
    shadowPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    shadowPsoDesc.RasterizerState.DepthBias = 100000;
    shadowPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
    shadowPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
    shadowPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    shadowPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    shadowPsoDesc.SampleMask = UINT_MAX;
    shadowPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    shadowPsoDesc.NumRenderTargets = 0;
    shadowPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    shadowPsoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    shadowPsoDesc.SampleDesc.Count = 1;

    ThrowIfFailed(mGraphicsDevice->GetDevice()->CreateGraphicsPipelineState(&shadowPsoDesc, IID_PPV_ARGS(&mPSOs["shadow_opaque"])));

    // ---- 스킨드 버전 ----
    // 나머지 상태는 그대로 두고 VS만 스키닝 버전으로 바꾼다.

    D3D12_GRAPHICS_PIPELINE_STATE_DESC skinnedPsoDesc = psoDesc;
    skinnedPsoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["skinnedVS"]->GetBufferPointer()),
                          mShaders["skinnedVS"]->GetBufferSize() };
    ThrowIfFailed(mGraphicsDevice->GetDevice()->CreateGraphicsPipelineState(&skinnedPsoDesc, IID_PPV_ARGS(&mPSOs["skinned_opaque"])));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC skinnedShadowPsoDesc = shadowPsoDesc;
    // "shadow" 레이아웃은 POSITION만 있어서 본 데이터가 안 들어온다.
    // 스킨드 그림자는 BLENDINDICES/BLENDWEIGHT가 있는 "default" 레이아웃을 써야 한다.
    skinnedShadowPsoDesc.InputLayout = { mInputLayouts["default"].data(), (UINT)mInputLayouts["default"].size() };
    skinnedShadowPsoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["skinnedShadowVS"]->GetBufferPointer()),
                                mShaders["skinnedShadowVS"]->GetBufferSize() };
    ThrowIfFailed(mGraphicsDevice->GetDevice()->CreateGraphicsPipelineState(&skinnedShadowPsoDesc, IID_PPV_ARGS(&mPSOs["skinned_shadow"])));

    // ---- 뼈대 디버그 ----
    D3D12_GRAPHICS_PIPELINE_STATE_DESC boneDebugPsoDesc = psoDesc;

    // 정점 버퍼를 안 쓰므로 입력 레이아웃이 비어 있다.
    // VS가 SV_VertexID로 상수 버퍼를 직접 읽는다.
    boneDebugPsoDesc.InputLayout = { nullptr, 0 };

    boneDebugPsoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["boneDebugVS"]->GetBufferPointer()),
                            mShaders["boneDebugVS"]->GetBufferSize() };
    boneDebugPsoDesc.GS = { reinterpret_cast<BYTE*>(mShaders["boneDebugGS"]->GetBufferPointer()),
                            mShaders["boneDebugGS"]->GetBufferSize() };
    boneDebugPsoDesc.PS = { reinterpret_cast<BYTE*>(mShaders["boneDebugPS"]->GetBufferPointer()),
                            mShaders["boneDebugPS"]->GetBufferSize() };

    // GS가 점을 받으므로 입력 토폴로지는 POINT.
    // 출력이 선이어도 여기는 '입력' 기준이다.
    boneDebugPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;

    // 깊이 테스트를 끈다. 뼈대는 메시 안쪽에 있어서, 켜두면 몸에 가려 안 보인다.
    boneDebugPsoDesc.DepthStencilState.DepthEnable = FALSE;
    boneDebugPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    ThrowIfFailed(mGraphicsDevice->GetDevice()->CreateGraphicsPipelineState(&boneDebugPsoDesc, IID_PPV_ARGS(&mPSOs["bone_debug"])));

    return true;
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> Renderer::GetStaticSamplers()
{
    const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
        0, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        1, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        2, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        3, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
        4, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
        0.0f,                             // mipLODBias
        8);                               // maxAnisotropy

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
        5, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
        0.0f,                              // mipLODBias
        8);                                // maxAnisotropy

    const CD3DX12_STATIC_SAMPLER_DESC shadow(
        6, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        0.0f, 16,
        D3D12_COMPARISON_FUNC_LESS_EQUAL, D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE
    );

    return {
        pointWrap, pointClamp,
        linearWrap, linearClamp,
        anisotropicWrap, anisotropicClamp, shadow };
}

void Renderer::Update(float dt)
{
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % 3;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    if (mCurrFrameResource->Fence != 0 && mCommandQueue->GetFence()->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mCommandQueue->GetFence()->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    // 프레임당 정확히 한 번: NewFrame -> UI 선언 -> Render
    // ImGui는 즉시 모드라 매 프레임 UI를 다시 선언해야 한다.
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    BuildDebugUI();
    ImGui::Render();   // 위젯을 정점 데이터로 변환만 함. GPU 명령은 Draw()에서.

    // 애니메이션은 Scene이 소유한다. 여기서는 시간을 진행시키고
    // 결과 팔레트를 상수 버퍼로 올리기만 한다.
    mScene->GetAnimation().Update(dt);

    SyncTransforms();
    SyncLights();
    UpdateObjectConstants();
    UpdatePassConstants();
    UpdateMaterialBuffer();
    UpdateSkinnedConstants();
    UpdateBoneDebugConstants();

    // UI 위에서 드래그할 때 카메라가 같이 돌아가지 않도록 막는다.
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse && !io.WantCaptureKeyboard)
        mMainCamera.Update(dt);

    InputManager::GetInstance()->ClearDeltas();
}

void Renderer::SyncTransforms()
{
    for (auto& go : mScene->GetGameObjects())
    {
        if (!go->Render) continue;

        XMFLOAT4X4 world;
        XMStoreFloat4x4(&world, go->GetTransform().GetWorldMatrix());

        // 값이 실제로 바뀐 경우에만 더티로 표시한다.
        // 상수 버퍼는 프레임 리소스마다 한 벌씩 있으므로, 한 번 바뀌면
        // MaxFrameResource 프레임 동안 계속 써야 모든 벌에 반영된다.
        // 이 표시를 빼먹으면 CPU쪽 World만 바뀌고 GPU는 옛날 값을 계속 본다.
        if (memcmp(&world, &go->Render->World, sizeof(XMFLOAT4X4)) != 0)
        {
            go->Render->World = world;
            go->Render->NumFramesDirty = MaxFrameResource;
        }
    }
}

void Renderer::SyncLights()
{
    for (auto& go : mScene->GetGameObjects())
    {
        if (!go->LightData) continue;

        XMMATRIX world = go->GetTransform().GetWorldMatrix();
        Light* light = go->LightData;

        // Position only means something for Point/Spot. Filter by type so we do
        // not clobber a Directional light's Position.
        if (light->Type == LightType::Point || light->Type == LightType::Spot)
        {
            XMStoreFloat3(&light->Position, world.r[3]); // row 3 of world = translation
        }

        // Direction only means something for Directional/Spot. Never overwrite a
        // Point light's Direction.
        if (light->Type == LightType::Directional || light->Type == LightType::Spot)
        {
            // Transform local forward (0,0,1) by rotation only.
            XMVECTOR baseForward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
            XMVECTOR worldDir = XMVector3TransformNormal(baseForward, world); // TransformNormal ignores translation
            XMStoreFloat3(&light->Direction, XMVector3Normalize(worldDir));
        }
    }
}

void Renderer::UpdateObjectConstants()
{
    auto currObjectCB = mCurrFrameResource->ObjectCB.get();

    for (auto& e : mScene->GetAllRenderItems())
    {
        if (e->NumFramesDirty > 0)
        {
            XMMATRIX world = XMLoadFloat4x4(&e->World);
            XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

            ObjectConstants objConstants;
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
            XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
            
            objConstants.MaterialIndex = e->Mat->MatCBIndex;

            currObjectCB->CopyData(e->ObjectCBIndex, objConstants);

            e->NumFramesDirty--;
        }
    }
}

void Renderer::UpdatePassConstants()
{
    XMMATRIX view = mMainCamera.GetView();
    XMMATRIX proj = mMainCamera.GetProj();

    XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
    XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
    XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

    XMStoreFloat4x4(&mPassCB.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&mPassCB.InvView, XMMatrixTranspose(invView));
    XMStoreFloat4x4(&mPassCB.Proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&mPassCB.InvProj, XMMatrixTranspose(invProj));
    XMStoreFloat4x4(&mPassCB.ViewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&mPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
    mPassCB.EyePosW = mMainCamera.GetPosition3f();
    mPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    mPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
    mPassCB.NearZ = 1.0f;
    mPassCB.FarZ = 1000.0f;

    mPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };

    // The shader reads gLights as fixed slots ordered [Directional][Point][Spot].
    // unordered_map iteration order is not guaranteed, so the slot must be chosen
    // by light type, never by iteration order.
    int dirSlot = 0;
    int pointSlot = NumDirLights;
    int spotSlot = NumDirLights + NumPointLights;

    for (auto& e : mScene->GetAllLights())
    {
        for (auto& light : e.second)
        {
            switch (light->Type)
            {
            case LightType::Directional:
                if (dirSlot < NumDirLights)
                    mPassCB.Lights[dirSlot++] = light->ToLightData();
                break;

            case LightType::Point:
                if (pointSlot < NumDirLights + NumPointLights)
                    mPassCB.Lights[pointSlot++] = light->ToLightData();
                break;

            case LightType::Spot:
                if (spotSlot < NumDirLights + NumPointLights + NumSpotLights)
                    mPassCB.Lights[spotSlot++] = light->ToLightData();
                break;
            }
        }
    }

    float sceneRadius = 10.0f;

    XMVECTOR lightDir = XMLoadFloat3(&mScene->GetMainLight()->Direction);
    XMVECTOR lightPos = -2.0f * sceneRadius * lightDir; // 광원 방향 반대편으로 씬 반지름의 2배만큼 물러난 위치
    XMVECTOR targetPos = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, up);
    XMMATRIX lightProj = XMMatrixOrthographicLH(20.0f, 20.0f, 1.0f, 40.0f);
    XMMATRIX lightViewProj = lightView * lightProj;

    // HLSL constant buffers read matrices as column-major, so transpose before
    // uploading. Same rule as the camera matrices above.
    XMStoreFloat4x4(&mPassCB.LightView, XMMatrixTranspose(lightView));
    XMStoreFloat4x4(&mPassCB.LightProj, XMMatrixTranspose(lightProj));
    XMStoreFloat4x4(&mPassCB.LightViewProj, XMMatrixTranspose(lightViewProj));

    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(0, mPassCB);
}

void Renderer::UpdateMaterialBuffer()
{
    auto currMaterialBuffer = mCurrFrameResource->MaterialBuffer.get();

    for (auto& e : mResources->GetAllMaterials())
    {
        Material* mat = e.second.get();
        if (mat->NumFramesDirty)
        {
            MaterialData matData;
            matData.DiffuseAlbedo = mat->DiffuseAlbedo;
            matData.FresnelR0 = mat->FresnelR0;
            matData.Roughness = mat->Roughness;
            matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;
            matData.NormalMapIndex = mat->NormalSrvHeapIndex;
            XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);
            XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));

            currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

            mat->NumFramesDirty--;
        }
    }
}

void Renderer::BuildRenderItemsByType()
{
    mRenderItemsByType.clear();

    for (auto& go: mScene->GetGameObjects())
    {
        if (!go->Render) continue;

        // PSO가 다르므로 목록을 나눠 담는다.
        RenderItemType type = (go->Render->SkinnedCBIndex >= 0)
            ? RenderItemType::SkinnedOpaque
            : RenderItemType::Opaque;

        mRenderItemsByType[type].push_back(go->Render);
    }
}

void Renderer::UpdateSkinnedConstants()
{
    const auto& transforms = mScene->GetAnimation().GetBoneTransforms();
    if (transforms.empty()) return;

    SkinnedConstants sc;

    // 상수 버퍼에 올릴 땐 전치해야 한다.
    // HLSL은 행렬을 column-major로 읽고 DirectXMath는 row-major로 저장하기 때문.
    const size_t count = (transforms.size() < 96) ? transforms.size() : 96;
    for (size_t i = 0; i < count; ++i)
    {
        XMStoreFloat4x4(&sc.BoneTransforms[i],
            XMMatrixTranspose(XMLoadFloat4x4(&transforms[i])));
    }

    mCurrFrameResource->SkinnedCB->CopyData(0, sc);
}

void Renderer::UpdateBoneDebugConstants()
{
    if (!mShowSkeleton) return;

    const AnimationPlayer& anim = mScene->GetAnimation();
    const Skeleton* skeleton = anim.GetSkeleton();
    if (skeleton == nullptr) return;

    const auto& worlds = anim.GetBoneWorldTransforms();
    if (worlds.empty()) return;

    BoneDebugConstants bc;
    bc.BoneCount = (UINT)((worlds.size() < 96) ? worlds.size() : 96);
    bc.AxisLength = mSkeletonAxisLength;

    for (UINT i = 0; i < bc.BoneCount; ++i)
    {
        XMStoreFloat4x4(&bc.BoneWorld[i], XMMatrixTranspose(XMLoadFloat4x4(&worlds[i])));
        bc.BoneParent[i] = XMINT4(skeleton->Bones[i].ParentIndex, 0, 0, 0);
    }

    // 본은 모델 공간 좌표다. 캐릭터 오브젝트의 월드 변환(위치/스케일)을 곱해야
    // 화면상의 캐릭터와 겹친다.
    XMMATRIX rootWorld = XMMatrixIdentity();
    const auto& skinned = mRenderItemsByType[RenderItemType::SkinnedOpaque];
    if (!skinned.empty())
        rootWorld = XMLoadFloat4x4(&skinned[0]->World);

    XMStoreFloat4x4(&bc.RootWorld, XMMatrixTranspose(rootWorld));

    mCurrFrameResource->BoneDebugCB->CopyData(0, bc);
}

void Renderer::Draw()
{
    auto cmdAllocator = mCurrFrameResource->CmdAllocator.Get();
    auto commandList = mCommandQueue->GetCommandList();

    ThrowIfFailed(cmdAllocator->Reset());
    
    //shadow pass
    ThrowIfFailed(commandList->Reset(cmdAllocator, mPSOs["shadow_opaque"].Get()));

    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mShadowMap->GetResource(),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_DEPTH_WRITE));

    commandList->RSSetViewports(1, &mShadowMap->GetViewport());
    commandList->RSSetScissorRects(1, &mShadowMap->GetScissorRect());

    commandList->ClearDepthStencilView(mShadowMap->Dsv(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    commandList->OMSetRenderTargets(0, nullptr, false, &mShadowMap->Dsv());
    
    commandList->SetGraphicsRootSignature(mRootSignature.Get());
    auto passCB = mCurrFrameResource->PassCB->Resource();
    commandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

    // 타입마다 PSO가 다르므로 나눠서 그린다.
    commandList->SetPipelineState(mPSOs["shadow_opaque"].Get());
    DrawRenderItems(commandList, mRenderItemsByType[RenderItemType::Opaque]);

    commandList->SetPipelineState(mPSOs["skinned_shadow"].Get());
    DrawRenderItems(commandList, mRenderItemsByType[RenderItemType::SkinnedOpaque]);

    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mShadowMap->GetResource(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_GENERIC_READ));

    //main pass
    commandList->SetPipelineState(mPSOs["opaque"].Get());
    commandList->RSSetViewports(1, &mScreenViewport);
    commandList->RSSetScissorRects(1, &mScissorRect);

    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mSwapChain->GetCurrentRenderTarget(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    commandList->ClearRenderTargetView(mSwapChain->GetCurrentRtvHandle(), Colors::LightSteelBlue, 0, nullptr);
    commandList->ClearDepthStencilView(mSwapChain->GetCurrentDsvHandle(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
    
    commandList->OMSetRenderTargets(1, &mSwapChain->GetCurrentRtvHandle(), true, &mSwapChain->GetCurrentDsvHandle());
    
    ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetGraphicsRootSignature(mRootSignature.Get());
    commandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

    auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
    commandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(3, mShadowMap->Srv());
    commandList->SetGraphicsRootDescriptorTable(4, mSrvHeap->GetGPUDescriptorHandleForHeapStart());

    commandList->SetPipelineState(mPSOs["opaque"].Get());
    DrawRenderItems(commandList, mRenderItemsByType[RenderItemType::Opaque]);

    commandList->SetPipelineState(mPSOs["skinned_opaque"].Get());
    DrawRenderItems(commandList, mRenderItemsByType[RenderItemType::SkinnedOpaque]);

    DrawSkeletonDebug(commandList);

    // UI는 씬 위에 겹쳐 그려야 하므로 마지막.
    // 아직 백버퍼가 RENDER_TARGET 상태이고 mSrvHeap이 바인딩된 시점이어야 한다.
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);

    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mSwapChain->GetCurrentRenderTarget(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(commandList->Close());

    ID3D12CommandList* cmdsLists[] = { commandList };
    mCommandQueue->GetCommandQueue()->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    mSwapChain->Present();
    mCurrFrameResource->Fence = ++mCommandQueue->mCurrFence;

    mCommandQueue->GetCommandQueue()->Signal(mCommandQueue->GetFence(), mCommandQueue->mCurrFence);
}

void Renderer::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT skinnedCBByteSize = CalcConstantBufferByteSize(sizeof(SkinnedConstants));

    auto objectCB = mCurrFrameResource->ObjectCB->Resource();
    auto skinnedCB = mCurrFrameResource->SkinnedCB->Resource();

    for (int i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjectCBIndex * objCBByteSize;

        cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

        // 스킨드 아이템만 본 팔레트를 묶는다.
        // 루트 파라미터 5번 = HLSL의 register(b2).
        if (ri->SkinnedCBIndex >= 0)
        {
            D3D12_GPU_VIRTUAL_ADDRESS skinnedAddress =
                skinnedCB->GetGPUVirtualAddress() + ri->SkinnedCBIndex * skinnedCBByteSize;

            cmdList->SetGraphicsRootConstantBufferView(5, skinnedAddress);
        }

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

void Renderer::DrawSkeletonDebug(ID3D12GraphicsCommandList* cmdList)
{
    if (!mShowSkeleton) return;

    const Skeleton* skeleton = mScene->GetAnimation().GetSkeleton();
    if (skeleton == nullptr || skeleton->Bones.empty()) return;

    const UINT boneCount = (UINT)((skeleton->Bones.size() < 96) ? skeleton->Bones.size() : 96);

    cmdList->SetPipelineState(mPSOs["bone_debug"].Get());

    auto boneDebugCB = mCurrFrameResource->BoneDebugCB->Resource();
    cmdList->SetGraphicsRootConstantBufferView(6, boneDebugCB->GetGPUVirtualAddress());

    // 정점 버퍼도 인덱스 버퍼도 없다.
    // "점 boneCount개를 그려라"라고만 하면 VS가 SV_VertexID로 알아서 읽어간다.
    cmdList->IASetVertexBuffers(0, 0, nullptr);
    cmdList->IASetIndexBuffer(nullptr);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);

    cmdList->DrawInstanced(boneCount, 1, 0, 0);
}

void Renderer::Pick(int sx, int sy)
{
    XMFLOAT4X4 P = mMainCamera.GetProj4x4();
    float vx = (+2.0f * sx / mClientWidth - 1.0f) / P(0, 0);
    float vy = (-2.0f * sy / mClientHeight + 1.0f) / P(1, 1);

    XMVECTOR rayOrigin = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    XMVECTOR rayDir = XMVectorSet(vx, vy, 1.0f, 0.0f);

    XMMATRIX V = mMainCamera.GetView();
    XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(V), V);

    XMVECTOR rayOriginW = XMVector3TransformCoord(rayOrigin, invView);
    XMVECTOR rayDirW = XMVector3TransformNormal(rayDir, invView);
    rayDirW = XMVector3Normalize(rayDirW);

    RenderItem* pickedItem = nullptr;
    float tMin = 9999999.0f; 

    for (auto& ri : mScene->GetAllRenderItems())
    {
        XMMATRIX W = XMLoadFloat4x4(&ri->World);
        DirectX::BoundingBox worldBounds;
        ri->Bounds.Transform(worldBounds, W);

        float t = 0.0f; 

        if (worldBounds.Intersects(rayOriginW, rayDirW, t))
        {
            if (t < tMin)
            {
                tMin = t;
                pickedItem = ri.get();
            }
        }
    }
}