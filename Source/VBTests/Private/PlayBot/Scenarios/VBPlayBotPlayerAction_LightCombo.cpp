#include "PlayBot/Scenarios/VBPlayBotPlayerActionCommon.h"
#include "PlayBot/VBPlayBotHumanModel.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/VBCombatInputComponent.h"
#include "Character/VBMeleeTraceComponent.h"
#include "Character/VBTargetLockComponent.h"
#include "Character/VBWeaponStateComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBTargetLockConfig.h"
#include "UObject/StrongObjectPtr.h"

namespace VBPlayBotPlayerActions
{
namespace
{
struct FVBPressBasis
{
	FVector2D World = FVector2D::ZeroVector;
	double DeltaDegrees = 0.0;
};

FVBPressBasis DescribePressBasis(const FVBPlayerPressReceipt& Press)
{
	const FVector2D Raw = Press.Raw.GetClampedToMaxSize(1.0);
	const double Radians = FMath::DegreesToRadians(Press.Yaw);
	FVBPressBasis Basis;
	Basis.World = FVector2D(FMath::Cos(Radians) * Raw.Y - FMath::Sin(Radians) * Raw.X,
		FMath::Sin(Radians) * Raw.Y + FMath::Cos(Radians) * Raw.X);
	const double RawYaw = FMath::RadiansToDegrees(FMath::Atan2(Raw.Y, Raw.X));
	const double WorldYaw = FMath::RadiansToDegrees(FMath::Atan2(Basis.World.Y, Basis.World.X));
	Basis.DeltaDegrees = FMath::Abs(FMath::FindDeltaAngleDegrees(RawYaw, WorldYaw));
	return Basis;
}

class FVBLightComboAction : public FVBPlayerSequenceAction
{
public:
	FVBLightComboAction() : FVBPlayerSequenceAction(TEXT("LightCombo")) {}
	bool Setup(FVBPlayBotContext& Ctx, FString& Error) override
	{
		if (!Configure(Ctx, Error) || !Positive(Ctx, TEXT("ComboGapSeconds"), Gap, Error)
			|| !Positive(Ctx, TEXT("AttackRangeCm"), AttackRange, Error)
			|| !Positive(Ctx, TEXT("DirectionAnchorDistanceCm"), AnchorDistance, Error)
			|| !Positive(Ctx, TEXT("DirectionSwitchDistanceCm"), SwitchDistance, Error)
			|| !Positive(Ctx, TEXT("MinBasisDeltaDegrees"), MinBasisDelta, Error)
			|| !Positive(Ctx, TEXT("ContactTargetDistanceCm"), ContactDistance, Error)
			|| !Positive(Ctx, TEXT("ComboOpenerHoldSeconds"), ComboHold, Error)
			|| !Positive(Ctx, TEXT("OriginToleranceCm"), OriginTolerance, Error))
		{
			return false;
		}

		ActionOrigin = Ctx.Player->GetActorLocation();
		const FVBPlayBotHumanProfile& Profile = Ctx.Human->GetProfile();
		if (ComboHold * 1000.0 < Profile.HoldMinMs || ComboHold * 1000.0 > Profile.HoldMaxMs)
		{ Error = TEXT("combo tap duration must stay inside the live human hold profile"); return false; }
		for (const TCHAR* Name : {TEXT("Input.Attack.Light"), TEXT("Input.Crouch"), TEXT("Input.WeaponSlot.1"), TEXT("Input.WeaponSlot.3")})
			if (!VBPlayBotSkills::EnsureAction(Ctx, FName(Name), Error))
			{
				return false;
			}
		FString TargetPath;
		if (!Ctx.Config->RequireString(TEXT("TargetClass"), TargetPath, Error))
		{
			return false;
		}

		TargetClass.Reset(FSoftClassPath(TargetPath).TryLoadClass<ACharacter>());
		if (!TargetClass.IsValid()) { Error = TEXT("LightCombo TargetClass did not load"); return false; }
		if (!Observer.Bind(Ctx, Error))
		{
			return false;
		}

		Ctx.OnHit = [this](const FVBPlayBotHit& Hit) { if (Hit.bFromPlayer && Hit.Victim == Target) ++Hits; };
		AddNeutral();
		Sequence.Then<FVBSkillEquip>(TEXT("Fighter"), FName(TEXT("Input.WeaponSlot.3")));
		AddIdleDispatch();
		AddCombo();
		AddStandAttack();
		AddContact(TEXT("Fighter"), TEXT("Input.WeaponSlot.3"));
		AddContact(TEXT("Blade"), TEXT("Input.WeaponSlot.1"));
		AddDirection();
		AddNeutral();
		return true;
	}

	void Teardown(FVBPlayBotContext& Ctx) override
	{
		Observer.Unbind(Ctx); Ctx.OnHit = nullptr;
		for (TWeakObjectPtr<ACharacter> Fixture : ContactFixtures)
		{
			if (Fixture.IsValid())
			{
				Fixture->Destroy();
			}
		}

		FVBPlayerSequenceAction::Teardown(Ctx);
	}

private:
	void AddIdleDispatch()
	{
		Sequence.Then<FVBSkillDo>(TEXT("combat input assembly"), [](FVBPlayBotContext& C, FString&)
		{
			UVBCombatInputComponent* Input = UVBCombatInputComponent::FindFor(C.Player);
			C.Checks->ExpectSCSComponent(C.Player, UVBCombatInputComponent::StaticClass(), TEXT("CombatInputComponent"), Input,
				TEXT("RG.Assembly.C7"), C.Frame);
			C.Checks->Expect(TEXT("RG.Assembly.C7Replicated"), Input && Input->GetIsReplicated(), true, C.Frame);
			return true;
		});
		Sequence.Then<FVBSkillPress>(TEXT("idle Light press"), FName(TEXT("Input.Attack.Light")));
		AddRecovery([] { return 0; });
		Sequence.Then<FVBSkillDo>(TEXT("idle server dispatch"), [this](FVBPlayBotContext& C, FString&)
		{
			C.Checks->Expect(TEXT("RG.C7.IdleServerDispatch"), Observer.Activations == 1, true, C.Frame);
			BaseActivations = Observer.Activations; return true;
		});
	}

	void AddCombo()
	{
		AddTrainingTarget(TEXT("combo"));
		Sequence.Then<FVBSkillDo>(TEXT("watch combo motion cue"), [this](FVBPlayBotContext&, FString&) { bAwaitComboMotion = true; bObserveCombo = true; Sections.Reset(); ComboMontages.Reset(); MaxMontageSections = 0; return true; });
		Sequence.Then<FVBSkillPress>(TEXT("combo opener"), FName(TEXT("Input.Attack.Light")), ComboHold);
		Sequence.Then<FVBSkillWaitUntil>(TEXT("next press scheduled from attack motion"), [this](FVBPlayBotContext&) { return ComboNextPress != INDEX_NONE; }, Timeout);
		AddRecovery([this] { return BaseActivations; });
		Sequence.Then<FVBSkillDo>(TEXT("combo dispatch and section receipts"), [this](FVBPlayBotContext& C, FString&)
		{
			const FString Evidence = FString::Printf(TEXT("{\"events\":%d,\"activations\":%d,\"sections\":%d,\"availableSections\":%d,\"montages\":\"%s\"}"),
				Observer.ComboEvents, Observer.Activations - BaseActivations, Sections.Num(), MaxMontageSections, *FString::Join(ComboMontages.Array(), TEXT("|")).ReplaceCharWithEscapedChar());
			C.Checks->Expect(TEXT("RG.C7.ComboServerDispatch"), Observer.ComboEvents == 1
				&& Observer.Activations == BaseActivations + 1, true, C.Frame, Evidence);
			C.Checks->Expect(TEXT("RG.Combo.SectionAdvanced"), MaxMontageSections > 1 && Sections.Num() > 1 && Hits > 0, true,
				C.Frame, Evidence);
			bObserveCombo = false; return true;
		});
		ReleaseTrainingTarget(TEXT("combo"));

	}

	void AddStandAttack()
	{
		Sequence.Then<FVBSkillPress>(TEXT("crouch"), FName(TEXT("Input.Crouch")));
		Sequence.Then<FVBSkillWaitUntil>(TEXT("actually crouched"), [](FVBPlayBotContext& C) { return C.Player->bIsCrouched != 0; }, Timeout);
		Sequence.Then<FVBSkillDo>(TEXT("stand attack baseline"), [this](FVBPlayBotContext&, FString&) { BaseActivations = Observer.Activations; return true; });
		Sequence.Then<FVBSkillPress>(TEXT("Light asks to stand"), FName(TEXT("Input.Attack.Light")));
		AddRecovery([this] { return BaseActivations; });
		Sequence.Then<FVBSkillDo>(TEXT("stand attack receipt"), [this](FVBPlayBotContext& C, FString&)
		{
			C.Checks->Expect(TEXT("RG.C7.StandBeforeAttack"), !C.Player->bIsCrouched && Observer.CrouchedActivations == 0
				&& Observer.Activations == BaseActivations + 1, true, C.Frame);
			return true;
		});
	}

	void AddDirection()
	{
		ReturnToActionOrigin(TEXT("direction"));
		Sequence.Then<FVBSkillDo>(TEXT("direction fixtures"), [this](FVBPlayBotContext& C, FString& Error) { return PrepareDirection(C, Error); });
		Sequence.Then<FVBSkillWaitUntil>(TEXT("face direction anchor"), [this](FVBPlayBotContext& C)
		{
			return VBPlayBotSkills::TurnCameraToward(C, DirectionPrevious.Get());
		}, Timeout);
		Sequence.Then<FVBSkillPress>(TEXT("lock direction anchor"), FName(TEXT("Input.LockOn")));
		Sequence.Then<FVBSkillWaitUntil>(TEXT("actual anchor lock"), [this](FVBPlayBotContext&)
		{
			const UVBTargetLockComponent* Lock = UVBTargetLockComponent::FindFor(Body.Get());
			return Lock && Lock->GetLockedTarget() == DirectionPrevious.Get();
		}, Timeout);
		Sequence.Then<FVBSkillDo>(TEXT("direction stick fixture"), [this](FVBPlayBotContext& C, FString& Error) { return ConfigureDirectionInput(C, Error); });
		Sequence.Then<FVBSkillWait>(TEXT("direction motor settle"), Gap);
		Sequence.Then<FVBSkillPress>(TEXT("directional Light"), FName(TEXT("Input.Attack.Light")));
		Sequence.Then<FVBSkillDo>(TEXT("release direction stick"), [](FVBPlayBotContext& C, FString&) { C.Input->SetStickWant(FVector2D::ZeroVector); return true; });
		AddRecovery([this] { return DirectionBaseline; });
		Sequence.Then<FVBSkillDo>(TEXT("direction result"), [this](FVBPlayBotContext& C, FString& Error) { return CheckDirectionResult(C, Error); });
		Sequence.Then<FVBSkillPress>(TEXT("unlock after direction"), FName(TEXT("Input.LockOn")));
		AddNeutral();
	}

	bool PrepareDirection(FVBPlayBotContext& C, FString& Error)
	{
		if (!VBPlayBotSkills::EnsureAction(C, TEXT("Input.LockOn"), Error))
		{
			return false;
		}

		const UVBTargetLockComponent* Lock = UVBTargetLockComponent::FindFor(Body.Get());
		const UVBTargetLockConfig* Data = Lock ? Lock->GetTargetLockConfig() : nullptr;
		DirectionRange = Data ? FMath::Min(Data->DirectionalMaxDistance, Data->DirectionalChainMaxDistance) : 0.0f;
		if (!(AnchorDistance < SwitchDistance && SwitchDistance < DirectionRange))
		{ Error = TEXT("direction distances must fit the live directional policy"); return false; }
		DirectionPrevious = VBPlayBotSkills::SpawnStillTarget(C, TargetClass->GetPathName(), AnchorDistance, Error);
		DirectionExpected = VBPlayBotSkills::SpawnStillTarget(C, TargetClass->GetPathName(), SwitchDistance, Error);
		ContactFixtures.Add(DirectionPrevious); ContactFixtures.Add(DirectionExpected);
		if (!DirectionPrevious.IsValid() || !DirectionExpected.IsValid())
		{
			return false;
		}
		// 현재 표적 유지 범위 밖에 둬 방향 전환 입력을 검증한다.
		const FVector Side = FVector::CrossProduct(FVector::UpVector, C.Player->GetActorForwardVector()).GetSafeNormal2D();
		const FVector SwitchLocation = C.Player->GetActorLocation() + Side * SwitchDistance;
		if (Side.IsNearlyZero() || !DirectionExpected->SetActorLocation(SwitchLocation, false, nullptr, ETeleportType::TeleportPhysics))
		{
			Error = TEXT("direction fixture could not place the orthogonal target");
			return false;
		}

		if (!VBPlayBotSkills::StopAI(DirectionPrevious.Get()) || !VBPlayBotSkills::StopAI(DirectionExpected.Get()))
		{ Error = TEXT("direction fixture brain is still running"); return false; }
		return true;
	}

	bool ConfigureDirectionInput(FVBPlayBotContext& C, FString& Error)
	{
		const UVBTargetLockComponent* Lock = UVBTargetLockComponent::FindFor(Body.Get());
		const UVBTargetLockConfig* Config = Lock ? Lock->GetTargetLockConfig() : nullptr;
		const FVector World = (DirectionExpected->GetActorLocation() - C.Player->GetActorLocation()).GetSafeNormal2D();
		if (!Config || !Config->bDirectionalAttackSwitch
			|| Lock->ProposeDirectionalTarget(FVector2D(World.X, World.Y), DirectionRange) != DirectionExpected.Get())
		{
			Error = TEXT("direction target layout is not uniquely admitted by the live lock policy"); return false;
		}

		DirectionPressBase = Observer.Presses.Num(); DirectionBaseline = Observer.Activations;
		const FVector2D Stick = C.ToStick(World);
		if (Lock->ProposeDirectionalTarget(Stick, DirectionRange) == DirectionExpected.Get())
		{
			Error = TEXT("direction layout cannot distinguish raw-space from world-space intent"); return false;
		}

		C.Input->SetStickWant(Stick); return true;
	}

	bool CheckDirectionResult(FVBPlayBotContext& C, FString& Error)
	{
		const FVBPlayerPressReceipt* Press = Observer.Presses.IsValidIndex(DirectionPressBase) ? &Observer.Presses[DirectionPressBase] : nullptr;
		if (!Press) { C.Checks->Expect(TEXT("RG.C7.DirectionAtPress"), false, true, C.Frame); return true; }
		const FVBPressBasis Basis = DescribePressBasis(*Press);
		const double Difference = Basis.DeltaDegrees;
		const FVector2D World = Basis.World;
		if (Difference < MinBasisDelta)
		{
			C.Checks->Unavailable(TEXT("RG.C7.DirectionAtPress"), C.Frame,
				TEXT("fixture world/camera bases too close; cannot distinguish the old raw-space fault"));
		}
		else C.Checks->Expect(TEXT("RG.C7.DirectionAtPress"), DirectionSwitchFrame >= 0 && !World.IsNearlyZero(), true, C.Frame,
			FString::Printf(TEXT("{\"switchFrame\":%lld,\"serial\":%d,\"yaw\":%.3f,\"world\":[%.5f,%.5f],\"basisDelta\":%.3f}"), DirectionSwitchFrame, Press->Serial, Press->Yaw, World.X, World.Y, Difference));
		return true;
	}

	void AddRecovery(TFunction<int32()> Baseline)
	{
		Sequence.Then<FVBSkillWaitUntil>(TEXT("attack completed and released"), [this, Baseline = MoveTemp(Baseline)](FVBPlayBotContext& C)
		{
			return Observer.Activations > Baseline() && Observer.AllReleased() && !Has(C, VBGameplayTags::State_Combat_Attacking);
		}, Timeout);
	}

	void ReturnToActionOrigin(const FString& Label)
	{
		Sequence.Then<FVBSkillApproachPoint>(Label + TEXT(" return to measured action origin"), [this](FVBPlayBotContext&) { return ActionOrigin; }, OriginTolerance, Timeout);
		Sequence.Then<FVBSkillDo>(Label + TEXT(" settle fixture input"), [](FVBPlayBotContext& C, FString&)
		{ C.Input->SetStickWant(FVector2D::ZeroVector); C.Input->SetLookWant(FVector2D::ZeroVector); return true; });
		Sequence.Then<FVBSkillWaitUntil>(Label + TEXT(" grounded origin settled"), Neutral, Timeout);
	}

	void AddTrainingTarget(const FString& Label)
	{
		ReturnToActionOrigin(Label);
		Sequence.Then<FVBSkillDo>(Label + TEXT(" fresh target"), [this](FVBPlayBotContext& C, FString& Error)
		{
			// 기존 정지 대상 fixture 를 다시 쓴다 - 접촉은 맵의 적 수와 무관하다.
			ACharacter* Best = VBPlayBotSkills::SpawnStillTarget(C, TargetClass->GetPathName(), ContactDistance, Error);
			if (!Best)
			{
				return false;
			}

			if (!VBPlayBotSkills::StopAI(Best))
			{ Error = TEXT("contact training target brain is still running"); Best->Destroy(); return false; }
			ContactFixtures.Add(Best);
			Target = Best; C.Target = Best; C.Watch(Best); Hits = 0; BeforeHealth = Health(Best); ContactBaseline = Observer.Activations;
			return true;
		});
		Sequence.Then<FVBSkillApproach>(Label + TEXT(" approach"), [this](FVBPlayBotContext&) { return Target.Get(); }, AttackRange, Timeout);
	}

	void ReleaseTrainingTarget(const FString& Label)
	{
		Sequence.Then<FVBSkillDo>(Label + TEXT(" release training fixture"), [this](FVBPlayBotContext& C, FString&)
		{
			if (Target.IsValid())
			{
				Target->Destroy();
			}

			Target.Reset(); C.Target = nullptr; return true;
		});
	}

	void AddContact(FString Weapon, FName Slot)
	{
		Sequence.Then<FVBSkillEquip>(Weapon, Slot);
		AddTrainingTarget(Weapon);
		Sequence.Then<FVBSkillAttackUntil>(Weapon + TEXT(" real contact"), [this](FVBPlayBotContext&) { return Target.Get(); }, FName(TEXT("Input.Attack.Light")), AttackRange,
			[this](FVBPlayBotContext&, int32) { return Hits > 0; }, Timeout);
		Sequence.Then<FVBSkillDo>(Weapon + TEXT(" contact receipt"), [this, Weapon](FVBPlayBotContext& C, FString&)
		{
			UVBMeleeTraceComponent* Trace = UVBMeleeTraceComponent::FindFor(C.Player);
			C.Checks->ExpectSCSComponent(C.Player, UVBMeleeTraceComponent::StaticClass(), TEXT("MeleeTraceComponent"), Trace,
				TEXT("RG.Assembly.Trace.") + Weapon, C.Frame);
			C.Checks->Expect(TEXT("RG.C9b.") + Weapon + TEXT("Contact"), Trace && Hits > 0 && Health(Target.Get()) < BeforeHealth,
				true, C.Frame,
				FString::Printf(TEXT("{\"hits\":%d,\"before\":%.3f,\"after\":%.3f}"), Hits, BeforeHealth, Health(Target.Get())));
			return true;
		});
		AddRecovery([this] { return ContactBaseline; });
		ReleaseTrainingTarget(Weapon);
	}

	void Observe(FVBPlayBotContext& Ctx) override
	{
		Observer.Observe(Ctx);
		if (bAwaitComboMotion && Observer.Activations > BaseActivations && Has(Ctx, VBGameplayTags::State_Combat_Attacking))
		{
			ComboNextPress = VBPlayBotSkills::ReactPress(Ctx, FName(TEXT("Input.Attack.Light")), FKey(),
				TEXT("visible opening swing"), Ctx.Frame, 0.0);
			bAwaitComboMotion = false;
		}

		const UVBTargetLockComponent* Lock = UVBTargetLockComponent::FindFor(Body.Get());
		if (DirectionExpected.IsValid() && Observer.Presses.Num() > DirectionPressBase && Lock
			&& Lock->GetLockedTarget() == DirectionExpected.Get() && DirectionSwitchFrame < 0)
		{
			DirectionSwitchFrame = Ctx.Frame;
		}

		const USkeletalMeshComponent* Mesh = Ctx.Player->GetMesh();
		const UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
		const UAnimMontage* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
		if (Montage && bObserveCombo && Observer.Activations > BaseActivations && Has(Ctx, VBGameplayTags::State_Combat_Attacking))
		{
			Sections.Add(Anim->Montage_GetCurrentSection(Montage));
			ComboMontages.Add(Montage->GetPathName());
			MaxMontageSections = FMath::Max(MaxMontageSections, Montage->GetNumSections());
		}
	}

	void Finish(FVBPlayBotContext& Ctx) override
	{
		Observer.CheckReceipts(Ctx, TEXT("RG.C7.PressReceipt."));
		Ctx.Checks->Expect(TEXT("RG.C7.PressSerialRelease"), Observer.AllReleased(), true, Ctx.Frame,
			FString::Printf(TEXT("{\"injectedPresses\":%d}"), Observer.Presses.Num()));
		FVBPlayerSequenceAction::Finish(Ctx);
	}

	FVBPlayerPressObserver Observer;
	TWeakObjectPtr<ACharacter> Target;
	TWeakObjectPtr<ACharacter> DirectionPrevious, DirectionExpected;
	float DirectionRange = 0.0f;
	TSet<FName> Sections;
	TSet<FString> ComboMontages;
	int32 MaxMontageSections = 0, ContactBaseline = 0, DirectionBaseline = 0;
	bool bObserveCombo = false;
	TArray<TWeakObjectPtr<ACharacter>> ContactFixtures;
	bool bAwaitComboMotion = false;
	int32 ComboNextPress = INDEX_NONE;
	double ContactDistance = 0.0, ComboHold = 0.0, OriginTolerance = 0.0;
	FVector ActionOrigin = FVector::ZeroVector;
	TStrongObjectPtr<UClass> TargetClass;
	double Gap = 0.0, AnchorDistance = 0.0, SwitchDistance = 0.0, AttackRange = 0.0, BeforeHealth = 0.0, MinBasisDelta = 0.0;
	int32 Hits = 0, BaseActivations = 0;
	int32 DirectionPressBase = MAX_int32;
	int64 DirectionSwitchFrame = -1;
};

}
TUniquePtr<FVBPlayBotPlayerAction> CreateLightCombo()
{
	return MakeUnique<FVBLightComboAction>();
}
}
