# DirectX 12 루트 시그니처 / 텍스처 바인딩 학습 정리

셰도우맵 다음으로 텍스처(특히 노멀맵까지 포함한 bindless 텍스처 배열) 바인딩을 붙이면서 정리한 내용. 앞의 `DirectX12_ShadowMap_Notes.md`에서 SRV/디스크립터 힙/루트 시그니처 기초는 다뤘으니, 여기서는 **HLSL 레지스터 타입(b/t/u/s)**, **루트 파라미터 종류 전체 정리**, **텍스처를 여러 개 다룰 때의 설계(bindless)**, 그리고 이 과정에서 겪은 버그들 위주로 정리한다.

---

## 1. HLSL 레지스터 타입: b, t, u, s가 뭔가

HLSL에서 리소스를 선언할 때 `: register(X#)`로 어떤 "슬롯"에 바인딩되는지 지정한다. 앞글자가 리소스의 **종류(뷰 타입)**를 나타낸다.

| 접두사 | 뜻 | 대응하는 뷰 | 이 프로젝트에서의 예 |
|---|---|---|---|
| `b` | **B**uffer(상수 버퍼) | CBV (Constant Buffer View) | `cbuffer cbPerObject : register(b0)`, `cbuffer cbPass : register(b1)` |
| `t` | **T**exture / 읽기 전용 리소스 | SRV (Shader Resource View) | `StructuredBuffer<MaterialData> gMaterialData : register(t0)`, `Texture2D gShadowMap : register(t1)`, `Texture2D gDiffuseMaps[] : register(t2)` |
| `u` | **U**nordered Access | UAV (Unordered Access View, 읽기+쓰기) | 아직 이 프로젝트엔 없음 (컴퓨트 셰이더나 UAV 카운터 쓸 때 필요) |
| `s` | **S**ampler | Sampler / SamplerComparisonState | `SamplerState gsamPointWrap : register(s0)` ~ `SamplerComparisonState gsamShadow : register(s6)` |

각 접두사별로 번호는 **독립적으로** 매겨진다 — `b0`과 `t0`은 서로 다른 슬롯이라 겹치지 않는다(같은 "0"이어도 종류가 다르면 별개). 그래서 이 프로젝트에서 `cbPerObject`가 `b0`, `gMaterialData`가 `t0`인 것처럼 둘 다 0번을 써도 충돌이 안 난다.

`register(t0, space0)`처럼 뒤에 **space**를 붙일 수도 있는데, 이건 같은 타입/번호를 쓰는 리소스를 서로 다른 "네임스페이스"로 분리하고 싶을 때 쓴다(주로 bindless 배열끼리 겹칠 때). 이 프로젝트는 전부 `space0`(생략 시 기본값)만 써서 아직 안 건드렸다.

---

## 2. 루트 시그니처 파라미터의 3가지 종류

루트 시그니처는 "이 셰이더가 어떤 리소스들을 어떤 슬롯으로 받을 것인가"의 설계도다. 각 루트 파라미터는 세 가지 방식 중 하나로 만들어진다.

### 2.1 루트 상수 (Root Constants) — 이 프로젝트는 안 씀

`InitAsConstants(num32BitValues, shaderRegister)`. 32비트 값 몇 개를 힙도 안 거치고 루트 인자에 직접 박아 넣는 방식. 제일 빠르지만 용량이 아주 작아서(보통 몇 개~십몇 개 정도) 큰 데이터엔 못 씀.

### 2.2 루트 디스크립터 (Root Descriptor)

`InitAsConstantBufferView`, `InitAsShaderResourceView`, `InitAsUnorderedAccessView`. GPU 가상주소를 루트 인자로 직접 넘긴다. **디스크립터 힙이 필요 없다.** 단, **Buffer 계열(상수 버퍼, 구조화 버퍼, raw 버퍼)에만 쓸 수 있고 Texture2D는 안 됨.**

```cpp
slotRootParameter[0].InitAsConstantBufferView(0); // b0, cbPerObject
slotRootParameter[1].InitAsConstantBufferView(1); // b1, cbPass
slotRootParameter[2].InitAsShaderResourceView(0, 0); // t0, gMaterialData (StructuredBuffer라 가능)
```

### 2.3 디스크립터 테이블 (Descriptor Table)

`InitAsDescriptorTable(numDescriptorRanges, pDescriptorRanges, visibility)`. 디스크립터 힙 안의 (연속된) 범위를 가리킨다. **Texture2D는 반드시 이 방식이어야 한다.**

```cpp
CD3DX12_DESCRIPTOR_RANGE shadowTable;
shadowTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0); // t1, 디스크립터 1개

CD3DX12_DESCRIPTOR_RANGE texTable;
texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, -1 /* unbounded */, 2, 0); // t2부터, 개수 미정

slotRootParameter[3].InitAsDescriptorTable(1, &shadowTable);                      // range 1개
slotRootParameter[4].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL); // range 1개
```

**`InitAsDescriptorTable`의 첫 번째 인자는 "디스크립터 개수"가 아니라 "range 구조체가 몇 개 배열로 있는가"다.** `texTable` 하나짜리 변수를 넘기면서 `2`를 넣는 실수를 했었는데(→ 뒤 메모리를 두 번째 range로 잘못 읽어서 루트 시그니처 생성이 깨짐), range가 하나면 무조건 `1`이어야 한다. "이 range 안에 디스크립터가 몇 개 있는가"는 `CD3DX12_DESCRIPTOR_RANGE::Init()`의 **두 번째 인자**(`NumDescriptors`)에서 정하는 거라 서로 다른 자리다.

### 2.4 정적 샘플러 (Static Sampler) — 위 3개와는 다른 별도 카테고리

`GetStaticSamplers()`로 만든 `CD3DX12_STATIC_SAMPLER_DESC` 배열은 루트 파라미터 배열(`slotRootParameter`)에 안 들어가고, `CD3DX12_ROOT_SIGNATURE_DESC`의 별도 인자로 들어간다.

```cpp
CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(5, slotRootParameter,
    (UINT)staticSamplers.size(), staticSamplers.data(),
    D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
```

정적 샘플러는 런타임에 값을 바꿀 수 없는 대신(필터/주소모드/비교함수 등이 루트 시그니처에 고정으로 박힘), 디스크립터 힙도 필요 없고 바인딩 비용도 없다. 이 프로젝트는 `s0`~`s6`(point/linear/anisotropic × wrap/clamp + 그림자 비교 샘플러) 총 7개를 전부 정적 샘플러로 처리한다.

### 이 프로젝트의 최종 루트 시그니처 구성

| 슬롯 | 종류 | 레지스터 | 용도 |
|---|---|---|---|
| [0] | 루트 디스크립터(CBV) | b0 | `cbPerObject` (오브젝트별 World, 머티리얼 인덱스) |
| [1] | 루트 디스크립터(CBV) | b1 | `cbPass` (뷰/프로젝션, 라이트, 그림자 변환 등) |
| [2] | 루트 디스크립터(SRV) | t0 | `gMaterialData` (머티리얼 StructuredBuffer) |
| [3] | 디스크립터 테이블(SRV) | t1 | `gShadowMap` (그림자맵 텍스처, 1개) |
| [4] | 디스크립터 테이블(SRV) | t2 | `gDiffuseMaps[]` (텍스처 배열, unbounded) |
| static | 정적 샘플러 | s0~s6 | 필터링용 6개 + 그림자 비교용 1개 |

---

## 3. 텍스처를 여러 개 다룰 때: Bindless 배열 패턴

### 3.1 왜 텍스처는 StructuredBuffer에 못 들어가나

`StructuredBuffer<T>`는 선형 메모리에 늘어선 POD 데이터 배열이다. `Texture2D`는 밉맵 체인, 압축 포맷(BC1~7), GPU마다 다른 타일링된 스와즐 레이아웃을 가진 "불투명 리소스"라 구조체 필드 하나로 표현이 안 된다. 텍스처를 참조하려면 항상 디스크립터(SRV)가 필요하고, 그 디스크립터는 힙/테이블을 거쳐야 셰이더에 넘어간다.

### 3.2 실제 패턴: 인덱스는 StructuredBuffer, 텍스처 실체는 배열 테이블

```hlsl
Texture2D gDiffuseMaps[] : register(t2); // 개수 제한 없는(unbounded) 배열

MaterialData matData = gMaterialData[gMaterialIndex]; // 여기까진 루트 디스크립터
float4 diffuse = gDiffuseMaps[NonUniformResourceIndex(matData.DiffuseMapIndex)]
                    .Sample(gsamLinearWrap, pin.TexC);
```

머티리얼 메타데이터(색상, 거칠기, **텍스처 인덱스**)는 `StructuredBuffer`로 통째로 넘기고, 실제 텍스처 데이터들은 별도의 배열/테이블에 몰아넣은 다음 그 인덱스로 골라 쓴다. 이러면 오브젝트마다 텍스처가 달라져도 드로우콜 사이에 루트 시그니처를 다시 바인딩할 필요가 없다 — 텍스처 배열은 프레임 시작할 때 한 번만 바인딩해두고, 어떤 텍스처를 쓸지는 오브젝트별 `MaterialData` 안의 정수 인덱스로만 결정된다.

### 3.3 `[]` 개수를 하드코딩하지 않는 법: Unbounded Descriptor Range

`kMaxTextures` 같은 상수를 C++/HLSL 양쪽에 똑같이 박아넣는 대신, **개수 제한 없는 range**를 쓰면 그 커플링 자체가 없어진다.

```cpp
texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, -1 /* UINT_MAX = unbounded */, 2, 0);
```
```hlsl
Texture2D gDiffuseMaps[] : register(t2); // 대괄호 비워두면 unbounded
```

실제 텍스처 개수는 오직 "런타임에 힙에 SRV를 몇 개 만들었는가"로만 결정된다. 단, 이건 **Resource Binding Tier 3** 하드웨어가 필요하다(2016년 이후 대부분의 GPU는 지원):

```cpp
D3D12_FEATURE_DATA_D3D12_OPTIONS opts = {};
device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &opts, sizeof(opts));
// opts.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3
```

### 3.4 `NonUniformResourceIndex`란

GPU는 픽셀을 웨이브(보통 32/64개) 단위로 묶어서 SIMD로 동시 실행한다. 리소스 배열을 런타임 인덱스로 접근할 때, 그 인덱스가 웨이브 안의 모든 픽셀에서 같다는 게 보장되면(uniform) 컴파일러가 디스크립터를 한 번만 읽어와 재사용하는 최적화를 하는데, 픽셀마다 인덱스가 다를 수 있으면(non-uniform) 이 최적화가 잘못된 결과를 낼 수 있다. `NonUniformResourceIndex(x)`는 값 자체는 그대로 두고(`x`를 리턴), 컴파일러한테 "이 인덱스는 다를 수 있으니 그 최적화 가정을 깨라"고 알려주는 표시자다. 지금은 오브젝트 하나당 드로우콜 하나라 사실상 uniform이지만, 인스턴싱이나 멀티 머티리얼 배치를 넣는 순간 non-uniform이 되므로 습관적으로 씌워두는 게 안전하다.

---

## 4. `TextureManager` 설계에서 겪은 문제들

### 4.1 `unordered_map`으로 저장해도 되나, 배열로 바꿔야 하나

**바꿀 필요 없다.** map은 이름으로 텍스처를 찾는 용도(머티리얼 설정할 때 "이 이름의 인덱스가 뭐야?" 조회)로 그대로 쓰고, 대신 **각 텍스처가 로드될 때 스스로 고정 인덱스를 부여받게** 하면 된다.

```cpp
struct Texture
{
    ...
    int SrvHeapIndex = -1;
};

void TextureManager::LoadTexture(...)
{
    ...
    tex->SrvHeapIndex = mTextureCount++; // 로드 순서대로 고정 인덱스
    mTextures[name] = std::move(tex);
}
```

`unordered_map`은 **순회 순서가 삽입 순서와 같다는 보장이 없다.** 그래서 나중에 SRV를 힙에 채울 때 map을 그냥 순서대로 돌면서 `hDescriptor.Offset(1, srvDescSize)`처럼 순차적으로 채우면, 어떤 텍스처의 SRV가 실제로는 그 텍스처의 `SrvHeapIndex`랑 다른 위치에 만들어질 수 있다. 반드시 **저장해둔 `SrvHeapIndex`로 절대 위치를 계산**해서 그 자리에 써야 한다:

```cpp
CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(hCpuStart, tex->SrvHeapIndex, srvDescSize); // 절대 위치
device->CreateShaderResourceView(tex->Resource.Get(), &srvDesc, hDescriptor);
```

### 4.2 `srvDescSize`는 어디서 오나

디스크립터 하나의 실제 크기는 GPU/드라이버마다 다르다(하드코딩 금지). 디바이스한테 물어봐야 한다:

```cpp
UINT srvDescSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
```

CBV/SRV/UAV는 같은 크기를 공유하지만, DSV/RTV는 각각 다른 크기라 필요하면 따로 쿼리해야 한다.

### 4.3 SRV desc를 채울 때 포맷을 하드코딩하지 않는 법

```cpp
D3D12_RESOURCE_DESC texDesc = tex->Resource->GetDesc(); // DDS 로더가 이미 알아낸 실제 포맷/밉레벨
D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
srvDesc.Format = texDesc.Format;             // diffuse=BC7_SRGB, normal=BC5_UNORM 등 텍스처마다 다름
srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
```

diffuse(albedo)는 보통 sRGB, normal map은 선형(linear) 데이터라 감마 보정하면 안 된다. DDS 파일 헤더에 이미 이 정보가 들어있어서 `CreateDDSTextureFromFile12`가 로드할 때 알아서 맞는 포맷으로 리소스를 만들어주므로, `GetDesc().Format`을 그대로 읽어 쓰면 텍스처별로 다른 처리를 따로 안 해도 된다.

### 4.4 힙 소유권을 누가 갖는가 — 설계가 꼬였던 지점

처음엔 두 가지 설계가 섞여 있었다:

- `InitializeDescriptor(device, hCpuStart, srvDescSize)`: **"힙 시작 핸들을 바깥에서 만들어서 넘겨줘"**라는 설계 (그림자맵의 `BuildDescriptor`랑 같은 패턴 — Renderer가 힙을 만들고 핸들만 넘김)
- `mTextureHeap` / `GetTextureHeap()`: **"힙을 TextureManager가 스스로 갖고 있다"**는 설계

이 둘을 같이 쓰면 `hCpuStart`를 얻으려면 힙이 있어야 하는데, 그 힙을 실제로 만드는 코드가 어디에도 없는 상태가 된다(순환이라기보다는 "만드는 사람이 빠진" 상태). 게다가 `GetTextureHeap()`이 `ComPtr`를 **값으로 반환**하고 있어서, Renderer 쪽에서 `IID_PPV_ARGS(&mTextureManger->GetTextureHeap())`처럼 그 반환값의 주소를 넘겨 채우려 해도 임시 객체에 쓰는 꼴이라 실제 멤버에는 아무것도 안 남는다.

**결론: 힙을 누가 소유/생성하는지 하나로 정해야 한다.** 텍스처는 개수가 가변적이고 그 개수를 아는 쪽이 `TextureManager` 자신이므로, `TextureManager`가 스스로 힙을 만들고 소유하는 쪽으로 통일했다:

```cpp
void TextureManager::BuildDescriptorHeap(ID3D12Device* device)
{
    if (mTextures.empty()) return; // 텍스처 없으면 힙도 안 만듦

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = mTextureCount;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mTextureHeap))); // 내부 멤버에 직접 씀

    UINT srvDescSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE hCpuStart = mTextureHeap->GetCPUDescriptorHandleForHeapStart(); // 스스로 구함

    for (auto& kv : mTextures)
    {
        Texture* tex = kv.second.get();
        // ... srvDesc 채우기 ...
        CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(hCpuStart, tex->SrvHeapIndex, srvDescSize);
        device->CreateShaderResourceView(tex->Resource.Get(), &srvDesc, hDescriptor);
    }
}
```

Renderer는 `mTextureManger->BuildDescriptorHeap(device)` 한 줄만 호출하고, 나중에 바인딩할 때 `GetTextureHeap()`으로 완성된 힙을 받아서 `SetDescriptorHeaps`/`SetGraphicsRootDescriptorTable`에 쓰면 된다. (그림자맵은 반대로 Renderer가 힙 소유자라 `BuildDescriptor`에 핸들을 넘기는 방식인데, 둘 다 맞는 패턴이고 "누가 힙을 소유하는가"만 프로젝트 안에서 일관되게 정하면 된다.)

---

## 5. 이번에 겪은 컴파일/런타임 에러들

### 에러 1. `pair<const string, unique_ptr<Texture>>::pair(const pair&)` 삭제된 함수 참조

```cpp
std::unordered_map<std::string, std::unique_ptr<Texture>> GetAllTextures() const { return mTextures; }
```

`unordered_map`을 **값으로 반환**하면 내부의 모든 `pair<const K, unique_ptr<Texture>>`를 복사해야 하는데, `unique_ptr`은 복사 생성자가 삭제된 move-only 타입이라 `pair`도, `unordered_map`도 복사 생성자가 자동으로 삭제된다. **참조로 반환**하면 해결된다:

```cpp
const std::unordered_map<std::string, std::unique_ptr<Texture>>& GetAllTextures() const { return mTextures; }
```

### 에러 2. `InitAsDescriptorTable`의 range 개수 착각

```cpp
slotRootParameter[4].InitAsDescriptorTable(2, &texTable, ...); // 잘못됨, texTable은 1개짜리 변수
```

첫 인자는 "range가 몇 개 배열로 있는가"인데 `texTable`은 단일 변수라 `1`이어야 한다. `2`를 넣으면 `texTable` 바로 뒤 스택 메모리를 두 번째 range로 잘못 읽어서 루트 시그니처 생성이 깨진다.

### 에러 3. `NumDescriptors cannot be 0`

```cpp
texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, mTextureManger->GetTextureCount(), 2, 0);
```

텍스처를 아직 하나도 로드 안 한 상태(`LoadTexture(...)` 호출이 주석 처리돼 있었음)라 `GetTextureCount()`가 0을 반환 → range의 `NumDescriptors`가 0 → D3D12가 명시적으로 금지하는 값이라 바로 실패. Unbounded range(`-1`)로 바꾸면 이 개수를 루트 시그니처 생성 시점에 몰라도 되므로 이 검증 자체가 안 걸린다.

### 에러 4. 힙을 값으로 반환하는 getter의 주소를 취함

```cpp
ThrowIfFailed(device->CreateDescriptorHeap(&texHeapDesc, IID_PPV_ARGS(&mTextureManger->GetTextureHeap())));
```

`GetTextureHeap()`이 값 반환이라 임시 객체의 주소를 넘기는 꼴 → 실제 멤버는 안 채워짐. 힙 생성 자체를 소유자(TextureManager) 내부로 옮겨서 해결.

---

## 6. 텍스처 바인딩 체크리스트

1. `Texture` 구조체에 `SrvHeapIndex` 같은 **로드 시점에 고정되는 인덱스** 필드를 둔다 (map 순회 순서에 의존하지 않기 위해)
2. 힙 소유자를 하나로 정한다 (Renderer가 만들어서 넘겨줄지, 리소스 매니저가 스스로 소유할지) — 두 패턴을 섞지 않는다
3. `GetDescriptorHandleIncrementSize`로 디스크립터 크기를 반드시 쿼리해서 쓴다 (하드코딩 금지)
4. SRV desc의 `Format`/`MipLevels`는 `resource->GetDesc()`에서 읽어와 텍스처마다 다른 실제 값을 반영한다
5. 여러 개의 텍스처를 다룰 땐 `[N]` 하드코딩 대신 unbounded range + `Texture2D arr[] : register(tN)`를 쓰고, 하드웨어의 Resource Binding Tier 3 지원 여부를 확인한다
6. 리소스 배열을 런타임 인덱스로 접근하는 곳엔 `NonUniformResourceIndex`를 씌운다
7. 루트 시그니처 파라미터 배열 크기를 늘렸으면 `CD3DX12_ROOT_SIGNATURE_DESC` 생성자의 "개수" 인자도 반드시 같이 바꾼다
8. `InitAsDescriptorTable`의 첫 인자(range 개수)와 `CD3DX12_DESCRIPTOR_RANGE::Init`의 두 번째 인자(디스크립터 개수)를 혼동하지 않는다
9. 텍스처를 하나도 안 불러온 상태에서 루트 시그니처/힙을 만들 수도 있다는 걸 감안해서(개발 중간 단계), `NumDescriptors == 0`이 되는 경로를 미리 방어한다 (unbounded range로 우회하거나, 0개면 해당 루트 파라미터 바인딩을 건너뛴다)
10. `unordered_map`을 값으로 반환하거나 복사하는 코드가 없는지 확인한다 (`unique_ptr` 멤버가 있는 타입은 복사 불가) — 항상 참조로 반환
