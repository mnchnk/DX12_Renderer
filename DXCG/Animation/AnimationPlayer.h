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

    std::vector<DirectX::XMFLOAT4X4> mBoneTransforms;
};
