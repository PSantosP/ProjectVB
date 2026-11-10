// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBInputPress.h"
#include "AbilitySystem/VBAbilityFailure.h"
#include "VBAbilitySystemComponent.generated.h"

class UVBCombatRuleTable;

DECLARE_MULTICAST_DELEGATE_OneParam(FVBPlayerAbilityFailure, const FVBAbilityFailureReport&);
DECLARE_MULTICAST_DELEGATE_OneParam(FVBAbilityRemoved, FGameplayAbilitySpecHandle);

// PlayerState 나 NPC 의 능력 활성화, 전투 규칙, 입력 누름 기록을 관리한다
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VB_API UVBAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	// 소유 클라이언트의 복제된 능력 목록을 맞춘 뒤 소비자에게 알린다
	FSimpleMulticastDelegate OnAbilitiesReplicated;
	// spec 이 목록을 떠나기 전에 지워지는 능력을 알린다
	FVBAbilityRemoved OnAbilityRemoved;
	virtual void OnRep_ActivateAbilities() override;
	UVBAbilitySystemComponent();
	

	// 옛 맵을 읽기만 한다 - unset 은 0 개가 아니라 키가 없다는 뜻이다.
	// 이 맵만으로는 지금 쓰는 복제 경로를 알 수 없다.
	TOptional<int32> GetReplicatedLooseTagCountForObservation(const FGameplayTag& Tag) const;

	// ActorInfo 설정 시 플래그 업데이트
	virtual void InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor) override;
	virtual void NotifyAbilityFailed(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability,
		const FGameplayTagContainer& FailureReason) override;

	// 로컬 연출 전용 - 입력 한 묶음에 성공한 후보가 없으면 한 번 낸다
	FVBPlayerAbilityFailure OnPlayerAbilityFailure;
	// 능력에 도달하기 전에 거절한 누름(가진 무기 없음)을 같은 경로로 알린다 - 이 캐릭터를 조종하는 로컬 플레이어에게만.
	void PresentPressFailure(const FGameplayTag& FailureTag);

	// 입력 태그 하나를 능력에 즉시 나눠 준다(Lyra AbilityInputTagPressed 자리). 켜진 능력이 받고, 없으면 후보를 차례로 켠다.
	//   PressSerial = 누름 기록 번호(모르면 INDEX_NONE), 능력이 PrepareActivation 을 하면 서버는 그 이벤트 데이터로 켠다.
	//   bForwardClaim = 부른 쪽에 서버로 보낼 길이 없다(로컬 분배) - 가져간 능력이 있으면 서버에도 보낸다.
	void AbilityInputTagPressed(const FGameplayTag& InputTag, int32 PressSerial = INDEX_NONE, bool bForwardClaim = false);
	// 놓음. 켜진 능력이 놓으면 끝나는 능력(UVBGameplayAbility::ShouldCancelOnInputRelease)이면 취소한다. 토글은 다시 누를 때 끈다.
	void AbilityInputTagReleased(const FGameplayTag& InputTag, bool bForwardClaim = false);
	// 가져간 입력을 서버에도 보내나 - 원격 소유 클라에서, 부른 쪽이 서버로 가는 길이 없을 때만.
	static bool ShouldForwardClaimedInput(bool bOwnerAuthoritative, bool bForwardClaim) { return !bOwnerAuthoritative && bForwardClaim; }

	// 누름 기록(§7.2). 소유 클라(리슨 호스트 포함)는 Begin/EndLocalPress 로 번호를 매기고 그 번호를 서버에 보낸다.
	//   서버는 Record* 로 같은 번호를 받는다 - 이미 아는 번호는 무시(중복, 늦은 RPC), 모르는 번호의 놓음은 버린다.
	int32 BeginLocalPress(const FGameplayTag& InputTag);
	// 지금 누르는 중인 누름을 놓았다고 기록하고 그 번호를 돌려준다. 누르는 중이 없으면 INDEX_NONE.
	int32 EndLocalPress(const FGameplayTag& InputTag);
	void RecordPress(const FGameplayTag& InputTag, int32 Serial);
	void RecordRelease(const FGameplayTag& InputTag, int32 Serial);
	const FVBInputPress* FindPress(const FGameplayTag& InputTag, int32 Serial) const;

	// 서버 입구. 물리 기록과 '요청을 받아들였나' 는 다르다 - 리슨 호스트는 로컬 기록이 먼저 있고
	//   서버 요청이 뒤에 온다. 받아들인 요청만 true - 호출자는 true 일 때만 동작, 통지로 잇는다(같은 요청 두 번 = 동작 한 번).
	bool AcceptPressRequest(const FGameplayTag& InputTag, int32 Serial);
	bool AcceptReleaseRequest(const FGameplayTag& InputTag, int32 Serial);
	// 아바타가 바뀌거나 죽으면 지난 누름은 새 Pawn 의 것이 아니다.
	void ClearInputPresses();

	// 태그마다 보존하는 최근 누름 수 - 예약된 누름이 다음 누름들에 밀려나지 않을 만큼.
	static constexpr int32 MaxPressesPerTag = 8;
	// 서버가 받는 누름 번호의 한 번 도약 상한. 신뢰성 RPC 라 정상 클라는 1씩 오른다.
	static constexpr int32 MaxPressSerialJump = 64;

	// 짝 애니메이션은 자체 복제와 수명을 가진다. slot 의 소유자는 정확히 한 생산자다
	bool AcquireExternalMontageOwnership(UObject* InOwner, uint8 PreviousPlayInstanceId);
	uint8 GetMontagePlayInstanceId() const { return GetRepAnimMontageInfo().PlayInstanceId; }
	void ReleaseExternalMontageOwnership(UObject* InOwner);
	// 누르고 있기/토글 판정의 유일한 입구 - 사용자 설정(토글/홀드)과 능력의 토글 성질로 '이 누름이 끄기인가'. 능력 입력과 조준이 같이 묻는다.
	bool IsToggleInput(const FGameplayTag& InputTag, bool bAbilityIsToggle) const;

	virtual float PlayMontage(UGameplayAbility* AnimatingAbility, FGameplayAbilityActivationInfo ActivationInfo,
		UAnimMontage* Montage, float InPlayRate, FName StartSectionName = NAME_None,
		float StartTimeSeconds = 0.0f) override;

	// 전투 규칙 표로 이 이름표의 능력을 지금 시작할 수 있으면 true. 거절이면 OutFailTags 에 엔진과 같은 실패 태그를 단다. 표가 없으면 false.
	bool CanStartUnderCombatRules(const FGameplayTagContainer& Identity, FGameplayTagContainer* OutFailTags = nullptr) const;
	// 캐릭터가 가진 상태 중 이 이름표의 시작을 막는 것이 있으면 true(막는 상태는 OutHeld 에). 표가 없으면 true.
	bool IsBlockedByHeldState(const FGameplayTag& Identity, FGameplayTagContainer* OutHeld = nullptr) const;

protected:
	virtual void OnRep_ReplicatedAnimMontage() override;
	// 능력이 시작하고 끝날 때 표의 능력 줄(실행 중 막기, 시작 시 취소)을 더한다. Lyra ULyraAbilitySystemComponent 와 같은 자리다.
	virtual void ApplyAbilityBlockAndCancelTags(const FGameplayTagContainer& AbilityTags, UGameplayAbility* RequestingAbility,
		bool bEnableBlockTags, const FGameplayTagContainer& BlockTags, bool bExecuteCancelTags,
		const FGameplayTagContainer& CancelTags) override;
	// 상태 태그가 생기면 서버에서 표의 CancelsOnEntry 능력을 취소한다.
	virtual void OnTagUpdated(const FGameplayTag& Tag, bool bTagExists) override;

private:
	FVBInputPress* FindPressMutable(const FGameplayTag& InputTag, int32 Serial);
	// 이 입력을 가져간 켜진 능력의 스펙(목록 순서의 첫 하나). 없으면 nullptr.
	FGameplayAbilitySpec* FindClaimingSpec(const FGameplayTag& InputTag);
	// 표에서 이 이름표를 막는 상태와 필요한 상태를 모은다. 표가 없으면 false(한 번 Error 를 남긴다).
	bool ResolveStartTags(const FGameplayTagContainer& Identity, FGameplayTagContainer& OutBlockedBy,
		FGameplayTagContainer& OutRequired) const;
	// 캐릭터가 가진 태그 중 BlockedBy 에 걸리는 것이 있으면 true(걸린 태그는 OutHeld 에).
	bool HoldsAnyOf(const FGameplayTagContainer& BlockedBy, FGameplayTagContainer* OutHeld) const;
	bool TryActivateFromInput(FGameplayAbilitySpec& Spec, const struct FVBActivationRequest& Request);
	// 누름 처리의 세 단계. 앞 단계가 입력을 가져가면 true 로 끝난다.
	bool TryPressClaimingAbility(const FGameplayTag& InputTag, bool bForwardClaim);
	bool TryPressActiveInputOwner(const FGameplayTag& InputTag, int32 PressSerial);
	bool ActivateInputCandidates(const FGameplayTag& InputTag, int32 PressSerial, const TWeakObjectPtr<AActor>& InputAvatar,
		TOptional<FVBAbilityFailureReport>& OutFirstDisplayFailure);
	void ReportAbilityFailure(const FVBAbilityFailureReport& Report) const;
	// 실패 알림을 받을 자리인가: 그 아바타가 지금 아바타이고 그 PC 가 이 ASC 의 로컬 플레이어다.
	bool IsLocalPresentationTarget(const AActor* Avatar, const APlayerController* PC) const;
	double NowSeconds() const;

	// 태그마다 최근 누름(오래된 것부터). 번호는 단조 증가 - 지운 뒤에도 되돌리지 않아 늦은 RPC 가 새 누름으로 되살아나지 않는다.
	TMap<FGameplayTag, TArray<FVBInputPress>> InputPresses;
	TMap<FGameplayTag, int32> LastPressSerial;
	TMap<FGameplayTag, int32> LastAcceptedPressSerial; // 서버 입구가 마지막으로 받아들인 누름 요청

	UPROPERTY()
	TObjectPtr<UVBCombatRuleTable> CombatRules;

	// 명시적 취소 관계가 있는 같은 입력 후보만 현재 능력의 입력을 인계받을 수 있다.
	bool TryActivateInputReplacement(const FGameplayTag& InputTag, int32 PressSerial, const FGameplayAbilitySpec& InputOwner);

protected:
	virtual void OnRemoveAbility(FGameplayAbilitySpec& Spec) override;
	friend class FVBFinisherLifecycleTest;
	TWeakObjectPtr<UObject> ExternalMontageOwner;
	uint8 SupersededPlayInstanceId = 0;
	bool bHasSupersededMontage = false;
	bool bDeferredExternalMontageRep = false;

};
