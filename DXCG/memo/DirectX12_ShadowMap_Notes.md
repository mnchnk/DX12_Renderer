# DirectX 12 Shadow Map 학습 정리

DXCG 프로젝트에서 그림자맵을 붙이면서 겪은 문제들과 개념을 순서대로 정리한 노트. 프랭크 루나 책의 셰도우 매핑 챕터를 실제로 구현하면서 막혔던 지점 위주로 씀.

---

## 1. 셰도우 매핑은 왜 2-Pass인가

그림자는 "이 픽셀이 광원한테 보이는가?"를 판단하는 문제다. GPU 래스터라이저는 기본적으로 카메라 시점에서만 그림을 그리기 때문에, 광원 시점에서 뭐가 보이는지를 알려면 **광원을 카메라라고 생각하고 한 번 더 렌더링**해야 한다. 그래서 그림자맵은 항상 두 번 그린다.

- **Pass 1 (Shadow Pass)**: 광원 위치에서 씬을 렌더링. 색은 필요 없고 깊이(depth)만 필요하다. 그 결과가 "광원 기준으로 각 방향에서 가장 가까운 표면까지의 거리"를 담은 텍스처, 즉 **Shadow Map**이다.
- **Pass 2 (Main Pass)**: 평소처럼 카메라 시점에서 렌더링하되, 각 픽셀마다 "이 픽셀을 광원 시점 좌표로 옮기면 어디에 찍히는가"를 계산해서 Shadow Map에 저장된 깊이값과 비교한다. 내 깊이가 Shadow Map에 저장된 깊이보다 크면 (= 나보다 광원에 더 가까운 뭔가가 있었다는 뜻) 그림자 처리를 한다.

이 프로젝트에서는 `mRenderItemsByType`에 들어있는 모든 오브젝트가 두 Pass에 전부 들어간다. 즉 **모든 오브젝트가 동시에 캐스터(그림자를 만드는 쪽)이자 리시버(그림자를 받는 쪽)**다. 박스만 캐스터, 바닥만 리시버 이런 식으로 역할이 나뉘는 게 아니다.

---

## 2. 핵심 개념

### 2.1 SRV (Shader Resource View)는 힙이 아니라 "뷰"

SRV 자체는 데이터를 담고 있는 게 아니라, 이미 존재하는 `ID3D12Resource`(텍스처나 버퍼)를 "셰이더에서 이런 포맷/모양으로 읽어라"라고 알려주는 작은 디스크립터(메타데이터)다.

- 리소스: 실제 GPU 메모리에 있는 데이터 덩어리 (`ID3D12Resource`)
- 뷰(SRV/CBV/UAV/RTV/DSV): 그 리소스를 어떤 용도/포맷/범위로 해석할지 정의하는 설명서

같은 리소스에 대해 여러 개의 뷰를 만들 수도 있다 (예: 같은 텍스처를 다른 포맷으로 해석하는 SRV 두 개).

### 2.2 디스크립터 힙

디스크립터 힙(`ID3D12DescriptorHeap`)은 이 "뷰"들을 GPU가 인덱싱할 수 있게 연속된 배열로 모아두는 그릇이다. 타입이 있다:

- `D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV`: CBV/SRV/UAV를 섞어서 담을 수 있는 힙. **셰이더에서 접근하니 `D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE`로 만들어야 한다.**
- `D3D12_DESCRIPTOR_HEAP_TYPE_DSV`: 깊이-스텐실 뷰 전용 힙. 셰이더가 직접 읽는 게 아니라 파이프라인의 출력 병합기(OM)가 쓰는 거라 셰이더 가시성이 필요 없다.
- `D3D12_DESCRIPTOR_HEAP_TYPE_RTV`: 렌더 타겟 뷰 전용.

이 프로젝트에서 원래 있던 힙은 `SwapChain`의 `mRtvHeap`/`mDsvHeap`(백버퍼용)뿐이었고, 그림자맵을 위해 `mShadowSrvHeap`(셰이더 가시성 있음, 그림자맵을 텍스처로 샘플링하기 위함)과 `mShadowDsvHeap`(그림자맵에 깊이를 렌더링하기 위함)을 새로 만들었다.

### 2.3 루트 시그니처: 루트 디스크립터 vs 디스크립터 테이블

SRV를 셰이더에 바인딩하는 방법은 두 가지다.

**1) 루트 디스크립터** (`InitAsShaderResourceView`, `SetGraphicsRootShaderResourceView`)
GPU 가상주소를 루트 인자로 직접 넘긴다. 디스크립터 힙이 필요 없다. 단, **Buffer 계열(구조화 버퍼, raw 버퍼)에만 쓸 수 있다.** 이 프로젝트의 머티리얼 버퍼(`gMaterialData`, t0)가 이 방식이다.

**2) 디스크립터 테이블** (`InitAsDescriptorTable`, `SetGraphicsRootDescriptorTable`)
디스크립터 힙 안에 있는 (연속된) 디스크립터 범위를 가리키는 방식. `Texture2D`는 루트 디스크립터로 바인딩할 수 없고 **반드시 이 방식을 써야 한다.** 그림자맵(`gShadowMap`, t1)이 이 경우다.

```cpp
CD3DX12_DESCRIPTOR_RANGE shadowTable;
shadowTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0); // t1 레지스터, 1개, space0

CD3DX12_ROOT_PARAMETER slotRootParameter[4];
slotRootParameter[0].InitAsConstantBufferView(0);              // b0, cbPerObject
slotRootParameter[1].InitAsConstantBufferView(1);              // b1, cbPass
slotRootParameter[2].InitAsShaderResourceView(0, 0);           // t0, 머티리얼 버퍼 (루트 디스크립터)
slotRootParameter[3].InitAsDescriptorTable(1, &shadowTable);   // t1, 그림자맵 (디스크립터 테이블)
```

**여기서 실제로 겪은 버그**: `slotRootParameter` 배열은 4개로 늘렸는데 `CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3, slotRootParameter, ...)`처럼 개수 인자를 안 맞추면, 4번째 파라미터는 조용히 무시되고 실제 루트 시그니처엔 안 들어간다. `CreateGraphicsPipelineState`가 "이 셰이더가 쓰는 레지스터를 루트 시그니처가 다 커버하는지" 검증하기 때문에, 셰이더는 t1을 쓰는데 루트 시그니처엔 없으면 PSO 생성 자체가 `E_INVALIDARG`로 실패한다.

**중요한 런타임 규칙**: `SetGraphicsRootSignature()`를 호출하는 순간, 그 이전에 세팅해뒀던 루트 인자들은 전부 무효화된다. 같은 루트 시그니처 객체를 다시 넘겨도 마찬가지다. 그래서 Shadow Pass와 Main Pass처럼 같은 루트 시그니처를 재사용하더라도, 루트 시그니처를 다시 바인딩할 때마다 필요한 루트 인자(CBV/SRV 등)를 매번 다시 세팅해야 한다.

**그림자 패스는 왜 머티리얼 버퍼(2번)/그림자맵 테이블(3번)을 안 넘겨도 되는가**: 그림자 패스 PSO는 픽셀 셰이더가 없고(`shadowPsoDesc.PS = { nullptr, 0 }`), 버텍스 셰이더(`ShadowVS.hlsl`)는 `cbPerObject`(b0)와 `cbPass`(b1)만 참조한다. 즉 그 Draw 호출에서 실제로 GPU가 참조하는 루트 파라미터가 0번, 1번뿐이라 2번/3번은 안 세팅해도 유효하다. **셰이더가 실제로 참조하지 않는 루트 파라미터는 안 묶어도 된다**는 게 핵심 규칙이다.

### 2.4 정적 샘플러 vs 비교 샘플러 (PCF)

일반 샘플러(`SamplerState`)는 텍셀 밀도와 픽셀 밀도가 안 맞을 때(확대/축소) 어떻게 보간할지(Point/Linear/Anisotropic)와 UV가 0~1 범위를 벗어났을 때 어떻게 처리할지(Wrap/Clamp/Border)를 정한다.

그림자맵 샘플링은 여기에 **깊이 비교**라는 완전히 다른 연산이 하나 더 필요하다. 단순히 텍셀을 보간해서 가져오면 안 되고:

1. 주변 텍셀(2x2 등)의 저장된 깊이 각각을 "내 현재 깊이"와 비교해서(`D3D12_COMPARISON_FUNC_LESS_EQUAL`) 텍셀별로 0/1 결과를 만들고
2. 그 0/1 결과들을 보간해서 부드러운 그림자 경계(0~1 사이 값)를 만든다.

이걸 **PCF (Percentage-Closer Filtering)**라고 한다. 순서가 중요한데, "먼저 깊이를 보간하고 나중에 비교"하면 물체 경계(깊이 불연속 지점)에서 서로 다른 표면의 깊이가 섞여 기하학적으로 의미 없는 값이 나오고, 이게 그림자 경계에 아티팩트를 만든다. 그래서 "비교 먼저, 보간 나중"을 하드웨어가 지원하는 전용 필터(`D3D12_FILTER_COMPARISON_*`)와 전용 타입(`SamplerComparisonState`)이 따로 있는 것이다.

```hlsl
// 이렇게 쓰면 컴파일 에러 (X3013):
SamplerState gsamShadow : register(s6);          // 잘못됨
gShadowMap.SampleCmpLevelZero(gsamShadow, uv, depth);

// 맞는 선언:
SamplerComparisonState gsamShadow : register(s6);
```

`SampleCmpLevelZero`는 HLSL 차원에서 반드시 `SamplerComparisonState`만 받는다. 일반 `SamplerState`를 넘기면 매칭되는 오버로드가 없어서 컴파일이 안 된다.

C++ 쪽 정적 샘플러 설정도 비교 기능을 켜야 한다:

```cpp
const CD3DX12_STATIC_SAMPLER_DESC shadow(
    6,                                                  // register(s6)
    D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,   // 비교 + 선형 필터
    D3D12_TEXTURE_ADDRESS_MODE_BORDER,
    D3D12_TEXTURE_ADDRESS_MODE_BORDER,
    D3D12_TEXTURE_ADDRESS_MODE_BORDER,
    0.0f, 16,
    D3D12_COMPARISON_FUNC_LESS_EQUAL,
    D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);
```

`ADDRESS_MODE_BORDER` + 흰색(깊이 1.0) 보더를 쓰는 이유: 광원 절두체 바깥을 샘플링하면 보더값(1.0)이 나오는데, 어떤 물체의 깊이든 1.0보다 작으므로 비교 결과가 항상 "그림자 아님"이 되어 절두체 밖에서 엉뚱하게 그림자가 지는 걸 막아준다.

---

## 3. 셰도우 매핑 파이프라인 흐름 (이 프로젝트 기준)

### 3.1 Shadow Pass

```
1. mShadowMap 리소스를 D3D12_RESOURCE_STATE_DEPTH_WRITE로 전환
2. 뷰포트/시저를 그림자맵 해상도로 설정
3. 그림자맵 DSV를 Clear, OMSetRenderTargets(0, nullptr, ..., &mShadowMap->Dsv())
   → 컬러 타겟이 없으므로(0, nullptr) 깊이만 씀
4. PSO를 "shadow_opaque"로 설정 (PS 없음, 즉 픽셀 셰이더 스테이지가 꺼져있음)
5. 루트 시그니처 바인딩 + cbPass(1번, LightViewProj 포함) 바인딩
6. 모든 렌더 아이템을 ShadowVS로 그림 → 깊이값만 mShadowMap에 기록됨
7. mShadowMap을 다시 D3D12_RESOURCE_STATE_GENERIC_READ로 전환 (이제 셰이더에서 읽을 차례)
```

ShadowVS.hlsl은 `gWorld`(오브젝트 월드 변환)와 `gLightViewProj`(광원의 뷰-투영 행렬)만 있으면 되고, 픽셀 셰이더가 없으니 컬러 계산이 아예 없다. `mInputLayouts["shadow"]`도 `POSITION`만 있으면 충분하다(법선/UV는 깊이 계산에 필요 없음).

### 3.2 Main Pass

```
1. 백버퍼 RTV/DSV Clear, OMSetRenderTargets
2. mShadowSrvHeap을 SetDescriptorHeaps로 커맨드 리스트에 붙임
3. 루트 시그니처 바인딩
4. cbPass(1번), 머티리얼 버퍼(2번), 그림자맵 테이블(3번) 바인딩
5. 렌더 아이템을 Default.hlsl(VS+PS)로 그림
```

Default.hlsl의 VS는 카메라 기준 클립좌표(`PosH`)뿐 아니라 **광원 기준 클립좌표(`ShadowPosH = mul(posW, gLightViewProj)`)도 같이 계산**해서 픽셀 셰이더로 넘긴다. 래스터라이저가 이걸 보간해서 각 픽셀마다 "이 픽셀이 광원 시점에서 어디에 찍히는지"를 알 수 있게 해주는 것이다.

### 3.3 좌표 변환 체인 정리

한 정점이 화면에 그려지기까지, 그리고 그림자 판정을 받기까지 거치는 좌표계:

```
로컬(Model) 
  → [World] → 월드 좌표
  → [View]  → 카메라 뷰 좌표      \
  → [Proj]  → 카메라 클립 좌표     } 이 둘로 화면에 그려짐 (PosH)
  → [/w, 뷰포트 변환] → 화면 좌표  /

월드 좌표
  → [LightView]  → 광원 뷰 좌표    \
  → [LightProj]  → 광원 클립 좌표   } 그림자 판정용 (ShadowPosH)
  → [/w] → 광원 NDC (-1~1, Y up)
  → [* (0.5,-0.5) + 0.5] → 그림자맵 텍스처 UV (0~1, Y down)
```

NDC에서 텍스처 UV로 갈 때 Y를 반전시키는 이유는, NDC는 Y가 위로 갈수록 커지는데(Y-up) 텍스처 좌표는 Y가 아래로 갈수록 커지기 때문(Y-down)이다.

```hlsl
float CalcShadowFactor(float4 shadowPosH)
{
    shadowPosH.xyz /= shadowPosH.w;                 // 원근분할 (직교투영이라 w=1, 습관적으로 넣음)
    float currentDepth = shadowPosH.z;

    float2 shadowUV = shadowPosH.xy * float2(0.5f, -0.5f) + 0.5f;

    if (shadowUV.x < 0 || shadowUV.x > 1 || shadowUV.y < 0 || shadowUV.y > 1 || currentDepth > 1.0f)
        return 1.0f; // 절두체 밖 -> 그림자 아님

    return gShadowMap.SampleCmpLevelZero(gsamShadow, shadowUV, currentDepth).r;
}
```

이 결과(0~1)를 방향광 계산 결과에 곱해서 그림자 진 곳의 직접광을 줄인다:

```hlsl
float shadowFactor = CalcShadowFactor(pin.ShadowPosH);
finalColor += shadowFactor * ComputeDirectionalLight(gLights[i], mat, N, V);
```

---

## 4. 실전에서 만난 버그들

구현 과정에서 겪은 문제를 원인 → 증상 → 해결 순으로. 대부분 "각 단계는 맞게 짰는데 그 단계들을 서로 연결하는 배선 하나가 빠짐" 패턴이었다.

### 버그 1. `mMainLight`가 nullptr — 크래시

**증상**: `XMLoadFloat3(&mMainLight->Direction)`에서 크래시. 인텔리센스도 빨간줄.
**원인**: `Renderer::InitializeLights()`가 정의만 되어있고 `Initialize()`에서 호출이 안 됨. `mMainLight`는 헤더에서 `nullptr`로 초기화된 채로 남아있었음.
**해결**: `Initialize()`에 `InitializeLights()` 호출 추가.
**교훈**: 함수를 만들었다고 끝이 아니라, 실제로 호출 체인에 들어가 있는지 항상 확인. `grep`으로 정의부/호출부 둘 다 찾아보는 습관이 유용함.

### 버그 2. MaterialBuffer 크기와 실제 머티리얼 개수 불일치 — 조용한 메모리 오버런

**증상**: 딱히 크래시는 안 남 (하지만 잠재적으로 위험).
**원인**: `FrameResource`를 만들 때 `materialCount`를 하드코딩된 3으로 넘겼는데, 실제 머티리얼은 5개(`copper`=index 3, `gold`=index 4)까지 있었음. `UploadBuffer<MaterialData>::CopyData()`는 범위 체크 없는 `memcpy`라서, index 3/4에 쓰면 할당된 버퍼 범위를 벗어나서 씀.
**왜 안 죽었나**: D3D12의 `CreateCommittedResource`로 만든 버퍼는 논리적 요청 크기와 무관하게 실제로는 64KB 정렬 단위로 메모리가 잡힌다. 336바이트(112바이트 × 3)짜리 논리 크기로 요청해도 실제 물리/가상 메모리는 64KB 블록이 통째로 커밋되고, 그 안의 "미사용 패딩 영역"에 오버런이 떨어져서 access violation이 안 남. GPU 쪽도 루트 디스크립터라 범위 체크가 없어서 같은 오프셋을 읽어 쓰기/읽기가 우연히 앞뒤가 맞았음. 이건 API가 보장하는 동작이 아니라 구현 디테일에 우연히 기댄 것이라 위험함 (GPU 기반 검증을 켜면 바로 잡힘).
**해결**: `materialCount`를 실제 `mMaterials.size()`로 넘기도록 수정.

### 버그 3. 커맨드 리스트를 Reset 안 하고 그림자 패스를 기록 — 그림자 패스 전체 무효화

**원인**: `Draw()`에서 `commandList->Reset()`이 함수 중간(메인 패스 시작 직전)에만 있었음. 그 앞의 그림자 패스 코드(리소스 배리어, Clear, Draw 등)는 이전 프레임 끝에서 이미 `Close()`된 리스트에 기록하려는 거라 유효하지 않은 호출.
**해결**: `Draw()` 맨 위, `cmdAllocator->Reset()` 직후에 `commandList->Reset(cmdAllocator, ...)`을 먼저 호출하도록 순서 변경. 원래 중간에 있던 두 번째 Reset은 제거하고 `SetPipelineState`로 교체.

### 버그 4. 그림자 패스에 루트 시그니처/PassCB가 안 묶임

**원인**: 그림자 패스에서 `SetPipelineState`만 하고 바로 `DrawRenderItems`를 불렀는데, `DrawRenderItems`는 루트 파라미터 0(오브젝트 CBV)만 아이템마다 세팅하고 1번(PassCB, `gLightViewProj` 포함)은 호출부 책임. 그림자 패스에서 이걸 빼먹으면 `ShadowVS`가 광원 변환 행렬을 못 받음.
**해결**: 그림자 패스 Draw 루프 앞에 `SetGraphicsRootSignature` + `SetGraphicsRootConstantBufferView(1, passCB...)` 추가.

### 버그 5. `ShadowMap::BuildDescriptor`가 실제로 뷰를 안 만듦

**원인**: `BuildDescriptor(hCpuSrv, hGpuSrv, hCpuDsv)`가 `D3D12_SHADER_RESOURCE_VIEW_DESC`/`D3D12_DEPTH_STENCIL_VIEW_DESC` 구조체를 지역 변수로 채우기만 하고 `device->CreateShaderResourceView(...)`/`CreateDepthStencilView(...)`를 호출하지 않음. 함수 시그니처에 `ID3D12Device*`도 없어서 애초에 이 함수 안에서 뷰를 만들 방법이 없었음.
**해결**: 시그니처에 `ID3D12Device* device` 추가, 함수 안에서 실제로 `CreateShaderResourceView`/`CreateDepthStencilView` 호출.

### 버그 6. 루트 시그니처 파라미터 개수(3) vs 실제 배열 크기(4) 불일치 — PSO 생성 실패

**증상**: `CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSOs["opaque"]))`에서 예외.
**원인**: 그림자맵용 디스크립터 테이블을 `slotRootParameter[3]`에 추가했는데, `CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3, slotRootParameter, ...)`의 개수 인자가 여전히 3. 4번째 파라미터가 조용히 무시되어 실제 루트 시그니처엔 t1(그림자맵) 슬롯이 없는 채로 만들어짐. `Default.hlsl`의 PS는 t1을 참조하는데 루트 시그니처가 이를 커버 못 하니 PSO 생성이 `E_INVALIDARG`로 실패.
**해결**: `rootSigDesc(4, slotRootParameter, ...)`로 수정.
**교훈**: 배열 크기를 늘렸으면 그 배열을 사용하는 곳의 "개수" 인자도 같이 찾아서 바꿔야 함 — 컴파일러가 잡아주지 않는 종류의 불일치.

### 버그 7. `SamplerState` vs `SamplerComparisonState`

**증상**: `error X3013: 'SampleCmpLevelZero': no matching 3 parameter intrinsic method`
**원인**: `gsamShadow`를 `SamplerState`로 선언함. `SampleCmpLevelZero`는 `SamplerComparisonState`만 받음.
**해결**: `SamplerComparisonState gsamShadow : register(s6);`로 타입 변경.

### 버그 8. `InitializeDescriptorHeaps()`를 구현은 했는데 호출을 안 함

**증상**: `commandList->OMSetRenderTargets(0, nullptr, false, &mShadowMap->Dsv())`에서 크래시.
**원인**: 힙 생성 + `BuildDescriptor` 호출까지 담긴 `InitializeDescriptorHeaps()` 함수는 제대로 작성했는데, `Initialize()` 안에서 이 함수를 부르는 코드가 없었음. 그래서 `ShadowMap`의 `mCpuDsv` 등은 초기화가 안 된 상태(POD라 기본값도 0이 아니라 쓰레기값)로 남아있었고, 그 쓰레기 핸들을 `OMSetRenderTargets`에 넘기니 크래시.
**해결**: `mShadowMap` 생성 직후에 `InitializeDescriptorHeaps();` 호출 추가.
**교훈**: 버그 1과 완전히 같은 패턴. "함수 구현 완료 = 실제로 실행됨"이 아니다. 새 초기화 함수를 만들 때마다 호출부가 있는지 바로 확인하는 습관을 들이는 게 좋음.

---

## 5. 그림자 품질 관련 개념

### Depth Bias / Slope-Scaled Depth Bias

그림자맵의 해상도는 유한하고, 부동소수점 정밀도도 유한하다. 그래서 어떤 표면의 한 지점이 "자기 자신이 그림자맵에 기록해둔 깊이값"과 비교당할 때, 미세한 오차 때문에 자기 자신에게 그림자가 지는 것처럼 잘못 판정될 수 있다. 이게 **셀프 셰도잉 acne**(표면에 줄무늬처럼 지글거리는 아티팩트)다. 표면이 광원 방향에 비스듬할수록(변화율이 클수록) 심해진다.

이를 막기 위해 그림자 패스의 래스터라이저에서 깊이를 광원 쪽으로 살짝 밀어준다:

```cpp
shadowPsoDesc.RasterizerState.DepthBias = 100000;         // 고정 바이어스
shadowPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f; // 기울기에 비례하는 바이어스
```

- `DepthBias`가 너무 작으면 acne가 남고, 너무 크면 그림자가 물체에서 눈에 띄게 떨어져 보이는 **Peter-panning**(그림자가 물체 발밑에 안 붙고 둥둥 떠 보이는 현상)이 생긴다.
- `SlopeScaledDepthBias`는 표면 기울기가 클수록 바이어스를 더 주는 보정치라, 각도가 완만한 바닥 같은 표면 vs 비스듬한 표면에서 동시에 acne 없이 자연스럽게 맞추는 데 도움이 된다.
- 정답은 없고 씬 스케일/그림자맵 해상도에 맞춰 실제로 보면서 튜닝해야 하는 값.

### 그림자맵 해상도와 절두체 크기

`XMMatrixOrthographicLH(20.0f, 20.0f, 1.0f, 40.0f)`처럼 광원의 직교투영 절두체 크기를 씬보다 너무 크게 잡으면, 그림자맵의 텍셀 하나가 커버하는 월드 공간 면적이 커져서 그림자 경계가 계단처럼 뭉개진다(엘리어싱). 절두체를 씬에 딱 맞게 타이트하게 잡을수록 같은 해상도에서 더 선명한 그림자를 얻는다.

---

## 6. 셰도우맵 구현 체크리스트

새로 셰도우맵을 붙일 때 순서대로 확인하면 좋은 것들:

1. 그림자맵 깊이 텍스처(리소스) 생성 — 포맷은 보통 `R24G8_TYPELESS`로 만들고 DSV는 `D24_UNORM_S8_UINT`, SRV는 `R24_UNORM_X8_TYPELESS`로 서로 다르게 해석
2. SRV용(셰이더 가시성 O) + DSV용(셰이더 가시성 X) 디스크립터 힙 생성
3. 그 힙에 실제로 `CreateShaderResourceView`/`CreateDepthStencilView` 호출 — 그리고 그 호출부가 실제로 실행되는지 확인
4. 루트 시그니처에 그림자맵 텍스처용 디스크립터 테이블 슬롯 추가, 파라미터 개수 인자 확인
5. 비교 샘플러(`SamplerComparisonState`, `D3D12_FILTER_COMPARISON_*`, `ComparisonFunc`) 추가
6. 그림자 전용 PSO — 픽셀 셰이더 없음(색 안 씀), `NumRenderTargets = 0`, DepthBias/SlopeScaledDepthBias 설정
7. Draw() 순서: 커맨드 리스트 Reset → 그림자맵 배리어(WRITE) → 그림자 패스 렌더(루트시그니처+PassCB 바인딩 필수) → 그림자맵 배리어(READ) → 메인 패스 렌더(힙 바인딩 + 전체 루트 파라미터 재바인딩)
8. VS에서 광원 클립좌표(`ShadowPosH`) 계산해서 PS로 전달
9. PS에서 NDC → UV 변환(Y 반전 포함) 후 `SampleCmpLevelZero`로 그림자 팩터 계산, 방향광에 곱함
10. 모든 초기화 함수(`InitializeXXX`)가 실제 `Initialize()` 호출 체인에 들어가 있는지 최종 확인
