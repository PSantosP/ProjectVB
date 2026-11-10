// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "VBPassiveComponent.generated.h"

class UAbilitySystemComponent;
class UVBPassiveSetDefinition;

UENUM()
enum class EVBPassiveGrantResult : uint8
{
	Acquired,
	AlreadyAcquired,
	// 패시브 목록에 없는 태그 - 아이템 데이터가 틀렸다.
	UnknownPassive,
	// 서버가 아니거나 목록, ASC 가 없다.
	Unavailable,
};

// bRestored = 세이브를 읽었거나 처음 받은 복제(접속, 이동)다. 받는 쪽(얻는 연출)은 이때 연출하지 않는다.
DECLARE_MULTICAST_DELEGATE_OneParam(FVBOnPassivesChanged, bool /*bRestored*/);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVBOnPassivesChangedBP, bool, bRestored);

/** 복제되는 패시브 상태 한 묶음 - 목록과 불러옴 횟수가 한 번에 도착해 OnRep 이 한 번만 돈다. */
USTRUCT()
struct FVBPassiveReplicatedState
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FGameplayTag> Acquired;

	// 세이브를 읽을 때마다 하나 오른다 - 클라가 불러옴을 새로 얻음과 가른다.
	UPROPERTY()
	int32 SaveReadSerial = 0;
};

/**
 * 귀속 아이템으로 얻는 패시브(회피, 패링, 반격, 처형)를 관리한다(AVBPlayerState 에 붙음). 기준 값은 Acquired 목록 하나다.
 * Passive.* 태그를 ASC 에 다는 곳도 여기뿐이고, 능력을 여는 일은 전투 규칙 표가 한다. 쓰기는 서버만.
 */
UCLASS(ClassGroup=(VB))
class VB_API UVBPassiveComponent : public UActorComponent, public IVBSaveParticipant
{
	GENERATED_BODY()

public:
	UVBPassiveComponent();

	// 폰, 컨트롤러, PlayerState 어느 쪽을 줘도 그 플레이어의 패시브. 없으면 nullptr.
	static UVBPassiveComponent* FindFor(const AActor* Actor);

	UFUNCTION(BlueprintPure, Category="VB|Passive")
	const TArray<FGameplayTag>& GetAcquired() const { return State.Acquired; }

	// 서버에서만. 같은 패시브를 두 번 줘도 한 번이다.
	EVBPassiveGrantResult Grant(const FGameplayTag& PassiveTag);

	// 실제 주기와 같은 검사를 하되 상태와 알림을 바꾸지 않는다. Acquired 는 지금 줄 수 있다는 뜻이다.
	EVBPassiveGrantResult CanGrant(const FGameplayTag& PassiveTag) const;

	// 서버는 바꾼 직후, 클라는 복제가 도착했을 때.
	FVBOnPassivesChanged OnPassivesChanged;

	UPROPERTY(BlueprintAssignable, Category="VB|Passive")
	FVBOnPassivesChangedBP OnPassivesChangedBP;

	//~ IVBSaveParticipant - 패시브 구역(UVBSaveGame::Passives). 문맥의 PlayerState 가 소유자일 때만.
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

protected:
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 얻을 수 있는 패시브 전부. 비면 아무것도 주지 않고 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Passive")
	TObjectPtr<const UVBPassiveSetDefinition> PassiveSet;

private:
	UPROPERTY(ReplicatedUsing=OnRep_State)
	FVBPassiveReplicatedState State;

	// 클라: 처음 받은 복제는 이미 가진 것이라 연출하지 않는다. 그 뒤로는 불러옴 횟수가 바뀌었을 때만 불러옴으로 본다.
	bool bReceivedFirstState = false;
	int32 SeenSaveReadSerial = 0;

	// 지금 ASC 에 단 패시브 태그. 다시 맞출 때 이것만 뗀다.
	TArray<FGameplayTag> AppliedTags;

	UFUNCTION()
	void OnRep_State();

	void SyncTags();
	void NotifyPassivesChanged(bool bRestored);
	bool HasWriteAuthority(const TCHAR* What) const;
	UAbilitySystemComponent* GetAbilitySystem() const;
};
