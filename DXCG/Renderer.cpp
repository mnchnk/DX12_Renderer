#include "Renderer.h"
#include <memory>
#include <array>
#include <d3d12.h>
#include "d3dx12.h"
#include <DirectXMath.h>
#include <DirectXColors.h>
#include "GraphicsDevice.h"
#include "CommandQueue.h"
#include "SwapChain.h"
#include "Util.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

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

    if (!(
        InitializeRootSignature() &&
        InitializeDescriptorHeaps() &&
        InitializeShadersAndInputLayout() &&
        InitializePSOs()
        )) return false;

    InitializeLights();
    InitializeShapesGeometry();
    InitializeMaterials();
    InitializeRenderItem();
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

    for (int i = 0; i < MaxFrameResource; i++)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(mGraphicsDevice->GetDevice(), 1, (UINT)mAllRenderItems.size(), (UINT)mMaterials.size()));
    }

    mCurrFrameResourceIndex = 0;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();
    return true;
}

bool Renderer::InitializeRootSignature()
{
    //CD3DX12_DESCRIPTOR_RANGE texTable;
    //texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 0, 0); // 2번째 파라미터는 텍스처 개수

    CD3DX12_DESCRIPTOR_RANGE shadowTable;
    shadowTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0);

    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsConstantBufferView(1);
    slotRootParameter[2].InitAsShaderResourceView(0, 0);
    slotRootParameter[3].InitAsDescriptorTable(1, &shadowTable);
    //slotRootParameter[3].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);

    auto staticSamplers = GetStaticSamplers();

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
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
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 1;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mGraphicsDevice->GetDevice()->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mShadowSrvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ThrowIfFailed(mGraphicsDevice->GetDevice()->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mShadowDsvHeap)));

    mShadowMap->BuildDescriptor(mGraphicsDevice->GetDevice(),
        mShadowSrvHeap->GetCPUDescriptorHandleForHeapStart(),
        mShadowSrvHeap->GetGPUDescriptorHandleForHeapStart(),
        mShadowDsvHeap->GetCPUDescriptorHandleForHeapStart());
    return true;
}

bool Renderer::InitializeShadersAndInputLayout()
{
    mShaders["standardVS"] = CompileShader(L"Default.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["PBRPS"] = CompileShader(L"Default.hlsl", nullptr, "PS", "ps_5_1");

    mInputLayouts["default"] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    mShaders["shadowVS"] = CompileShader(L"ShadowVS.hlsl", nullptr, "VS", "vs_5_1");

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
    //컬러를 쓰지 않으므로 렌더 타겟 개수는 0개, 포맷은 UNKNOWN
    shadowPsoDesc.NumRenderTargets = 0;
    shadowPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    shadowPsoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    shadowPsoDesc.SampleDesc.Count = 1;

    ThrowIfFailed(mGraphicsDevice->GetDevice()->CreateGraphicsPipelineState(&shadowPsoDesc, IID_PPV_ARGS(&mPSOs["shadow_opaque"])));
    return true;
}

void Renderer::InitializeShapesGeometry()
{
    // 1. 24개의 정점 배열 생성 (위치, 법선, UV)
    // 각 면마다 4개의 정점을 가집니다. (Front, Back, Top, Bottom, Left, Right)
    std::array<Vertex, 24> vertices =
    {
        // 앞면 (Front) - 법선 Z는 -1
        Vertex({ XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT2(0.0f, 1.0f) }),
        Vertex({ XMFLOAT3(-0.5f, +0.5f, -0.5f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT2(0.0f, 0.0f) }),
        Vertex({ XMFLOAT3(+0.5f, +0.5f, -0.5f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT2(1.0f, 0.0f) }),
        Vertex({ XMFLOAT3(+0.5f, -0.5f, -0.5f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT2(1.0f, 1.0f) }),

        // 뒷면 (Back) - 법선 Z는 +1
        Vertex({ XMFLOAT3(-0.5f, -0.5f, +0.5f), XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT2(1.0f, 1.0f) }),
        Vertex({ XMFLOAT3(+0.5f, -0.5f, +0.5f), XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT2(0.0f, 1.0f) }),
        Vertex({ XMFLOAT3(+0.5f, +0.5f, +0.5f), XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT2(0.0f, 0.0f) }),
        Vertex({ XMFLOAT3(-0.5f, +0.5f, +0.5f), XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT2(1.0f, 0.0f) }),

        // 윗면 (Top) - 법선 Y는 +1
        Vertex({ XMFLOAT3(-0.5f, +0.5f, -0.5f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) }),
        Vertex({ XMFLOAT3(-0.5f, +0.5f, +0.5f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) }),
        Vertex({ XMFLOAT3(+0.5f, +0.5f, +0.5f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) }),
        Vertex({ XMFLOAT3(+0.5f, +0.5f, -0.5f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) }),

        // 아랫면 (Bottom) - 법선 Y는 -1
        Vertex({ XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) }),
        Vertex({ XMFLOAT3(+0.5f, -0.5f, -0.5f), XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) }),
        Vertex({ XMFLOAT3(+0.5f, -0.5f, +0.5f), XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) }),
        Vertex({ XMFLOAT3(-0.5f, -0.5f, +0.5f), XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) }),

        // 왼쪽면 (Left) - 법선 X는 -1
        Vertex({ XMFLOAT3(-0.5f, -0.5f, +0.5f), XMFLOAT3(-1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) }),
        Vertex({ XMFLOAT3(-0.5f, +0.5f, +0.5f), XMFLOAT3(-1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) }),
        Vertex({ XMFLOAT3(-0.5f, +0.5f, -0.5f), XMFLOAT3(-1.0f, 0.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) }),
        Vertex({ XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(-1.0f, 0.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) }),

        // 오른쪽면 (Right) - 법선 X는 +1
        Vertex({ XMFLOAT3(+0.5f, -0.5f, -0.5f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) }),
        Vertex({ XMFLOAT3(+0.5f, +0.5f, -0.5f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) }),
        Vertex({ XMFLOAT3(+0.5f, +0.5f, +0.5f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) }),
        Vertex({ XMFLOAT3(+0.5f, -0.5f, +0.5f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) })
    };

    std::array<std::uint16_t, 36> indices =
    {
        // 앞면
        0, 1, 2,  0, 2, 3,
        // 뒷면
        4, 5, 6,  4, 6, 7,
        // 윗면
        8, 9, 10, 8, 10, 11,
        // 아랫면
        12, 13, 14, 12, 14, 15,
        // 왼쪽면
        16, 17, 18, 16, 18, 19,
        // 오른쪽면
        20, 21, 22, 20, 22, 23
    };

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "boxGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = CreateDefaultBuffer(mGraphicsDevice->GetDevice(),
        mCommandQueue->GetCommandList(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = CreateDefaultBuffer(mGraphicsDevice->GetDevice(),
        mCommandQueue->GetCommandList(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["box"] = submesh;

    mGeometries[geo->Name] = std::move(geo);

    std::array<Vertex, 4> groundVertices =
    {
        Vertex({ XMFLOAT3(-10.0f, -1.0f, -10.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 5.0f) }),
        Vertex({ XMFLOAT3(-10.0f, -1.0f, +10.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) }),
        Vertex({ XMFLOAT3(+10.0f, -1.0f, +10.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(5.0f, 0.0f) }),
        Vertex({ XMFLOAT3(+10.0f, -1.0f, -10.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(5.0f, 5.0f) }),
    };

    std::array<std::uint16_t, 6> groundIndices =
    {
        0, 1, 2,  0, 2, 3
    };

    const UINT groundVbByteSize = (UINT)groundVertices.size() * sizeof(Vertex);
    const UINT groundIbByteSize = (UINT)groundIndices.size() * sizeof(std::uint16_t);

    auto groundGeo = std::make_unique<MeshGeometry>();
    groundGeo->Name = "groundGeo";

    ThrowIfFailed(D3DCreateBlob(groundVbByteSize, &groundGeo->VertexBufferCPU));
    CopyMemory(groundGeo->VertexBufferCPU->GetBufferPointer(), groundVertices.data(), groundVbByteSize);

    ThrowIfFailed(D3DCreateBlob(groundIbByteSize, &groundGeo->IndexBufferCPU));
    CopyMemory(groundGeo->IndexBufferCPU->GetBufferPointer(), groundIndices.data(), groundIbByteSize);

    groundGeo->VertexBufferGPU = CreateDefaultBuffer(mGraphicsDevice->GetDevice(),
        mCommandQueue->GetCommandList(), groundVertices.data(), groundVbByteSize, groundGeo->VertexBufferUploader);

    groundGeo->IndexBufferGPU = CreateDefaultBuffer(mGraphicsDevice->GetDevice(),
        mCommandQueue->GetCommandList(), groundIndices.data(), groundIbByteSize, groundGeo->IndexBufferUploader);

    groundGeo->VertexByteStride = sizeof(Vertex);
    groundGeo->VertexBufferByteSize = groundVbByteSize;
    groundGeo->IndexFormat = DXGI_FORMAT_R16_UINT;
    groundGeo->IndexBufferByteSize = groundIbByteSize;

    SubmeshGeometry groundSubmesh;
    groundSubmesh.IndexCount = (UINT)groundIndices.size();
    groundSubmesh.StartIndexLocation = 0;
    groundSubmesh.BaseVertexLocation = 0;

    groundGeo->DrawArgs["grid"] = groundSubmesh;

    mGeometries[groundGeo->Name] = std::move(groundGeo);
}

void Renderer::InitializeMaterials()
{
    auto plastic = std::make_unique<Material>();
    plastic->Name = "plastic";
    plastic->MatCBIndex = 0;
    plastic->DiffuseSrvHeapIndex = -1;
    plastic->DiffuseAlbedo = XMFLOAT4(0.0f, 0.2f, 0.6f, 1.0f);
    plastic->FresnelR0 = XMFLOAT3(0.04f, 0.04f, 0.04f);      
    plastic->Roughness = 0.2f;                                

    auto wood = std::make_unique<Material>();
    wood->Name = "wood";
    wood->MatCBIndex = 1;
    wood->DiffuseSrvHeapIndex = -1;
    wood->DiffuseAlbedo = XMFLOAT4(0.4f, 0.2f, 0.0f, 1.0f);
    wood->FresnelR0 = XMFLOAT3(0.04f, 0.04f, 0.04f);
    wood->Roughness = 0.8f;

    auto iron = std::make_unique<Material>();
    iron->Name = "iron";
    iron->MatCBIndex = 2;
    iron->DiffuseSrvHeapIndex = -1;
    iron->DiffuseAlbedo = XMFLOAT4(0.1f, 0.1f, 0.1f, 1.0f);    
    iron->FresnelR0 = XMFLOAT3(0.56f, 0.57f, 0.58f);           
    iron->Roughness = 0.4f;                                    

    auto copper = std::make_unique<Material>();
    copper->Name = "copper";
    copper->MatCBIndex = 3;
    copper->DiffuseSrvHeapIndex = -1;
    copper->DiffuseAlbedo = XMFLOAT4(0.05f, 0.05f, 0.05f, 1.0f);
    copper->FresnelR0 = XMFLOAT3(0.95f, 0.64f, 0.54f);         
    copper->Roughness = 0.2f;                                  

    auto gold = std::make_unique<Material>();
    gold->Name = "gold";
    gold->MatCBIndex = 4;
    gold->DiffuseSrvHeapIndex = -1;
    gold->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
    gold->FresnelR0 = XMFLOAT3(1.00f, 0.71f, 0.29f);          
    gold->Roughness = 0.1f;                                   

    mMaterials[plastic->Name] = std::move(plastic);
    mMaterials[wood->Name] = std::move(wood);
    mMaterials[iron->Name] = std::move(iron);
    mMaterials[copper->Name] = std::move(copper);
    mMaterials[gold->Name] = std::move(gold);
}

void Renderer::InitializeRenderItem()
{
    auto boxRitem = std::make_unique<RenderItem>();

    boxRitem->Name = "box";
    XMStoreFloat4x4(&boxRitem->World, XMMatrixIdentity());

    boxRitem->ObjectCBIndex = 0;
    boxRitem->Geo = mGeometries["boxGeo"].get();
    boxRitem->Mat = mMaterials["copper"].get();

    boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
    boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
    boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;

    boxRitem->NumFramesDirty = 3;

    boxRitem->Bounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
    boxRitem->Bounds.Extents = XMFLOAT3(0.5f, 0.5f, 0.5f);

    mRenderItemsByType[RenderItemType::Opaque].push_back(boxRitem.get());
    mAllRenderItems.push_back(std::move(boxRitem));

    auto groundRitem = std::make_unique<RenderItem>();

    groundRitem->Name = "ground";
    XMStoreFloat4x4(&groundRitem->World, XMMatrixIdentity());

    groundRitem->ObjectCBIndex = 1; // box가 0을 쓰고 있으니 1
    groundRitem->Geo = mGeometries["groundGeo"].get();
    groundRitem->Mat = mMaterials["wood"].get();

    groundRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    groundRitem->IndexCount = groundRitem->Geo->DrawArgs["grid"].IndexCount;
    groundRitem->StartIndexLocation = groundRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    groundRitem->BaseVertexLocation = groundRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

    groundRitem->NumFramesDirty = 3;

    groundRitem->Bounds.Center = XMFLOAT3(0.0f, -1.0f, 0.0f);
    groundRitem->Bounds.Extents = XMFLOAT3(10.0f, 0.01f, 10.0f);

    mRenderItemsByType[RenderItemType::Opaque].push_back(groundRitem.get());
    mAllRenderItems.push_back(std::move(groundRitem));
}

void Renderer::InitializeLights()
{
    auto mainDirectionalLight = std::make_unique<Light>();
    mainDirectionalLight->Direction = { 0.57735f, -0.57735f, 0.57735f };
    mainDirectionalLight->Strength = { 0.8f, 0.8f, 0.8f };
    mMainLight = mainDirectionalLight.get();

    auto pointLight1 = std::make_unique<Light>();
    pointLight1->Position = { 0.0f, 0.0f, -10.0f };
    pointLight1->Strength = { 0.8f, 0.8f, 0.8f };

    mAllLights["Directional"].push_back(std::move(mainDirectionalLight));
    mAllLights["Point"].push_back(std::move(pointLight1));
}

void Renderer::LoadTextures()
{
    mTextureManger = std::make_unique<TextureManager>();

    // mTextureManager->LoadTexture("wood", "Textures/WoodCrate01.dds", mGraphicsDevice->GetDevice(), mCommandQueue->GetCommandList());
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

    UpdateObjectConstants();
    UpdatePassConstants();
    UpdateMaterialBuffer();
    mMainCamera.Update(dt);
    InputManager::GetInstance()->ClearDeltas();
}

void Renderer::UpdateObjectConstants()
{
    auto currObjectCB = mCurrFrameResource->ObjectCB.get();

    for (auto& e : mAllRenderItems)
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
    int idx = 0;
    for (auto& e : mAllLights)
    {
        for (auto& light : e.second)
        {
            mPassCB.Lights[idx++] = *light.get();
        }
    }

    float sceneRadius = 10.0f;

    XMVECTOR lightDir = XMLoadFloat3(&mMainLight->Direction);
    XMVECTOR lightPos = -2.0f * sceneRadius * lightDir; // 씬의 반지름을 고려해 멀리 배치
    XMVECTOR targetPos = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, up);
    XMStoreFloat4x4(&mPassCB.LightView, lightView);
    XMMATRIX lightProj = XMMatrixOrthographicLH(20.0f, 20.0f, 1.0f, 40.0f);
    XMStoreFloat4x4(&mPassCB.LightProj, lightProj);
    XMMATRIX lightViewProj = lightView * lightProj;
    XMStoreFloat4x4(&mPassCB.LightViewProj, lightViewProj);

    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(0, mPassCB);
}

void Renderer::UpdateMaterialBuffer()
{
    auto currMaterialBuffer = mCurrFrameResource->MaterialBuffer.get();

    for (auto& e : mMaterials)
    {
        Material* mat = e.second.get();
        if (mat->NumFramesDirty)
        {
            MaterialData matData;
            matData.DiffuseAlbedo = mat->DiffuseAlbedo;
            matData.FresnelR0 = mat->FresnelR0;
            matData.Roughness = mat->Roughness;
            matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;
            
            XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);
            XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));

            currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

            mat->NumFramesDirty--;
        }
    }
}

void Renderer::Draw()
{
    auto cmdAllocator = mCommandQueue->GetCommandAllocator();
    auto commandList = mCommandQueue->GetCommandList();

    ThrowIfFailed(cmdAllocator->Reset());
    
    //shadow pass
    ThrowIfFailed(commandList->Reset(cmdAllocator, mPSOs["shader_opaque"].Get()));

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

    commandList->SetPipelineState(mPSOs["shadow_opaque"].Get());
    for(auto& e: mRenderItemsByType)
        DrawRenderItems(commandList, e.second); // (여기서는 빛 시점의 PassConstants 버퍼를 타게 설정해야 함)

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
    
    ID3D12DescriptorHeap* heaps[] = { mShadowSrvHeap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetGraphicsRootSignature(mRootSignature.Get());
    commandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

    auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
    commandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(3, mShadowSrvHeap->GetGPUDescriptorHandleForHeapStart());

    DrawRenderItems(commandList, mRenderItemsByType[RenderItemType::Opaque]);

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

    auto objectCB = mCurrFrameResource->ObjectCB->Resource();

    for (int i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjectCBIndex * objCBByteSize;

        cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
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

    for (auto& ri : mAllRenderItems)
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

    if (pickedItem != nullptr)
    {
        pickedItem->Mat = mMaterials["plastic"].get();
        pickedItem->NumFramesDirty = MaxFrameResource;
    }
}

int Renderer::Run()
{
    MSG msg = { 0 };

    mTimer.Reset();

    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        else
        {
            mTimer.Tick();
            Update(mTimer.DeltaTime());
            Draw();
        }
    }

    return (int)msg.wParam;
}

