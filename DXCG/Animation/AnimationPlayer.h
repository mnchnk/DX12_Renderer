#pragma once
#include <vector>
#include <DirectXMath.h>

#include "Animation/Skeleton.h"

// 클립 하나를 시간에 따라 재생하고, 셰이더에 올릴 본 행렬 팔레트를 만든다.
//
// Skeleton과 AnimationClip은 ResourceManager가 소유하므로 여기서는 참조만 한다.
// (SetClip에 넘긴 포인터가 살아있는 동안만 유효)
class AnimationPlayer
{
public:
    void SetClip(const Skeleton* skeleton, const AnimationClip* clip);

    void Update(float dt);

    // Skeleton::Bones와 같은 순서. 상수 버퍼에 그대로 올린다.
    const std::vector<DirectX::XMFLOAT4X4>& GetBoneTransforms() const { return mBoneTransforms; }

    // 스키닝용(OffsetMatrix가 곱해진) 팔레트와 달리, 본이 모델 공간의 어디에
    // 있는지를 나타낸다. 뼈대를 눈으로 그릴 때 필요하다.
    const std::vector<DirectX::XMFLOAT4X4>& GetBoneWorldTransforms() const { return mBoneWorldTransforms; }

    const Skeleton* GetSkeleton() const { return mSkeleton; }

    bool IsPlaying() const { return mSkeleton != nullptr && mClip != nullptr; }

    float GetTimeSeconds() const { return mTimeSeconds; }
    void  SetTimeSeconds(float t) { mTimeSeconds = t; }

    bool  IsLooping() const { return mLoop; }
    void  SetLooping(bool loop) { mLoop = loop; }

private:
    // mTimeSeconds 기준으로 팔레트를 다시 계산한다.
    void ComputeBoneTransforms();

    const Skeleton* mSkeleton = nullptr;
    const AnimationClip* mClip = nullptr;

    float mTimeSeconds = 0.0f;
    bool  mLoop = true;

    std::vector<DirectX::XMFLOAT4X4> mBoneTransforms;        // OffsetMatrix 포함 (스키닝용)
    std::vector<DirectX::XMFLOAT4X4> mBoneWorldTransforms;   // OffsetMatrix 없음 (본의 실제 위치)
};
