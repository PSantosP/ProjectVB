// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "PlayBot/Scenarios/VBPlayBotScenario_BloodOption.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "AbilitySystem/GameplayCues/VBGC_MeleeImpact.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/Character.h"
#include "PlayBot/VBPlayBotChecks.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "Settings/VBOptionTypes.h"

VB_PLAYBOT_SCENARIO(FVBPlayBotScenario_BloodOption, "BloodOption")

namespace
{
	bool IsPlayingMontage(const ACharacter* Character)
	{
		const UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
		return Anim && Anim->GetCurrentActiveMontage() != nullptr;
	}
}

bool FVBPlayBotScenario_BloodOption::Setup(FVBPlayBotContext& Ctx, FString& OutError)
{
	const FVBPlayBotRunConfig& C = *Ctx.Config;
	FString TargetClass;
	FString AttackText;
	if (!C.RequireString(TEXT("TargetClass"), TargetClass, OutError) || !C.RequireString(TEXT("AttackAction"), AttackText, OutError))
	{
		return false;
	}

	const FName Attack(*AttackText);
	const double TargetDistance = C.GetDouble(TEXT("TargetDistance"), 300.0);
	const double AttackRange = C.GetDouble(TEXT("AttackRange"), 160.0);
	WindowFrames = C.GetInt(TEXT("WindowFrames"), WindowFrames);
	const TArray<FString> Route = C.GetArray(TEXT("OptionsRoute"));
	const TArray<FString> Modes = C.GetArray(TEXT("Mode"));
	const TArray<FString> HitWeapons = C.GetArray(TEXT("HitWeapon"));
	if (Route.IsEmpty() || Modes.IsEmpty() || HitWeapons.IsEmpty())
	{
		OutError = FString::Printf(TEXT("[%s] needs +OptionsRoute, +Mode=<Enum>|<label> and +HitWeapon=<Weapon>|<slot action>"),
			*C.Section);
		return false;
	}

	Ctx.OnHit = [this, &Ctx](const FVBPlayBotHit& Hit)
	{
		if (Active == INDEX_NONE || !Hit.bFromPlayer || Hit.Victim.Get() != Target.Get())
		{
			return;
		}

		FHitWindow& Window = Windows[Active];
		if (Window.bHit)
		{
			return;
		}

		Window.bHit = true;
		Window.HitFrame = Hit.Frame;
		Window.DecalsAtHit = Ctx.DecalCount;
	};

	Sequence.Then<FVBSkillDo>(TEXT("spawn still target"), [this, TargetClass, TargetDistance](FVBPlayBotContext& C2, FString& Error)
	{
		ACharacter* Spawned = VBPlayBotSkills::SpawnStillTarget(C2, TargetClass, TargetDistance, Error);
		if (!Spawned)
		{
			return false;
		}

		Target = Spawned;
		C2.Target = Spawned;
		C2.Watch(Spawned);
		return true;
	});
	Sequence.Then<FVBSkillWaitUntil>(TEXT("target AI stopped and grounded"), [this](FVBPlayBotContext&)
	{
		return Target.IsValid() && VBPlayBotSkills::StopAI(Target.Get()) && FVBPlayBotContext::IsGrounded(Target.Get());
	}, 5.0);

	for (const FString& ModeLine : Modes)
	{
		FString ModeName;
		FString Label;
		if (!ModeLine.Split(TEXT("|"), &ModeName, &Label))
		{
			OutError = FString::Printf(TEXT("Mode '%s' must be <Enum>|<label>"), *ModeLine);
			return false;
		}

		const int64 Wanted = StaticEnum<EVBBloodDisplay>()->GetValueByNameString(ModeName);
		if (Wanted == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("Mode '%s' is not an EVBBloodDisplay value"), *ModeName);
			return false;
		}

		TMap<FString, FString> Vars;
		Vars.Add(TEXT("Value"), Label);
		if (!VBPlayBotSkills::AddMenuPath(Sequence, Route, Vars, OutError))
		{
			return false;
		}
		// 경로(Apply 포함)가 끝나면 옵션 값은 메뉴가 보여 준 값이어야 한다.
		Sequence.Then<FVBSkillDo>(TEXT("readback ") + ModeName, [ModeName, Wanted](FVBPlayBotContext& C2, FString&)
		{
			const EVBBloodDisplay Saved = UVBSaveGameSubsystem::GetSettingsData(C2.World).BloodDisplay;
			C2.Checks->Expect(TEXT("option_readback_") + ModeName, Saved == static_cast<EVBBloodDisplay>(Wanted), true, C2.Frame,
				FString::Printf(TEXT("{\"saved\":\"%s\"}"), *StaticEnum<EVBBloodDisplay>()->GetNameStringByValue(static_cast<int64>(Saved))));
			return true;
		});

		for (const FString& WeaponLine : HitWeapons)
		{
			FString Weapon;
			FString Slot;
			if (!WeaponLine.Split(TEXT("|"), &Weapon, &Slot))
			{
				OutError = FString::Printf(TEXT("HitWeapon '%s' must be <Weapon>|<slot action>"), *WeaponLine);
				return false;
			}

			Weapons.AddUnique(Weapon);
			Sequence.Then<FVBSkillEquip>(Weapon, FName(*Slot));
			// 사람처럼 무기 교체가 끝나고 적이 일어선 뒤에 휘두른다 - 교체 중이나 기절한 적에게 누르면 적중이 없다.
			Sequence.Then<FVBSkillWaitUntil>(TEXT("swap finished"), [](FVBPlayBotContext& C2) { return !IsPlayingMontage(C2.Player); }, 3.0);
			Sequence.Then<FVBSkillWaitUntil>(TEXT("target recovered"), [this](FVBPlayBotContext&) { return !IsPlayingMontage(Target.Get()); }, 8.0);
			Sequence.Then<FVBSkillApproach>(TEXT("approach target"), [this](FVBPlayBotContext&) { return Target.Get(); }, AttackRange, 8.0);
			Sequence.Then<FVBSkillDo>(TEXT("open window ") + ModeName + TEXT("/") + Weapon,
				[this, ModeName, Weapon](FVBPlayBotContext&, FString&)
			{
				FHitWindow& Window = Windows.AddDefaulted_GetRef();
				Window.Mode = ModeName;
				Window.Weapon = Weapon;
				Active = Windows.Num() - 1;
				return true;
			});
			Sequence.Then<FVBSkillPress>(TEXT("attack"), Attack);
			Sequence.Then<FVBSkillWait>(TEXT("watch the hit"), (WindowFrames + 30) / 60.0);
			Sequence.Then<FVBSkillDo>(TEXT("close window"), [this](FVBPlayBotContext&, FString&) { Active = INDEX_NONE; return true; });
		}
	}

	return true;
}

EVBPlayBotState FVBPlayBotScenario_BloodOption::Tick(FVBPlayBotContext& Ctx)
{
	if (Active != INDEX_NONE)
	{
		FHitWindow& Window = Windows[Active];
		if (Window.bHit && Ctx.Frame - Window.HitFrame <= WindowFrames)
		{
			for (const FString& Name : Ctx.NewFX)
			{
				Window.FX.Add(Name);
			}

			Window.MaxDecalDelta = FMath::Max(Window.MaxDecalDelta, Ctx.DecalCount - Window.DecalsAtHit);
		}
	}

	const EVBPlayBotState State = Sequence.Tick(Ctx);
	if (State == EVBPlayBotState::Failed)
	{
		return Fail(Sequence.GetFailure());
	}

	if (State == EVBPlayBotState::Succeeded && !bEvaluated)
	{
		bEvaluated = true;
		Evaluate(Ctx);
	}

	return State;
}

const FVBPlayBotScenario_BloodOption::FHitWindow* FVBPlayBotScenario_BloodOption::Find(const FString& Mode, const FString& Weapon) const
{
	return Windows.FindByPredicate([&](const FHitWindow& W) { return W.Mode == Mode && W.Weapon == Weapon; });
}

namespace
{
	// 피가 보일 때만 임팩트 큐가 띄우는 상처 이펙트 이름들(UVBGC_MeleeImpact::WoundSystem).
	// 로드된 큐 CDO 에서 읽어, Off 검사가 스윙 접촉 수에 따라 흔들리는 이펙트 개수가 아니라 게임 데이터에 기댄다.
	TSet<FString> WoundEffectNames()
	{
		TSet<FString> Names;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (!It->IsChildOf(UVBGC_MeleeImpact::StaticClass()))
			{
				continue;
			}

			const UObject* Defaults = It->GetDefaultObject(false);
			const FObjectProperty* Prop = FindFProperty<FObjectProperty>(*It, TEXT("WoundSystem"));
			const UObject* Wound = Defaults && Prop ? Prop->GetObjectPropertyValue_InContainer(Defaults) : nullptr;
			if (Wound)
			{
				Names.Add(Wound->GetName());
			}
		}

		return Names;
	}

	int32 CountIn(const TArray<FString>& FX, const TSet<FString>& Names)
	{
		int32 Count = 0;
		for (const FString& Name : FX)
		{
			Count += Names.Contains(Name) ? 1 : 0;
		}

		return Count;
	}
}

void FVBPlayBotScenario_BloodOption::Evaluate(FVBPlayBotContext& Ctx)
{
	FVBPlayBotChecks& Checks = *Ctx.Checks;
	const TSet<FString> Wounds = WoundEffectNames();
	for (const FHitWindow& W : Windows)
	{
		const FString Key = W.Mode + TEXT("_") + W.Weapon;
		Checks.Expect(TEXT("hit_landed_") + Key, W.bHit, true, W.HitFrame, FString::Printf(TEXT("{\"fx\":%d,\"decals\":%d}"), W.FX.Num(), W.MaxDecalDelta));
		if (W.bHit)
		{
			Checks.Look(TEXT("blood_") + Key, W.HitFrame - 10, W.HitFrame + WindowFrames, TEXT("blood look at the hit"));
		}
	}

	for (const FString& Weapon : Weapons)
	{
		const FHitWindow* Normal = Find(TEXT("Normal"), Weapon);
		const FHitWindow* Alternate = Find(TEXT("Alternate"), Weapon);
		const FHitWindow* Off = Find(TEXT("Off"), Weapon);
		const int64 Frame = Off ? Off->HitFrame : Ctx.Frame;
		if (!Normal || !Off || !Normal->bHit || !Off->bHit)
		{
			Checks.Unavailable(TEXT("off_vs_normal_") + Weapon, Frame, TEXT("Normal and Off both need a landed hit"));
			continue;
		}

		const int32 NormalWounds = CountIn(Normal->FX, Wounds);
		const int32 OffWounds = CountIn(Off->FX, Wounds);
		const FString Evidence = FString::Printf(TEXT("{\"normalFx\":%d,\"normalWounds\":%d,\"normalDecals\":%d,\"offFx\":%d,\"offWounds\":%d,\"offDecals\":%d,\"woundAssets\":%d}"),
			Normal->FX.Num(), NormalWounds, Normal->MaxDecalDelta, Off->FX.Num(), OffWounds, Off->MaxDecalDelta, Wounds.Num());
		// Off 는 같은 실행의 Normal 과 비교한다 - Normal 에 피가 없으면 Off 가 지울 것도 없다.
		const bool bNormalHasBlood = Normal->MaxDecalDelta > 0 || NormalWounds > 0;
		Checks.Expect(TEXT("normal_shows_blood_") + Weapon, bNormalHasBlood, true, Normal->HitFrame, Evidence);
		if (Normal->MaxDecalDelta > 0)
		{
			Checks.Expect(TEXT("off_no_ground_decal_") + Weapon, Off->MaxDecalDelta == 0, true, Off->HitFrame, Evidence);
		}
		else
		{
			Checks.Unavailable(TEXT("off_no_ground_decal_") + Weapon, Off->HitFrame, TEXT("Normal left no decal on this hit"));
		}

		if (Wounds.IsEmpty() || NormalWounds == 0)
		{
			Checks.Unavailable(TEXT("off_no_wound_effect_") + Weapon, Off->HitFrame,
				TEXT("no authored wound effect seen in Normal to compare against"));
		}
		else
		{
			Checks.Expect(TEXT("off_no_wound_effect_") + Weapon, OffWounds == 0, true, Off->HitFrame, Evidence);
		}

		Checks.Expect(TEXT("off_flash_stays_") + Weapon, Off->FX.Num() - OffWounds >= 1, true, Off->HitFrame, Evidence);
		if (Alternate && Alternate->bHit)
		{
			const int32 AltWounds = CountIn(Alternate->FX, Wounds);
			Checks.Expect(TEXT("alternate_shows_blood_") + Weapon, Alternate->MaxDecalDelta > 0 || AltWounds > 0, true,
				Alternate->HitFrame, FString::Printf(TEXT("{\"altFx\":%d,\"altWounds\":%d,\"altDecals\":%d}"), Alternate->FX.Num(), AltWounds, Alternate->MaxDecalDelta));
		}
	}
}
