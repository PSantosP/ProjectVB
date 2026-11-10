#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimNodeMessages.h"
#include "AnimNode_VBMuteEffects.generated.h"

struct FAnimNotifyEventReference;

// 숨은 갈래(화면에 안 나가고 갱신만 하는 포즈)에서 나온 노티파이에 붙는 표시.
// 엔진의 bIsActive 는 링크 그래프 경계에서 사라져 그래프 메시지 문맥 데이터로 전한다.
// 내보내기 표시는 매크로와 함수에만 붙인다 - 클래스에 붙이면 C2487.
class FVBMutedEffectsNotifyContext : public UE::Anim::IAnimNotifyEventContextDataInterface
{
	DECLARE_NOTIFY_CONTEXT_INTERFACE_API(FVBMutedEffectsNotifyContext, VB_API)
};

class FVBMuteEffectsScope : public UE::Anim::IGraphMessage
{
	DECLARE_ANIMGRAPH_MESSAGE_API(FVBMuteEffectsScope, VB_API);
public:
	VB_API virtual TUniquePtr<const UE::Anim::IAnimNotifyEventContextDataInterface> MakeUniqueEventContextData() const override;
};

namespace VBAnimEffects
{
	// 소리, 흔들림처럼 보이는 포즈에서만 날 노티파이가 부른다. 상태를 바꾸는 노티파이는 부르지 않는다.
	VB_API bool IsMuted(const FAnimNotifyEventReference& EventReference);
}

// 통과 노드. 이 노드 아래(링크 그래프 안까지)에서 갱신된 애셋의 노티파이에 FVBMutedEffectsNotifyContext 를 붙인다.
// 쓰는 곳: ABP_VBCharacter_58 의 슬라이드 중 숨은 이동 그래프 갈래.
USTRUCT(BlueprintInternalUseOnly)
struct VB_API FAnimNode_VBMuteEffects : public FAnimNode_Base
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Links)
	FPoseLink Source;

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
};
