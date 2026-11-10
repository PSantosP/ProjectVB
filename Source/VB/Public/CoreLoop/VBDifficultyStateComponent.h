// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "VBDifficultyStateComponent.generated.h"

class UVBDifficultyConfig;

DECLARE_MULTICAST_DELEGATE(FVBOnDifficultyChanged);

/**
 * 지금 난이도 단계를 관리한다. AVBGameState 에 붙고 복제된다. 쓰기는 서버의 SetLevel 하나뿐이다.
 * 처음 값은 이 월드를 연 활성 세이브에서 InitializeComponent 가 읽는다(새 게임엔 ReadSave 가 오지 않는다).
 * 들고 있는 태그는 늘 정규화돼 있다(빈, 모르는 태그 -> 기본 단계).
 */
UCLASS(ClassGroup=(VB))
class VB_API UVBDifficultyStateComponent : public UActorComponent, public IVBSaveParticipant
{
	GENERATED_BODY()

public:
	UVBDifficultyStateComponent();

	// 지금 월드의 GameState 에 붙은 것. 없으면 nullptr(메뉴 월드 등).
	static UVBDifficultyStateComponent* Get(const UObject* WorldContextObject);

	FGameplayTag GetLevel() const { return CurrentLevel; }

	// 피해 하나에 곱할 값(VBDifficultyRules::ResolveDamageScale). 설정 자산이 없으면 1.
	float GetDamageScale(bool bVictimIsPlayer, bool bFinisherDamage) const;

	// 서버에서만, 목록에 있는 단계만. 바뀌었으면 true + OnDifficultyChanged.
	bool SetLevel(FGameplayTag Level);

	// 서버에서는 바꾼 직후, 클라에서는 복제가 왔을 때.
	FVBOnDifficultyChanged OnDifficultyChanged;

	//~ IVBSaveParticipant - FVBDifficultySaveData
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

protected:
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(ReplicatedUsing=OnRep_CurrentLevel)
	FGameplayTag CurrentLevel;

	// 피해마다 설정을 다시 찾지 않도록 처음 쓸 때 잡아 둔다. 자산은 설정이 가리키는 하나뿐이라 월드 수명 동안 바뀌지 않는다.
	UPROPERTY(Transient)
	mutable TObjectPtr<const UVBDifficultyConfig> Config;
	mutable bool bConfigLookedUp = false;

	UFUNCTION()
	void OnRep_CurrentLevel();

	const UVBDifficultyConfig* GetConfig() const;
	// 소유 액터가 없으면(단위 시험) 쓴다.
	bool HasWriteAuthority(const TCHAR* What) const;
	// 저장에서 온 값을 정규화해 넣는다. 목록에 없는 단계면 기본 단계로 바꾸고 말한다(단계를 지운 뒤 옛 슬롯).
	void AdoptSavedLevel(FGameplayTag Saved, const TCHAR* From);
};
