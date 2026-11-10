// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "Progression/VBGrowthRules.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "VBProgressionComponent.generated.h"

class UAbilitySystemComponent;
class UVBGrowthConfig;
class UVBSkillTreeDefinition;
struct FVBSkillNode;

DECLARE_MULTICAST_DELEGATE(FVBOnProgressionChanged);

/**
 * 성장(경험치, 레벨, 스킬 트리)을 관리한다. AVBPlayerState 에 붙어 폰이 죽어도 남는다.
 * 진실은 누적 경험치(TotalXP)와 연 칸 목록(Unlocked) 둘뿐이고 레벨, 포인트는 표로 센다.
 * 쓰기는 서버만 한다. 화면은 RequestUnlock 만 보내고 서버가 다시 판정한다.
 */
UCLASS(ClassGroup=(VB))
class VB_API UVBProgressionComponent : public UActorComponent, public IVBSaveParticipant
{
	GENERATED_BODY()

public:
	UVBProgressionComponent();

	// 폰, 컨트롤러, PlayerState 어느 쪽을 줘도 그 플레이어의 성장. 없으면 nullptr.
	static UVBProgressionComponent* FindFor(const AActor* Actor);

	const UVBGrowthConfig* GetConfig() const { return Config.Get(); }
	const UVBSkillTreeDefinition* GetSkillTree() const;
	int32 GetLevel() const;
	int32 GetMaxLevel() const;
	// 이 레벨이 시작된 뒤 모은 양 / 다음 레벨까지 필요한 양(최대 레벨이면 0/0).
	int32 GetXPIntoLevel() const;
	int32 GetXPForNextLevel() const;
	// 표시용. 음수가 되면 0.
	int32 GetPointsAvailable() const;
	const TArray<FGameplayTag>& GetUnlocked() const { return Unlocked; }
	// 세이브를 읽어 바뀐 횟수. 이 값이 바뀐 갱신은 불러오기라 HUD 가 알림 없이 기준만 잡는다.
	int32 GetSaveReadSerial() const { return SaveReadSerial; }
	bool IsUnlocked(const FGameplayTag& NodeId) const { return Unlocked.Contains(NodeId); }
	EVBSkillUnlockResult CanUnlock(const FGameplayTag& NodeId) const;

	// 화면이 부른다. 결과는 OnProgressionChanged 로 돌아온다.
	UFUNCTION(BlueprintCallable, Category="VB|Growth")
	void RequestUnlock(FGameplayTag NodeId);

	// 서버에서만. 경험치 효과, 개발용 명령이 부른다. 레벨이 오르면 Event.Progression.LevelUp.
	void GiveXP(int32 Amount);
	// 서버에서만. 열었으면 Unlocked.
	EVBSkillUnlockResult Unlock(const FGameplayTag& NodeId);

	// 서버는 바꾼 직후, 클라는 복제가 도착했을 때.
	FVBOnProgressionChanged OnProgressionChanged;

	//~ IVBSaveParticipant - 성장 구역. 문맥의 PlayerState 가 소유자일 때만.
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

protected:
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 레벨 표, 포인트, 트리, 능력치 효과. 비면 경험치는 쌓이되 레벨 1 에 머물고 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Growth")
	TObjectPtr<const UVBGrowthConfig> Config;

private:
	UPROPERTY(ReplicatedUsing=OnRep_Progression)
	int32 TotalXP = 0;

	UPROPERTY(ReplicatedUsing=OnRep_Progression)
	TArray<FGameplayTag> Unlocked;

	// 위 둘과 같은 갱신에 실려 와 원격 클라 HUD 도 불러오기를 가린다.
	UPROPERTY(ReplicatedUsing=OnRep_Progression)
	int32 SaveReadSerial = 0;

	UFUNCTION()
	void OnRep_Progression();

	// WithValidation 없음 - 칸 이름 하나라 깨질 값이 없고 Unlock 이 서버에서 다시 판정한다.
	UFUNCTION(Server, Reliable)
	void ServerUnlock(FGameplayTag NodeId);

	// 서버 전용 - 연 칸이 건 것들.
	FActiveGameplayEffectHandle StatEffectHandle;
	TMap<FGameplayTag, FGameplayAbilitySpecHandle> GrantedAbilities;

	// 연 칸 목록에 맞춰 능력치 효과를 떼고 다시 건다.
	void ReapplyStatEffect();
	// 능력 칸의 능력을 준다. 시작 능력과 겹치면 주지 않고 경고한다.
	void GrantAbility(const FVBSkillNode& Node);
	void RevokeAllAbilities();
	void BroadcastEvent(const FGameplayTag& EventTag, const FGameplayTag& Target, int32 Count) const;
	bool HasWriteAuthority(const TCHAR* What) const;
	UAbilitySystemComponent* GetAbilitySystem() const;
};
