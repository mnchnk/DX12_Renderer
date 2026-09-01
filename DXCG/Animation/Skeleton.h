#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <DirectXMath.h>

#include "Core/MathHelper.h"

// 뼈 하나.
struct BoneInfo
{
    std::string Name;
    int ParentIndex = -1;   // 계층 구조. -1이면 루트 본

    // inverse bind pose. 바인드 포즈(보통 T포즈) 상태의 정점을
    // 이 본의 로컬 공간으로 되돌리는 행렬이다.
    //
    // 정점은 모델 공간 좌표로 저장돼 있는데, 본이 움직인 만큼 따라가게 하려면
    // 먼저 본 기준 좌표로 옮겼다가(OffsetMatrix) 현재 본 자세를 다시 적용해야 한다.
    //   finalMatrix = OffsetMatrix * (현재 본의 월드 행렬)
    DirectX::XMFLOAT4X4 OffsetMatrix = MathHelper::Identity4x4();

    // 애니메이션 채널이 없는 본을 위한 기본 자세 (노드의 로컬 변환)
    DirectX::XMFLOAT4X4 LocalBindTransform = MathHelper::Identity4x4();
};

// 본 목록 + 계층. 부모가 항상 자식보다 앞에 오도록 정렬해두면
// 한 번의 순회로 월드 행렬을 누적할 수 있다.
struct Skeleton
{
    std::vector<BoneInfo> Bones;
    std::unordered_map<std::string, int> BoneIndexByName;

    // 모델 전체에 걸린 루트 변환. FBX는 단위/축 보정이 여기 들어있는 경우가 많다.
    DirectX::XMFLOAT4X4 GlobalInverseTransform = MathHelper::Identity4x4();

    bool IsEmpty() const { return Bones.empty(); }
    int GetBoneCount() const { return (int)Bones.size(); }

    int FindBone(const std::string& name) const
    {
        auto it = BoneIndexByName.find(name);
        return (it != BoneIndexByName.end()) ? it->second : -1;
    }
};

// 시간축 위의 키 하나.
template<typename T>
struct Keyframe
{
    float Time = 0.0f;   // 틱 단위 (초가 아님)
    T Value{};
};

// 본 하나가 시간에 따라 어떻게 움직이는지.
// 위치/회전/스케일은 키 개수가 서로 다를 수 있어 따로 보관한다.
struct BoneAnimation
{
    std::vector<Keyframe<DirectX::XMFLOAT3>> PositionKeys;
    std::vector<Keyframe<DirectX::XMFLOAT4>> RotationKeys;   // 쿼터니언
    std::vector<Keyframe<DirectX::XMFLOAT3>> ScaleKeys;

    bool IsEmpty() const
    {
        return PositionKeys.empty() && RotationKeys.empty() && ScaleKeys.empty();
    }
};

// 애니메이션 클립 하나 (걷기, 뛰기 등).
struct AnimationClip
{
    std::string Name;

    float Duration = 0.0f;          // 틱 단위 전체 길이
    float TicksPerSecond = 25.0f;   // 0이면 파일에 값이 없다는 뜻이라 기본값을 쓴다

    // Skeleton::Bones와 같은 인덱스로 대응된다.
    // 애니메이션이 없는 본은 IsEmpty()가 true.
    std::vector<BoneAnimation> BoneAnimations;

    float GetDurationSeconds() const
    {
        return (TicksPerSecond > 0.0f) ? (Duration / TicksPerSecond) : 0.0f;
    }
};
