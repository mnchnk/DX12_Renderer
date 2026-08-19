# DirectX 12 모델 로딩 / 씬 구조 학습 정리

Scene·GameObject·Transform 리팩터링부터 Assimp 모델 로딩까지의 정리.
앞선 `DirectX12_ShadowMap_Notes.md`, `DirectX12_RootSignature_Texture_Notes.md`의 후속.

---

## 1. 디스크립터 힙은 타입당 하나만 바인딩된다 (중요)

이번에 가장 헷갈렸던 부분. 앞 문서에서 "TextureManager가 SRV 힙을 스스로 소유하자"고 정리했는데, **그 설계로는 그림자맵과 텍스처를 동시에 쓸 수 없다.** 정정이 필요하다.

### 규칙

`ID3D12GraphicsCommandList::SetDescriptorHeaps`로 바인딩할 수 있는 힙은:

- `CBV_SRV_UAV` 타입 **1개**
- `SAMPLER` 타입 **1개**

이게 전부다. GPU 하드웨어가 각 타입의 힙 베이스 주소를 담는 레지스터를 하나씩만 갖고 있기 때문이다.

### 디스크립터 테이블은 "현재 힙의 오프셋"이다

`SetGraphicsRootDescriptorTable(rootParamIndex, gpuHandle)`에서 넘기는 `gpuHandle`은 **현재 바인딩된 힙 내부의 위치**를 가리킨다. 다른 힙의 핸들을 넘기면 유효하지 않다.

```
책상 위에 책장을 하나만 올릴 수 있다.
테이블은 "그 책장의 N번째 칸부터 봐라"라는 지시일 뿐이다.
```

### 잘못된 구조

```
mShadowSrvHeap  [그림자맵]                 ← 힙 A
mTextureHeap    [tex0][tex1][tex2]        ← 힙 B

A를 바인딩하면 텍스처를 못 읽고, B를 바인딩하면 그림자맵을 못 읽는다.
픽셀 셰이더가 둘 다 필요하므로 해결 불가.
```

### 맞는 구조

```
mSrvHeap  [tex0][tex1][tex2][그림자맵]      ← 힙 하나
             ↑                   ↑
    root param 4 (t2)      root param 3 (t1)
    offset 0               offset 3
```

힙은 하나만 바인딩하고, 두 개의 디스크립터 테이블이 같은 힙의 서로 다른 오프셋을 가리킨다.

```cpp
// 힙 생성 - 텍스처 개수 + 그림자맵 1개
srvHeapDesc.NumDescriptors = texCount + 1;

CD3DX12_CPU_DESCRIPTOR_HANDLE hCpu(mSrvHeap->GetCPUDescriptorHandleForHeapStart());
CD3DX12_GPU_DESCRIPTOR_HANDLE hGpu(mSrvHeap->GetGPUDescriptorHandleForHeapStart());

// [0 .. texCount-1] 텍스처
mTextureManger->InitializeDescriptor(device, hCpu, srvDescSize);

// [texCount] 그림자맵
mShadowMap->BuildDescriptor(device,
    CD3DX12_CPU_DESCRIPTOR_HANDLE(hCpu, texCount, srvDescSize),
    CD3DX12_GPU_DESCRIPTOR_HANDLE(hGpu, texCount, srvDescSize),
    mShadowDsvHeap->GetCPUDescriptorHandleForHeapStart());
```

```cpp
// Draw() - 힙은 하나만 바인딩
ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
commandList->SetDescriptorHeaps(1, heaps);

commandList->SetGraphicsRootDescriptorTable(3, mShadowMap->Srv());                              // 힙의 texCount번째
commandList->SetGraphicsRootDescriptorTable(4, mSrvHeap->GetGPUDescriptorHandleForHeapStart()); // 힙의 0번째
```

### 설계 원칙

**디스크립터 힙은 렌더러가 통합 소유하고, 각 서브시스템은 "어느 칸부터 채울지"에 대한 핸들만 받는다.**
`ShadowMap::BuildDescriptor(device, hCpuSrv, hGpuSrv, hCpuDsv)`가 원래 이 방식이었다. TextureManager도 여기에 맞춰야 한다.

참고로 DSV 힙은 이 문제에서 자유롭다. DSV는 셰이더가 읽는 게 아니라 출력 병합기(OM)가 쓰는 것이라 `SetDescriptorHeaps` 대상이 아니고, `OMSetRenderTargets`에 CPU 핸들을 직접 넘기기 때문이다.

---

## 2. CPU 구조체와 GPU 구조체를 분리한다

`Light`에 타입 정보를 넣으려다 발견한 문제.

### 왜 그냥 못 넣나

`PassConstants::Lights[]`는 상수 버퍼로 통째로 GPU에 복사되고, 셰이더의 `Light gLights[MaxLights]`가 그 메모리를 그대로 해석한다. 즉 C++ `Light`와 HLSL `Light`는 **바이트 단위로 같은 레이아웃**이어야 한다.

여기에 `LightType Type` 필드를 추가하면 구조체 크기가 48 → 52바이트로 바뀌면서 그 뒤 모든 필드의 오프셋이 밀린다. 셰이더는 여전히 48바이트 간격으로 읽으니 전부 깨진다.

### 해결: 두 개로 나눈다

이미 프로젝트에 있던 `Material`(CPU) / `MaterialData`(GPU) 패턴과 동일하다.

```cpp
// GPU 레이아웃 - HLSL과 1:1. CPU 전용 필드 추가 금지.
struct LightData
{
    XMFLOAT3 Strength;  float FalloffStart;
    XMFLOAT3 Direction; float FalloffEnd;
    XMFLOAT3 Position;  float SpotPower;
};

// CPU 쪽 - 타입 정보 등 엔진이 필요로 하는 것을 자유롭게 추가
struct Light
{
    LightType Type = LightType::Directional;
    /* ... 같은 데이터 ... */

    LightData ToLightData() const { /* 필요한 것만 복사 */ }
};
```

`PassConstants::Lights`의 타입을 `LightData[]`로 바꾸고, 업로드할 때 `ToLightData()`를 거친다.

### 덤으로 발견한 상수 불일치

C++ `#define MAXLIGHT 10` vs HLSL `#define MaxLights 16`.
셰이더가 선언한 상수 버퍼(16개)보다 CPU가 올리는 버퍼(10개)가 작았다. 실제로 쓰는 인덱스가 0, 1뿐이라 사고가 안 났을 뿐 잠재적 버그였다.

**교훈: CPU/HLSL 양쪽에 같은 값을 적는 상수는 한쪽만 고치기 쉽다. 주석으로 짝을 명시할 것.**

---

## 3. 배열 슬롯을 맵 순회 순서로 채우면 안 된다

```cpp
// 잘못된 코드
int idx = 0;
for (auto& e : mScene->GetAllLights())      // unordered_map!
    for (auto& light : e.second)
        mPassCB.Lights[idx++] = *light;
```

셰이더는 `gLights[0]`이 방향광, `gLights[1]`이 포인트광이라고 **고정된 규칙**으로 읽는다(`NUM_DIR_LIGHTS` 기준). 그런데 `unordered_map`의 순회 순서는 표준이 보장하지 않는다. "Point"가 먼저 나오면 방향광 자리에 포인트광이 들어간다.

```cpp
// 맞는 코드 - 타입으로 슬롯을 결정
int dirSlot = 0;
int pointSlot = NumDirLights;
int spotSlot = NumDirLights + NumPointLights;

switch (light->Type) { /* 각 타입을 정해진 슬롯 범위에 */ }
```

앞서 텍스처 `SrvHeapIndex`를 맵 순회 순서 대신 로드 시점에 고정한 것과 **완전히 같은 문제**다. 이 패턴이 반복해서 나온다는 걸 기억할 것.

### 관련: 타입 정보가 없으면 동기화도 못 한다

`SyncLights()`가 모든 라이트의 `Position`과 `Direction`을 무조건 Transform에서 덮어쓰고 있었다. 그 결과:

- 포인트광의 `Direction`이 (0,0,1)로 덮어써짐
- 방향광의 `Position`이 (0,0,0)으로 덮어써짐

타입 필드가 생긴 뒤에야 "위치는 Point/Spot만, 방향은 Directional/Spot만" 갱신하도록 고칠 수 있었다.

---

## 4. 상수 버퍼에 올리는 행렬은 전부 전치

가장 찾기 어려웠던 버그.

```cpp
// 카메라 행렬 - 전치함
XMStoreFloat4x4(&mPassCB.ViewProj, XMMatrixTranspose(viewProj));

// 라이트 행렬 - 전치 안 함 (버그!)
XMStoreFloat4x4(&mPassCB.LightViewProj, lightViewProj);
```

### 원리

- DirectXMath는 **row-major**로 행렬을 저장
- HLSL 상수 버퍼는 기본적으로 **column-major**로 해석
- 셰이더의 `mul(posW, gViewProj)`는 row-vector 규약

그래서 업로드 전에 전치해야 앞뒤가 맞는다. 하나만 빠지면 그 행렬만 조용히 틀린 결과를 낸다.

### 증상이 왜 헷갈렸나

`gLightViewProj`는 그림자 패스(`ShadowVS.hlsl`)와 메인 패스(`ShadowPosH` 계산) 양쪽에서 쓰인다. 둘 다 **똑같이 틀린** 행렬을 썼기 때문에 아무것도 안 나오는 게 아니라 "그림자 비슷한 이상한 줄기"가 나왔다. 그래서 셀프 셰도잉 acne로 오진하고 DepthBias를 만지느라 시간을 썼다.

### 원인 격리가 추측보다 빠르다

결국 `CalcShadowFactor`가 무조건 `1.0f`를 반환하게 만들어서 그림자 경로를 통째로 끊어보고 나서야 "그림자맵 문제"로 범위가 좁혀졌다.

**의심되는 경로를 통째로 끊어보는 것**이 코드를 계속 들여다보는 것보다 빠르다.

---

## 5. 커맨드 얼로케이터는 프레임마다 따로

```cpp
// 잘못된 코드 - 공유 얼로케이터를 매 프레임 Reset
auto cmdAllocator = mCommandQueue->GetCommandAllocator();
ThrowIfFailed(cmdAllocator->Reset());
```

### 규칙

`ID3D12CommandAllocator::Reset()`은 **그 얼로케이터로 기록한 모든 커맨드 리스트의 GPU 실행이 끝난 뒤에만** 호출할 수 있다.

프레임 리소스를 3개 돌려쓰면 최대 3프레임이 GPU에서 동시에 진행 중일 수 있다. 얼로케이터가 하나뿐이면 아직 실행 중인 이전 프레임의 커맨드 메모리를 덮어쓰게 된다.

### 해결

`FrameResource`마다 얼로케이터를 하나씩 둔다. `Update()`에서 이미 그 프레임 리소스의 펜스를 기다리므로 자동으로 안전해진다.

```cpp
// FrameResource 생성자
ThrowIfFailed(device->CreateCommandAllocator(
    D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(CmdAllocator.GetAddressOf())));

// Draw()
auto cmdAllocator = mCurrFrameResource->CmdAllocator.Get();
```

### GPU 크래시는 다음 API 호출에서 드러난다

이 버그의 증상은 `SwapChain::Present()`에서의 예외였다. GPU가 죽어도 CPU는 그 시점에 모르고, 다음 API 호출이 device removed를 반환하면서 그제서야 드러난다.

**`Present()`에서 터졌다고 `Present()`가 원인인 경우는 거의 없다.**

---

## 6. 엔진 구조: Scene / GameObject / Transform

### 왜 분리하나

기존에는 `Renderer`가 렌더링, 씬 데이터, 카메라, 입력, 윈도우 루프를 전부 들고 있었다. 이러면 기능을 추가할 때마다 계속 `Renderer`에 쌓인다.

```
Scene       : 무엇이 존재하는가 (GameObject, RenderItem, Light 소유)
GameObject  : 하나의 개체 (Transform + 렌더/라이트 데이터 참조)
Transform   : 어디에 있는가 (위치/회전/스케일 + 부모-자식 계층)
Renderer    : 어떻게 그리는가 (PSO, 커맨드 리스트, GPU 리소스)
```

### 회전은 쿼터니언으로

부모-자식 계층에서 오일러 각을 쓰면 회전 합성 순서 의존성과 짐벌락 문제가 바로 나온다. 처음부터 쿼터니언으로 가는 게 나중에 뜯어고치는 것보다 싸다.

### 행렬 곱 순서

```cpp
XMMATRIX Transform::GetWorldMatrix() const
{
    XMMATRIX local = GetLocalMatrix();
    if (mParent)
        return local * mParent->GetWorldMatrix();   // 자식 로컬 -> 부모 월드
    return local;
}
```

HLSL에서 `mul(float4(vin.PosL,1.0f), gWorld)`처럼 벡터를 왼쪽에 두는 row-vector 규약을 쓰므로, 계층 변환도 로컬을 먼저 곱한다.

### RenderItem은 파생 데이터가 된다

`Transform`이 위치의 유일한 원천이 되고, `RenderItem::World`는 매 프레임 거기서 계산해 채우는 값이 된다.

```cpp
void Renderer::SyncTransforms()
{
    for (auto& go : mScene->GetGameObjects())
        if (go->Render)
            XMStoreFloat4x4(&go->Render->World, go->GetTransform().GetWorldMatrix());
}
```

### 소유권 정리

- `unique_ptr` : 소유자가 명확한 곳 (`Scene::mAllRenderItems`, `Scene::mAllLights`)
- 生 포인터 : 참조만 하는 곳 (`GameObject::Render`, `GameObject::LightData`, `mRenderItemsByType`)

같은 객체를 두 `unique_ptr`이 소유하면 이중 해제가 난다. `mRenderItemsByType`은 소유가 아니라 "PSO 타입별 드로우 그룹핑 캐시"이므로 생 포인터가 맞다.

### 헤더 배치 원칙

`RenderItem`이 `Renderer.h` 안에 정의돼 있어서 `GameObject.h`가 참조할 수 없었다(`Renderer.h`는 무거운 헤더라 순환 위험).

**여러 시스템이 공유하는 데이터 구조체는, 그 시스템 중 하나의 헤더 안에 갇혀 있으면 안 된다.** 독립된 가벼운 헤더로 뺀다.

```
Renderer.h -> Scene.h -> GameObject.h -> RenderItem.h -> FrameResource.h / Util.h
(한 방향으로만 흐르고 되돌아오지 않는다)
```

---

## 7. 모델 로딩 (Assimp)

### 상용 엔진은 FBX를 런타임에 읽지 않는다

언리얼 등이 쓰는 구조는 2단계다.

**1단계 - 임포트 (에디터에서 한 번)**
FBX를 읽어 엔진 내부 표현으로 변환하고 **엔진 전용 바이너리**로 저장한다. 언리얼에서 FBX를 드래그하면 `.uasset`이 생기는 게 이 단계다. 정점 병합, 탄젠트 계산, 인덱스 최적화, 텍스처 압축을 여기서 다 끝낸다.

**2단계 - 런타임**
변환된 바이너리만 읽는다. GPU에 바로 올릴 수 있는 레이아웃이라 파싱이 거의 없다.

**왜 나누나**: FBX 파서(FBX SDK, Assimp)는 무겁다. 실행할 때마다 파싱하면 로딩이 느리고, 배포할 때 파서를 같이 넣어야 한다.

### 현재 단계에서의 선택

지금은 Assimp로 런타임 로딩부터 시작하고, 나중에 `AssetImporter.exe`로 분리해 "FBX → 자체 바이너리" 변환기를 만들면 된다. 그때 엔진에서 Assimp 의존성이 빠진다.

### Assimp 구조와 엔진 구조의 대응

| Assimp | 이 프로젝트 |
|---|---|
| `aiMesh` (정점/인덱스/노멀/UV/탄젠트) | `MeshGeometry` + `SubmeshGeometry` |
| `aiMaterial` (색상, 텍스처 경로) | `Material` + `TextureManager` |
| `aiNode` 트리 | `GameObject` + `Transform` 계층 |

### 중요한 임포트 플래그

```cpp
aiProcess_Triangulate |
aiProcess_GenSmoothNormals |
aiProcess_CalcTangentSpace |     // 노멀 매핑용 탄젠트 자동 계산
aiProcess_JoinIdenticalVertices |
aiProcess_ConvertToLeftHanded    // = MakeLeftHanded | FlipUVs | FlipWindingOrder
```

`aiProcess_ConvertToLeftHanded`가 특히 중요하다. D3D는 왼손 좌표계이고 텍스처 V축이 아래로 향한다. 이게 없으면 모델이 거울처럼 뒤집히고 와인딩이 반대가 되어 앞면 대신 뒷면이 컬링된다.

### 여러 메시를 하나의 버퍼로 합치기

모든 `aiMesh`를 하나의 정점/인덱스 버퍼에 이어 붙이고, 각각을 `DrawArgs`의 서브메시로 기록한다. 드로우콜마다 버퍼를 다시 바인딩할 필요가 없어진다.

```cpp
submesh.StartIndexLocation = (UINT)indices.size();
submesh.BaseVertexLocation = (INT)vertices.size();  // 이 메시 정점의 시작 위치
// 인덱스는 메시 기준(0부터)으로 저장 - GPU가 그릴 때 BaseVertexLocation을 더해준다
```

### 인덱스 포맷

캐릭터 모델은 정점이 65535개를 쉽게 넘으므로 `DXGI_FORMAT_R32_UINT`를 쓴다. `IndexFormat`은 `MeshGeometry`마다 저장되므로, 손으로 만든 박스/바닥이 `R16_UINT`인 채로 공존해도 문제없다.

### 로더의 책임 범위

`ModelLoader`는 GPU 텍스처 리소스를 만들지 않고, "어떤 텍스처 파일이 필요한지"만 `LoadedMaterial`에 담아 반환한다. 텍스처 로딩은 `TextureManager`의 책임이므로 로더가 그 역할까지 갖지 않게 분리했다.

### 텍스처 경로 주의

모델 파일에는 제작자 PC의 절대 경로가 박혀 있는 경우가 많다. 파일명만 뽑아 써야 한다.

FBX는 보통 `aiTextureType_NORMALS`에, OBJ는 `aiTextureType_HEIGHT`에 노멀맵을 넣는다. 둘 다 확인해야 한다.

### 스케일

Mixamo 등에서 받은 캐릭터는 보통 센티미터 단위라 키가 150~180이다. 씬 스케일이 미터 단위라면 `Transform::SetScale(0.01f)` 정도가 필요하다. 화면에 아무것도 안 보이면 스케일부터 의심할 것.

---

## 8. 앞으로: 텍스처 연결 체크리스트

파이프라인은 다 깔려 있고 마지막 배선만 남았다.

### 0) 텍스처를 DDS로 변환

현재 로더가 `CreateDDSTextureFromFile12`만 쓰므로 DDS가 필요하다. [texconv](https://github.com/microsoft/DirectXTex/releases) 사용:

```
texconv -f BC7_UNORM_SRGB -m 0 -y -o Models Models\character_diffuse.png
texconv -f BC5_UNORM      -m 0 -y -o Models Models\character_normal.png
```

**diffuse는 SRGB, 노멀맵은 선형(BC5_UNORM).** 노멀맵은 색이 아니라 방향 벡터라 감마 보정이 들어가면 조명이 틀어진다.

### 1) 초기화 순서

모델을 먼저 로드해야 어떤 텍스처가 필요한지 알 수 있다.

```
InitializeShapesGeometry()   // 모델 로드 -> 텍스처 파일명 확보
LoadTextures()               // 그 파일들을 로드
InitializeRootSignature()
InitializeDescriptorHeaps()  // 텍스처 개수를 알아야 힙 크기가 정해짐
...
InitializeMaterials()        // SrvHeapIndex를 채움
InitializeRenderItem()
InitializeFrameResource()
```

### 2) 힙 통합

1번 항목대로 `mSrvHeap` 하나에 [텍스처들 + 그림자맵]을 담는다.

### 3) 머티리얼에 인덱스 채우기

```cpp
Texture* diffuse = mTextureManger->GetTexture(src.DiffuseTextureFile);
mat->DiffuseSrvHeapIndex = diffuse ? diffuse->SrvHeapIndex : -1;
```

### 4) Draw()에서 바인딩

```cpp
ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };   // 하나만
commandList->SetDescriptorHeaps(1, heaps);
commandList->SetGraphicsRootDescriptorTable(3, mShadowMap->Srv());
commandList->SetGraphicsRootDescriptorTable(4, mSrvHeap->GetGPUDescriptorHandleForHeapStart());
```

### 5) 셰이더 - 텍스처 없는 머티리얼 방어

```hlsl
float4 diffuseAlbedo = matData.DiffuseAlbedo;
if (matData.DiffuseMapIndex != 0xFFFFFFFF)     // -1이 uint로 넘어온 값
    diffuseAlbedo *= gTextures[NonUniformResourceIndex(matData.DiffuseMapIndex)]
                        .Sample(gsamAnisotropicWrap, pin.TexC);
```

**이 가드가 없으면 `SrvHeapIndex = -1`인 머티리얼(바닥 등)에서 42억번째 텍스처를 읽으려다 GPU가 죽는다.** 이미 한 번 겪은 문제다.

### 6) 링커 에러 예방

`TextureManager::GetTexture()`가 `.cpp`에서 `inline`으로 정의되어 있다. `inline` 함수를 `.cpp`에 정의하면 그 번역 단위 안에서만 쓸 수 있어서, 다른 파일에서 호출하는 순간 링커 에러가 난다. `inline`을 제거할 것.

---

## 9. 빌드 환경에서 겪은 문제

### 소스 파일 인코딩 (CP949 vs UTF-8)

```
warning C4819: 현재 코드 페이지(949)에서 표시할 수 없는 문자가 파일에 들어 있습니다
error C2178: 'LoadedSubmesh::ai_epsilon'은(는) 'constexpr' 지정자로 선언할 수 없습니다
```

MSVC는 BOM이 없으면 소스를 시스템 코드 페이지(한국어 Windows = CP949)로 읽는다. CP949는 2바이트 문자셋이라 0x81~0xFE 범위 바이트를 만나면 다음 바이트까지 한 글자로 묶는다.

UTF-8로 저장된 한글 주석이 CP949로 해석되면 **선행 바이트가 줄 끝의 개행 문자를 삼켜서 주석이 다음 줄까지 이어진다.** 그 결과 `};`가 주석에 먹히고, 그 뒤에 include된 헤더 전체가 앞선 구조체의 멤버로 파싱된다.

**해결책 (하나 선택)**
1. 프로젝트 속성 → C/C++ → 명령줄 → 추가 옵션에 `/utf-8` (기존 CP949 파일들도 UTF-8로 변환 필요)
2. 파일을 "UTF-8(BOM 포함)"으로 저장 (VS: 다른 이름으로 저장 → 저장 버튼 옆 화살표 → 인코딩하여 저장)
3. 주석을 ASCII로만 작성

**진단 요령**: 에러가 "내 타입의 멤버가 아닌 것이 내 타입의 멤버로 보인다"는 형태면 인코딩이나 괄호 누락으로 선언이 안 닫힌 것이다. 에러 목록 맨 위의 C4819를 먼저 볼 것.

### C++ 표준 버전

`std::clamp`, `<filesystem>`은 C++17부터다. VS2022 기본값은 C++14이므로 프로젝트 속성 → C/C++ → 언어 → C++ 언어 표준을 `/std:c++17`로 올려야 한다. 구성 드롭다운을 "모든 구성"으로 놓고 바꿀 것.

### 헤더 구조체 크기가 바뀌면 전체 재빌드

`MAXLIGHT`를 10에서 16으로 바꿔 `sizeof(PassConstants)`가 288바이트 커졌을 때, 일부 .obj만 재컴파일되어 `Renderer`의 멤버 오프셋이 파일마다 달라졌다. `mShadowMap`이 `mPassCB` 뒤에 선언되어 있어서, 생성자가 초기화하지 않은 위치를 읽고 쓰레기 포인터로 `Release()`를 호출해 크래시가 났다.

**헤더에서 구조체 크기를 바꿨으면 반드시 "솔루션 다시 빌드".**

### vcpkg로 라이브러리 설치

```bash
git clone https://github.com/microsoft/vcpkg.git D:\vcpkg
cd /d/vcpkg
./bootstrap-vcpkg.bat
./vcpkg integrate install          # MSBuild 자동 연동 (프로젝트 설정 불필요)
./vcpkg install assimp:x64-windows # 트리플렛 명시 (빌드 구성이 x64여야 함)
```

`integrate install` 후에는 추가 포함 디렉터리/라이브러리 디렉터리/링커 입력을 손댈 필요가 없고, DLL도 출력 폴더로 자동 복사된다. 설치 후 Visual Studio를 재시작해야 반영된다.

Git Bash(MINGW64)에서는 `.\` 대신 `./`를 쓴다. `git clone` 뒤에 목적지 경로를 안 주면 현재 디렉터리에 저장소 이름으로 받아진다.

---

## 10. 반복해서 나온 버그 패턴

이번 세션까지 누적된 것들. 새 코드를 짤 때 체크리스트로 쓸 것.

1. **구현했는데 호출하지 않음** — `InitializeLights`, `InitializeDescriptorHeaps`, `BuildDescriptor`. `grep`으로 정의부와 호출부를 둘 다 확인하는 습관.
2. **개수 인자 불일치** — 루트 시그니처 파라미터 개수, `InitAsDescriptorTable`의 range 개수, CPU/HLSL 배열 크기. 배열을 늘렸으면 그 배열을 쓰는 쪽의 숫자도 같이 찾는다.
3. **맵 순회 순서에 의존** — 텍스처 SRV 인덱스, 라이트 배열 슬롯. `unordered_map`은 순서를 보장하지 않는다.
4. **CPU/GPU 구조체 불일치** — 크기, 정렬, 공유 상수. 항상 짝으로 수정.
5. **행렬 전치 누락** — 상수 버퍼에 올리는 모든 행렬.
6. **인덱스 -1이 uint로 넘어감** — 초기화되지 않은 인덱스로 리소스 배열을 인덱싱하면 GPU가 죽는다.
7. **GPU 크래시는 다음 API 호출에서 드러난다** — `Present()`가 범인인 경우는 드물다.
8. **원인이 안 잡히면 격리 테스트** — 의심 경로를 통째로 끊어보는 게 코드를 노려보는 것보다 빠르다.
9. **출력 창을 먼저 볼 것** — 셰이더 컴파일 에러, 디버그 레이어 메시지는 이미 찍히고 있다.
10. **빌드 상태를 의심할 것** — 헤더 크기 변경 후 증분 빌드, 인코딩 문제 등 코드가 아니라 빌드가 원인인 경우가 있다.
