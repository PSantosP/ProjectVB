// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "AttributeSet.h"                 // FGameplayAttribute - TArray 멤버라 전방선언 불가
#include "AbilitySystem/VBAbilitySet.h"   // FVBAbilitySetGrantedHandles - 값 멤버라 전방선언 불가
#include "SaveSystem/VBSaveParticipant.h" // 빌드 구역의 저장 참여자
#include "VBPlayerState.generated.h"

// 전방선언 (헤더 include 최소화)
class UVBAbilitySystemComponent;
class UVBHealthAttributeSet;
class UVBCombatAttributeSet;
class UVBReputationAttributeSet;
class UVBInventoryComponent;
class UVBEquipmentComponent;
class UVBProgressionComponent;
class UVBQuestLogComponent;
struct FVBPlayerBuildSaveData;

/**
 * GAS 의 집 - ASC 와 AttributeSet, 인벤토리, 장비, 성장 등 폰이 바뀌어도 남는 상태를 소유한다.
 */
UCLASS()
class VB_API AVBPlayerState : public APlayerState, public IAbilitySystemInterface, public IVBSaveParticipant
{
	GENERATED_BODY()

public:
	AVBPlayerState();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	//~ IVBSaveParticipant - 빌드 구역. 문맥의 PlayerState 가 자기일 때만 쓴다.
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

protected:
	// 저장 참여 등록은 여기서 한다 - Continue 적용이 월드 BeginPlay 보다 먼저 온다.
	virtual void PostInitializeComponents() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	
	// 커스텀 ASC 접근자 (다운 캐스트 없이 바로 사용)
	UVBAbilitySystemComponent* GetVBAbilitySystemComponent() const;

	// 시작 능력, 효과(이 PlayerState 의 ASC 에 산다).
	UPROPERTY(EditDefaultsOnly, Category="VB|GAS")
	TObjectPtr<const UVBAbilitySet> StartupAbilitySet;

	// 서버. 시작 세트를 이 PlayerState 수명에 한 번 준다(UVBPawnExtensionComponent 가 부른다).
	void GrantStartupAbilities();

	// 빌드/스탯 저장, 복원(서버). 둘 다 SavedAttributes 를 같은 순서로 돈다.
	// ApplyBuild 는 무기만 바로 넣고 현재값은 HandleSaveFinalized 에서 최종 상한에 맞춰 넣는다.
	void SnapshotBuild(FVBPlayerBuildSaveData& Out) const;
	void ApplyBuild(const FVBPlayerBuildSaveData& In);

	const TArray<FGameplayAttribute>& GetSavedAttributes() const { return SavedAttributes; }
	const TMap<FGameplayAttribute, FGameplayAttribute>& GetSavedAttributeCaps() const { return SavedAttributeCaps; }

	// 세이브 키 = 어트리뷰트 프로퍼티 이름. rename 하면 옛 세이브 값이 유실된다(테스트가 키 집합을 고정한다).
	static FName MakeAttributeSaveKey(const FGameplayAttribute& Attribute);

protected:
	// 저장/복원 대상 어트리뷰트(직렬화 계약). BP 델타가 비우지 못하게 VisibleAnywhere 로 둔다.
	UPROPERTY(VisibleAnywhere, Category="VB|Save")
	TArray<FGameplayAttribute> SavedAttributes;

	// 복원값을 자를 상한(어트리뷰트 -> 상한 어트리뷰트). SetNumericAttributeBase 는 base 를 클램프하지 않기 때문이다.
	// 새로 저장하는 어트리뷰트에 상한이 있으면 여기도 채운다.
	UPROPERTY(VisibleAnywhere, Category="VB|Save")
	TMap<FGameplayAttribute, FGameplayAttribute> SavedAttributeCaps;

	// 상한이 있으면 [0, 상한] 으로, 없으면 그대로. HandleSaveFinalized 에서 부른다.
	float ClampToCap(const UAbilitySystemComponent& ASC, const FGameplayAttribute& Attribute, float Value) const;

	// 읽기 마지막 단계(OnSaveFinalized)에서 저장된 현재값을 최종 상한에 맞춰 넣는다.
	// 장비, 강화 효과가 상한을 다 세운 뒤라야 강화 전 상한으로 잘리지 않는다.
	void HandleSaveFinalized();
	// ReadSave 가 받아 두고 HandleSaveFinalized 가 쓴다. 읽기가 도중에 끊기면 다음 ReadSave 가 덮는다.
	TOptional<TMap<FName, float>> PendingAttributeValues;
	FDelegateHandle SaveFinalizedHandle;
	TWeakObjectPtr<class UVBSaveParticipantSubsystem> SaveRegistry;

	UPROPERTY(VisibleAnywhere, Category="VB|GAS")
	TObjectPtr<UVBAbilitySystemComponent> AbilitySystemComponent;
	
	// ASC 에 등록되는 AttributeSet. GC 참조 전용이라 UPROPERTY() 만 쓴다(Category 를 달면 UHT 경고).
	UPROPERTY()
	TObjectPtr<UVBHealthAttributeSet> HealthAttributeSet;

	UPROPERTY()
	TObjectPtr<UVBCombatAttributeSet> CombatAttributeSet;

	UPROPERTY()
	TObjectPtr<UVBReputationAttributeSet> ReputationAttributeSet;

	// 가진 아이템을 관리한다. 폰이 죽고 다시 스폰되어도 남는다.
	UPROPERTY(VisibleAnywhere, Category="VB|Item")
	TObjectPtr<UVBInventoryComponent> InventoryComponent;

	// 장착한 장비를 관리한다. 폰이 바뀌어도 장비와 효과가 남는다.
	UPROPERTY(VisibleAnywhere, Category="VB|Item")
	TObjectPtr<UVBEquipmentComponent> EquipmentComponent;

	// 강화 단계를 관리한다.
	UPROPERTY(VisibleAnywhere, Category="VB|Item")
	TObjectPtr<class UVBEnhancementComponent> EnhancementComponent;

	// 성장(경험치, 레벨, 스킬 트리)을 관리한다.
	UPROPERTY(VisibleAnywhere, Category="VB|Growth")
	TObjectPtr<UVBProgressionComponent> ProgressionComponent;

	// 귀속 아이템으로 얻는 패시브를 관리한다(스킬 트리와 별개).
	UPROPERTY(VisibleAnywhere, Category="VB|Passive")
	TObjectPtr<class UVBPassiveComponent> PassiveComponent;

	// 튜토리얼 개인 진행을 관리한다.
	UPROPERTY(VisibleAnywhere, Category="VB|Tutorial")
	TObjectPtr<class UVBTutorialComponent> TutorialComponent;

	// 퀘스트 진행을 관리한다.
	UPROPERTY(VisibleAnywhere, Category="VB|Quest")
	TObjectPtr<UVBQuestLogComponent> QuestLogComponent;

	// 시작 세트를 주었는가. 빈 세트를 준 것과 안 준 것을 가르려고 영수증 대신 쓴다.
	bool bStartupGranted = false;
	// 시작 세트가 준 것의 영수증. PlayerState 수명이라 거두지 않는다.
	FVBAbilitySetGrantedHandles StartupGrant;
};
