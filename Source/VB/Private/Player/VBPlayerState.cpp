// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "Player/VBPlayerState.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/Attributes/VBReputationAttributeSet.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/Equipment/VBEquipmentComponent.h"
#include "Inventory/Enhancement/VBEnhancementComponent.h"
#include "Passive/VBPassiveComponent.h"
#include "Progression/VBProgressionComponent.h"
#include "Narrative/Tutorial/VBTutorialComponent.h"
#include "Narrative/Quest/VBQuestLogComponent.h"
#include "Character/VBWeaponStateComponent.h"
#include "Types/VBWeaponStateTypes.h"
#include "VB/VB.h"

AVBPlayerState::AVBPlayerState()
{
	// ASC 생성 - PlayerState가 소유
	AbilitySystemComponent = CreateDefaultSubobject<UVBAbilitySystemComponent>(
		TEXT("AbilitySystemComponent"));
	
	// SetIsReplicated, SetReplicationMode는 UVBAbilitySystemComponent 생성자에서 처리
	
	// HealthAttributeSet 생성 -> ASC 가 하위 객체로 감지해 자동 등록한다.
	HealthAttributeSet = CreateDefaultSubobject<UVBHealthAttributeSet>(
		TEXT("HealthAttributeSet"));
	
	CombatAttributeSet = CreateDefaultSubobject<UVBCombatAttributeSet>(
		TEXT("CombatAttributeSet"));
	
	ReputationAttributeSet = CreateDefaultSubobject<UVBReputationAttributeSet>(
		TEXT("ReputationAttributeSet"));

	InventoryComponent = CreateDefaultSubobject<UVBInventoryComponent>(TEXT("Inventory"));
	EquipmentComponent = CreateDefaultSubobject<UVBEquipmentComponent>(TEXT("Equipment"));
	EnhancementComponent = CreateDefaultSubobject<UVBEnhancementComponent>(TEXT("Enhancement"));
	ProgressionComponent = CreateDefaultSubobject<UVBProgressionComponent>(TEXT("Progression"));
	PassiveComponent = CreateDefaultSubobject<UVBPassiveComponent>(TEXT("Passive"));
	TutorialComponent = CreateDefaultSubobject<UVBTutorialComponent>(TEXT("Tutorial"));
	QuestLogComponent = CreateDefaultSubobject<UVBQuestLogComponent>(TEXT("QuestLog"));

	// 저장 대상은 플레이어가 소유한 값뿐이다. 상한(Max*)은 GE 가 소유해 저장하지 않는다(세이브가 GE 튜닝을 덮지 않게).
	// 옛 세이브의 Max* 키는 이 목록에 없어 그냥 읽히지 않는다.
	SavedAttributes = {
		UVBCombatAttributeSet::GetCinematicSkillGaugeAttribute(),
		UVBHealthAttributeSet::GetHealthAttribute(),
		UVBHealthAttributeSet::GetShieldAttribute(),
		UVBReputationAttributeSet::GetReputationAttribute(),
	};

	// 복원값을 자를 상한. Reputation 은 상한이 없어 넣지 않는다.
	SavedAttributeCaps = {
		{ UVBCombatAttributeSet::GetCinematicSkillGaugeAttribute(), UVBCombatAttributeSet::GetMaxCinematicSkillGaugeAttribute() },
		{ UVBHealthAttributeSet::GetHealthAttribute(), UVBHealthAttributeSet::GetMaxHealthAttribute() },
		{ UVBHealthAttributeSet::GetShieldAttribute(), UVBHealthAttributeSet::GetMaxShieldAttribute() },
	};
}

FName AVBPlayerState::MakeAttributeSaveKey(const FGameplayAttribute& Attribute)
{
	// GetName() 은 프로퍼티 이름 그대로라 기존 세이브 키와 같다.
	return FName(*Attribute.GetName());
}

UAbilitySystemComponent* AVBPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

UVBAbilitySystemComponent* AVBPlayerState::GetVBAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AVBPlayerState::GrantStartupAbilities()
{
	if (!HasAuthority() || bStartupGranted || !AbilitySystemComponent)
	{
		return;
	}
	// 래치를 먼저 세운다 - 부여 도중 OnSpawn 능력의 콜백이 다시 들어와도 두 번 주지 않는다.
	bStartupGranted = true;
	if (!StartupAbilitySet)
	{
		// Warning 인 이유: 세트 없는 C++ PlayerState 로 빙의하는 시험이 많다. 배포 BP 의 세트는 시험 AbilitySet.ShippedSets 가 막는다.
		VB_LOG(Warning, "%s: 시작 능력 세트(StartupAbilitySet)가 비었다 - 이 플레이어는 능력도 초기 스탯도 받지 않는다", *GetName());
		return;
	}

	StartupAbilitySet->GiveToAbilitySystem(AbilitySystemComponent, StartupGrant, this);
}

void AVBPlayerState::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	UVBSaveParticipantSubsystem::Register(this);
	// 이어하기는 BeginPlay 보다 먼저 읽힌다 - 세 번째 단계 신호를 등록과 같은 자리에서 듣는다.
	if (UVBSaveParticipantSubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UVBSaveParticipantSubsystem>() : nullptr)
	{
		SaveRegistry = Registry;
		SaveFinalizedHandle = Registry->OnSaveFinalized.AddUObject(this, &AVBPlayerState::HandleSaveFinalized);
	}
}

void AVBPlayerState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UVBSaveParticipantSubsystem* Registry = SaveRegistry.Get())
	{
		Registry->OnSaveFinalized.Remove(SaveFinalizedHandle);
	}

	SaveRegistry.Reset();
	UVBSaveParticipantSubsystem::Unregister(this);
	Super::EndPlay(EndPlayReason);
}

void AVBPlayerState::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	// base 값을 읽는다 - 버프가 걸린 채 저장해도 버프가 base 로 구워지지 않는다.
	if (Context.PlayerState == this)
	{
		SnapshotBuild(Save.PlayerBuild);
	}
}

void AVBPlayerState::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (Context.PlayerState == this)
	{
		ApplyBuild(Save.PlayerBuild);
	}
	else
	{
		// 다른 플레이어를 읽는 회차다. 지난 회차가 도중에 끊겨 남은 내 값이 그 회차의 세 번째 단계에서 나에게 들어가지 않게 지운다(리슨 서버).
		PendingAttributeValues.Reset();
	}
}

void AVBPlayerState::SnapshotBuild(FVBPlayerBuildSaveData& Out) const
{
	// SavedAttributes 의 base 값을 저장한다(ApplyBuild 가 base 로 복원하므로 대칭이어야 한다).
	// 현재값을 저장하면 버프분이 base 로 구워져 저장할수록 드리프트한다. 버프 중 저장하면 버프분은 사라진다(의도).
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ensureMsgf(ASC, TEXT("SnapshotBuild: ASC 없음")))
	{
		// Out 을 건드리지 않는다(직전 스냅샷 보존). 빈 맵을 쓰면 다음 저장이 멀쩡한 값을 지운다.
		VB_LOG(Error, "SnapshotBuild: ASC 부재 - 어트리뷰트 저장 생략(직전 값 유지)");
		return;
	}

	TMap<FName, float> Values;
	Values.Reserve(SavedAttributes.Num());
	for (const FGameplayAttribute& Attribute : SavedAttributes)
	{
		if (!ensureMsgf(Attribute.IsValid() && ASC->HasAttributeSetForAttribute(Attribute),
			TEXT("SnapshotBuild: 미등록 어트리뷰트 %s"), *Attribute.GetName()))
		{
			continue;
		}

		Values.Add(MakeAttributeSaveKey(Attribute), ASC->GetNumericAttributeBase(Attribute));
	}

	Out.AttributeValues = MoveTemp(Values);

	// 현재 무기 -> UEnum 이름 문자열. None 이거나 폰이 아직 없으면 빈 값.
	if (const UVBWeaponStateComponent* W = UVBWeaponStateComponent::FindFor(GetPawn()))
	{
		const EVBWeaponType T = W->GetResummonWeaponType();
		if (T != EVBWeaponType::None)
		{
			Out.EquippedWeaponId = FName(*StaticEnum<EVBWeaponType>()->GetNameStringByValue((int64)T));
		}
		// 가진 무기도 같은 이름 문자열로. 이것이 없으면 프롤로그에서 주운 무기가 이어하기 뒤 맵 시작값(무기 없음)으로 돌아간다.
		Out.OwnedWeaponIds.Reset();
		for (const EVBWeaponType Owned : W->GetOwnedWeapons())
		{
			Out.OwnedWeaponIds.Add(FName(*StaticEnum<EVBWeaponType>()->GetNameStringByValue((int64)Owned)));
		}

		Out.bOwnedWeaponsSaved = true;
	}
}

float AVBPlayerState::ClampToCap(const UAbilitySystemComponent& ASC, const FGameplayAttribute& Attribute, float Value) const
{
	const FGameplayAttribute* Cap = SavedAttributeCaps.Find(Attribute);
	if (!Cap || !Cap->IsValid() || !ASC.HasAttributeSetForAttribute(*Cap))
	{
		return Value;
	}

	const float Ceiling = ASC.GetNumericAttribute(*Cap);
	if (Value > Ceiling)
	{
		// 조용히 자르지 않는다. 상한을 내린 결과인지 세이브가 깨진 것인지는 눈으로 봐야 구분된다.
		VB_LOG(Log, "저장 복원(세 번째 단계): %s 저장값 %.1f 이 최종 상한 %.1f 초과 - 상한으로 자름",
			*Attribute.GetName(), Value, Ceiling);
	}

	return FMath::Clamp(Value, 0.f, Ceiling);
}

void AVBPlayerState::HandleSaveFinalized()
{
	if (!PendingAttributeValues.IsSet())
	{
		return;
	}

	const TMap<FName, float> Values = PendingAttributeValues.GetValue();
	PendingAttributeValues.Reset();
	// base 값을 직접 세팅해 저장된 값을 재현한다(GE 스택은 건드리지 않는다). 상한은 이 단계 전에 다 섰다.
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ensureMsgf(ASC, TEXT("HandleSaveFinalized: ASC 없음")))
	{
		VB_LOG(Error, "HandleSaveFinalized: ASC 부재 - 어트리뷰트 복원 생략");
		return;
	}

	for (const FGameplayAttribute& Attribute : SavedAttributes)
	{
		if (!Attribute.IsValid() || !ASC->HasAttributeSetForAttribute(Attribute))
		{
			continue;
		}

		if (const float* Value = Values.Find(MakeAttributeSaveKey(Attribute)))
		{
			ASC->SetNumericAttributeBase(Attribute, ClampToCap(*ASC, Attribute, *Value));
		}
		// 키 부재 = 그 어트리뷰트가 없던 시절의 구 세이브. 초기화 GE 값을 유지한다(하위호환 경로).
	}
}

void AVBPlayerState::ApplyBuild(const FVBPlayerBuildSaveData& In)
{
	// 현재값(체력, 보호막, 평판)은 여기서 넣지 않는다 - 상한이 장비, 강화 효과로 아직 바뀔 수 있다. 세 번째 단계(HandleSaveFinalized)가 넣는다.
	PendingAttributeValues = In.AttributeValues;

	// 가진 무기 복원 - 꺼내기 관문이 이 목록을 묻으므로 재소환보다 먼저. 옛 세이브(칸 없음)는 건드리지 않는다.
	if (In.bOwnedWeaponsSaved)
	{
		if (UVBWeaponStateComponent* W = UVBWeaponStateComponent::FindFor(GetPawn()))
		{
			TArray<EVBWeaponType> Owned;
			for (const FName& Id : In.OwnedWeaponIds)
			{
				const int64 Val = StaticEnum<EVBWeaponType>()->GetValueByNameString(Id.ToString());
				if (Val == INDEX_NONE)
				{
					VB_LOG(Warning, "ApplyBuild: 가진 무기 %s 를 모른다 - 버린다(enum 이름이 바뀌었나)", *Id.ToString());
					continue;
				}

				Owned.Add((EVBWeaponType)Val);
			}

			W->SetOwnedWeapons(Owned);
		}
	}

	// 무기 복원: FName -> enum -> 재소환(서버 권위 - GameMode 에서 호출되므로 정합).
	if (!In.EquippedWeaponId.IsNone())
	{
		const int64 Val = StaticEnum<EVBWeaponType>()->GetValueByNameString(In.EquippedWeaponId.ToString());
		if (Val != INDEX_NONE && (EVBWeaponType)Val != EVBWeaponType::None)
		{
			if (UVBWeaponStateComponent* W = UVBWeaponStateComponent::FindFor(GetPawn()))
			{
				W->EnsureWeaponSummoned((EVBWeaponType)Val);
			}
		}
	}

	VB_LOG(Log, "AVBPlayerState::ApplyBuild — 세이브 빌드 적용 완료(무기=%s)", *In.EquippedWeaponId.ToString());
}
