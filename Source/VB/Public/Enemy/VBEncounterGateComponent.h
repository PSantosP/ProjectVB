// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Narrative/Story/VBStoryStateBinding.h"
#include "Player/VBPlayerPreparationBinding.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "VBEncounterGateComponent.generated.h"

struct FVBDeathFacts;
class UAnimMontage;
class UAnimSequenceBase;
class UVBHealthComponent;
class APlayerState;
class APlayerController;
class APawn;
class AVBAreaTrigger;
class UVBPlayerReadinessComponent;
class UVBStoryStateComponent;
struct FOnAttributeChangeData;

DECLARE_MULTICAST_DELEGATE(FVBOnEncounterWoken);
DECLARE_MULTICAST_DELEGATE_OneParam(FVBOnEncounterHeldChanged, bool /*bHeld*/);

/** 보류 요청 하나의 처리. ASC 없이 시험하려고 판정만 뗐다. */
enum class EVBEncounterHoldAction : uint8
{
	Ignore, // 같은 값이거나, 쓰러진 조우에 새 보류
	ApplyAndNotify, // 태그, 공격 취소 + 방송(AI 가 행동 트리를 멈추거나 잇는다)
	ApplySilently, // 쓰러지기 전 걸린 보류를 걷는다 - 태그만, 방송 없음(죽은 적의 행동 트리를 잇지 않는다)
};

namespace VBEncounterHold
{
	VB_API EVBEncounterHoldAction Decide(bool bHeldNow, bool bWantHeld, bool bDefeated);
}

/** 잠든 조우의 자세 한 줄 - 조건이 맞으면 이 클립을 되풀이한다. */
USTRUCT()
struct FVBEncounterDormantPose
{
	GENERATED_BODY()

	// 전부 참이면 이 자세. 비면 늘 참이라 목록 끝의 기본 자세로 둔다.
	UPROPERTY(EditAnywhere, Category="VB|Encounter", meta=(BaseStruct="/Script/VB.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> Conditions;

	// 잠든 동안 되풀이할 제자리 클립. 비어 있으면 데이터 오류다(IsDataValid).
	UPROPERTY(EditAnywhere, Category="VB|Encounter")
	TObjectPtr<UAnimSequenceBase> Animation;
};

namespace VBEncounterDormant
{
	// 위에서부터 Holds(줄)이 처음 참인 줄, 없으면 INDEX_NONE. 사실 저장소 없이 시험하려고 판정만 뗐다.
	VB_API int32 Choose(int32 NumPoses, TFunctionRef<bool(int32 PoseIndex)> Holds);
}

/**
 * 배치한 적 하나의 조우 문 - 나오는가 / 언제 깨어나는가 / 쓰러지면 무엇을 남기나 / 시네마틱 중 보류. 서버에서만 판정한다.
 * 이 컴포넌트는 깨어도 되는지만 말하고 행동 트리 시작은 AI 컨트롤러가 한다. 붙이지 않은 적은 바로 깬다.
 * 조건의 "부른 플레이어" 는 첫 플레이어다(싱글/리슨 호스트 전제).
 */
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBEncounterGateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBEncounterGateComponent();

	bool IsAwake() const { return bAwake; }
	bool IsHeld() const { return bHeld; }

	// 시네마틱 동안 이 조우를 멈춘다(State.Encounter.Held + 진행 중 공격 취소). 서버에서만, 같은 값이면 무시.
	// 쓰러진 조우에는 새로 걸지 않고, 풀어도 행동 트리를 잇지 않는다.
	void SetHeld(bool bInHeld);

	const FGameplayTagContainer& GetEncounterTags() const { return EncounterTags; }

	// 보류가 바뀔 때(서버). AI 컨트롤러가 듣는다.
	FVBOnEncounterHeldChanged OnHeldChanged;

	// 깨어날 때 한 번(서버). AI 컨트롤러가 듣고 행동 트리를 시작한다.
	FVBOnEncounterWoken OnWoken;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 이 조우를 가리키는 꼬리표. 규칙 효과 "조우 보류" 가 이것으로 대상을 찾는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Encounter")
	FGameplayTagContainer EncounterTags;

	// 거짓이면 이 적은 나오지 않는다(서버 BeginPlay 에서 한 번 본다). 비면 늘 나온다. 이미 쓰러뜨린 적이 불러오기 후 다시 나오는 것을 막는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Encounter",
		meta=(BaseStruct="/Script/VB.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> ExistConditions;

	// 전부 참이면 깨어난다(사실이 바뀔 때 다시 본다). 비면 바로 깬다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Encounter",
		meta=(BaseStruct="/Script/VB.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> WakeConditions;

	// 대기 중에 체력이 깎이면 깬다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Encounter")
	bool bWakeWhenDamaged = true;

	// 조건/피격 깨우기 모두 현재 플레이어 준비를 확인한다. 기본 false.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Encounter")
	bool bRequirePlayerPreparation = false;

	// 준비 opt-in 의 조건 깨우기 영역(실제 겹침을 본다). 준비된 선공은 영역 밖에서도 깨운다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Encounter", meta=(EditCondition="bRequirePlayerPreparation"))
	TObjectPtr<AVBAreaTrigger> WakeArea;

	// 깨어날 때 적용한다(조건으로 깨든 맞아서 깨든).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Encounter",
		meta=(BaseStruct="/Script/VB.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> WokenEffects;

	// 소유 액터의 죽음이 끝날 때(또는 KillZ 낙하로 파괴될 때) 한 번 적용한다. 레벨 전환, 종료는 해소가 아니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Encounter",
		meta=(BaseStruct="/Script/VB.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> DefeatedEffects;

	// 잠든 동안의 자세. 조건이 처음 맞는 줄을 되풀이하고, 깨어나거나 쓰러지면 멈춘다. 비면 애님 BP 대기 그대로다.
	// 사실이 바뀌면 다시 골라, 시퀀서가 멈춘 자세도 다시 걸린다.
	UPROPERTY(EditAnywhere, Category="VB|Encounter")
	TArray<FVBEncounterDormantPose> DormantPoses;

	// 자세가 바뀔 때 섞는 시간(들어가기, 나오기 같음).
	UPROPERTY(EditAnywhere, Category="VB|Encounter", meta=(ClampMin="0.0"))
	float DormantBlendSeconds = 0.25f;

	// 잠든 자세를 거는 몽타주 슬롯. 공격 몽타주, 시퀀서와 같은 슬롯이어야 장면 시작 때 자세가 비킨다.
	UPROPERTY(EditAnywhere, Category="VB|Encounter")
	FName DormantSlotName = TEXT("DefaultSlot");

private:
	// 서버가 고른 줄(INDEX_NONE = 자세 없음). 클라는 OnRep 으로 같은 자세를 건다.
	UPROPERTY(ReplicatedUsing=OnRep_DormantPoseIndex)
	int32 DormantPoseIndex = INDEX_NONE;
	UFUNCTION()
	void OnRep_DormantPoseIndex();
	// 서버: 자세를 다시 고르고 깨어남을 본다. 클라: 지금 줄을 다시 건다(장면이 멈춘 자세).
	void HandleStoryChanged();
	// 서버: 지금 사실로 자세를 고른다. 깨어났거나 쓰러졌으면 자세 없음.
	void RefreshDormantPose();
	// 양쪽: 고른 자세를 이 적의 메시에 건다. 그 클립이 이미 돌면 그대로 두고, 멈춰 있으면(시퀀서가 멈춤) 다시 건다.
	void ApplyDormantPose();
	TWeakObjectPtr<UAnimMontage> DormantMontage;
	TWeakObjectPtr<UAnimSequenceBase> DormantAnimation;

#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBEncounterConsumerTestAccess;
#endif
	void EvaluateWake();
	void RequestEvaluateWake();
	void Wake(const TCHAR* Why, bool bFromDamage = false);
	void RefreshPlayerBindings();
	void DisconnectPlayerBindings();
	// 피해로 깨기만 한다(bWakeWhenDamaged). 쓰러짐은 소유 액터의 죽음 완료(HandleOwnerDeathCompleted)로 받는다.
	void HandleHealthChanged(const FOnAttributeChangeData& Data);
	void HandleOwnerDeathCompleted(const FVBDeathFacts& Facts);
	// 해소 한 번(쓰러짐, 사라짐). DefeatedEffects 를 적용한다.
	void ApplyDefeated(APlayerState* Killer, const TCHAR* Why);
	APlayerState* GetFirstPlayerState() const;

	bool bAwake = false;
	bool bHeld = false;
	bool bDefeated = false;
	// 존재 조건을 통과해 실제로 나왔다 - 스스로 사라진 경우(존재 조건 거짓)를 해소로 세지 않으려고.
	bool bExisted = false;
	FVBStoryStateBinding StoryBinding;
	TWeakObjectPtr<AVBAreaTrigger> BoundWakeArea;
	FDelegateHandle HealthChangedHandle;
	TWeakObjectPtr<UVBHealthComponent> BoundHealth;
	FDelegateHandle DeathCompletedHandle;
	FDelegateHandle AreaOccupancyHandle;
	bool bEvaluatePending = false;
	FVBPlayerPreparationBinding PlayerBinding;
	bool HasValidWakeArea() const;
	bool bWarnedInvalidWakeArea = false;
	bool bEnding = false;
};
