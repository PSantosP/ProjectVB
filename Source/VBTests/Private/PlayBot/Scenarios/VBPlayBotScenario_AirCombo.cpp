// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBGA_AirCombo.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Animation/AnimInstance.h"
#include "Character/VBWeaponStateComponent.h"
#include "Character/VBMeleeTraceComponent.h"
#include "Character/VBAirAfterimageComponent.h"
#include "Character/VBTargetLockComponent.h"
#include "Character/Appearance/VBCharacterAppearanceComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBAirComboConfig.h"
#include "Data/VBAirAfterimageConfig.h"
#include "Data/VBBodyFadeConfig.h"
#include "Data/VBCombatFeedbackProfile.h"
#include "Camera/VBCombatFeedbackModifier.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/UObjectIterator.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBWeaponDefinition.h"
#include "Data/VBEnemyConfig.h"
#include "Enemy/VBEnemyBase.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "PlayBot/VBPlayBotChecks.h"
#include "PlayBot/VBPlayBotInputDriver.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "PlayBot/VBPlayBotScenarioBase.h"
#include "PlayBot/VBPlayBotSkills.h"
#include "UObject/UnrealType.h"
#include "AbilitySystem/GameplayCues/VBGC_CombatFeedback.h"
#include "GameplayCueManager.h"
#include "GameplayCueSet.h"
#include "UObject/ObjectKey.h"

// 큐 태그가 재생하는 월드 FX 자산들(그 큐 노티파이의 UVBCombatFeedbackProfile::WorldEffects). 연출 자산 큐가 아니면 빈 집합이다.
static TSet<TObjectKey<UNiagaraSystem>> FindCueWorldSystems(const FGameplayTag& CueTag)
{
	TSet<TObjectKey<UNiagaraSystem>> Systems;
	UGameplayCueManager* Manager = UAbilitySystemGlobals::Get().GetGameplayCueManager();
	UGameplayCueSet* CueSet = Manager ? Manager->GetRuntimeCueSet() : nullptr;
	const int32* Index = CueSet ? CueSet->GameplayCueDataMap.Find(CueTag) : nullptr;
	if (!Index || !CueSet->GameplayCueData.IsValidIndex(*Index))
	{
		return Systems;
	}

	const FGameplayCueNotifyData& Data = CueSet->GameplayCueData[*Index];
	const UClass* NotifyClass = Data.LoadedGameplayCueClass ? Data.LoadedGameplayCueClass.Get()
		: Cast<UClass>(Data.GameplayCueNotifyObj.TryLoad());
	const UVBGC_CombatFeedback* Notify = NotifyClass ? Cast<UVBGC_CombatFeedback>(NotifyClass->GetDefaultObject()) : nullptr;
	if (const UVBCombatFeedbackProfile* Profile = Notify ? Notify->GetProfile() : nullptr)
	{
		for (const FVBCombatFeedbackEffect& Effect : Profile->WorldEffects)
		{
			if (Effect.System)
			{
				Systems.Add(Effect.System.Get());
			}
		}
	}

	return Systems;
}

// 실제 공격 입력으로 띄우기부터 내려찍기와 지상 복귀까지 확인한다.
class FVBPlayBotScenario_AirCombo : public FVBPlayBotScenarioBase
{
public:
	virtual bool Setup(FVBPlayBotContext& Ctx, FString& OutError) override
	{
		FString Weapon, Slot, AttackText, TargetClass, LockText;
		const FVBPlayBotRunConfig& Config = *Ctx.Config;
		if (!Config.RequireString(TEXT("Weapon"), Weapon, OutError) || !Config.RequireString(TEXT("WeaponAction"), Slot, OutError)
			|| !Config.RequireString(TEXT("AttackAction"), AttackText, OutError) || !Config.RequireString(TEXT("TargetClass"),
				TargetClass, OutError)
			|| !Config.RequireString(TEXT("LockOnAction"), LockText, OutError))
		{
			return false;
		}

		LauncherVariant = Config.GetInt(TEXT("LauncherVariant"), INDEX_NONE);
		ForceCombo = IConsoleManager::Get().FindConsoleVariable(TEXT("vb.Combat.ForceCombo"));
		if (!ForceCombo || LauncherVariant < 0)
		{
			OutError = TEXT("AirCombo requires the development ForceCombo setting and a LauncherVariant");
			return false;
		}

		OriginalForceCombo = ForceCombo->GetInt();
		const FName Attack(*AttackText);
		const FString EndMode = Config.GetString(TEXT("EndMode"), TEXT("Slam"));
		if (EndMode != TEXT("Slam") && EndMode != TEXT("Cancel") && EndMode != TEXT("String"))
		{
			OutError = TEXT("AirCombo EndMode must be Slam, Cancel or String");
			return false;
		}

		bCancelString = EndMode == TEXT("Cancel");
		bFullString = EndMode == TEXT("String");
		ExpectedLethalHit = Config.GetInt(TEXT("LethalAirHit"), 0);
		TargetHealth = Config.GetDouble(TEXT("TargetHealth"), 0.0);
		if (ExpectedLethalHit < 0 || ExpectedLethalHit > 4 || (ExpectedLethalHit > 0 && (!bFullString || TargetHealth <= 0.0)))
		{
			OutError = TEXT("LethalAirHit requires String mode, hit1-4 and positive TargetHealth");
			return false;
		}

		SlamReaction = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/VB/Animations/Montage/Enemy/AM_Enemy_AirSlam_Blade"));
		if (!SlamReaction.IsValid()) { OutError = TEXT("Authored victim slam reaction is missing"); return false; }
		bProbeHeights = Config.GetInt(TEXT("ProbeHeights"), 0) != 0;
		const FString RecoveryProbe = Config.GetString(TEXT("RecoveryProbe"), TEXT("Attack"));
		if (RecoveryProbe != TEXT("Attack") && RecoveryProbe != TEXT("Dodge"))
		{
			OutError = TEXT("RecoveryProbe must be Attack or Dodge");
			return false;
		}

		bProbeDodge = RecoveryProbe == TEXT("Dodge");
		Ctx.OnPressInjected = [this, Player = TWeakObjectPtr<ACharacter>(Ctx.Player),
			ASC = TWeakObjectPtr<UAbilitySystemComponent>(Ctx.PlayerASC)](const FVBPlayBotPress& Press)
		{
			if (Press.Cue == TEXT("chase launched target"))
			{
				ChasePressFrame = Press.InjectedFrame;
			}

			if (Press.Cue == TEXT("attack during air recovery") || Press.Cue == TEXT("dodge during air recovery"))
			{
				bRecoveryProbeInAir = Player.IsValid() && Player->GetCharacterMovement()->IsFalling()
					&& ASC.IsValid() && ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_AirRecovery);
			}
		};
		const double Distance = Config.GetDouble(TEXT("TargetDistance"), 350.0);
		const double Range = Config.GetDouble(TEXT("AttackRange"), 170.0);
		Sequence.Then<FVBSkillEquip>(Weapon, FName(*Slot));
		Sequence.Then<FVBSkillDo>(TEXT("prepare air-combo target"), [this, TargetClass, Distance](FVBPlayBotContext& C2, FString& Error)
		{
			const UVBWeaponStateComponent* Weapons = C2.Player->FindComponentByClass<UVBWeaponStateComponent>();
			const UVBWeaponDefinition* WeaponData = Weapons ? Weapons->GetSelectedDefinition() : nullptr;
			const UVBAirComboConfig* Air = WeaponData ? WeaponData->GetAirCombo() : nullptr;
			if (!Air || !Air->StringAttack || !Air->StringAttack->AttackMontage || Air->StringAttack->ComboSectionNames.IsEmpty()
				|| !Air->HasSlam())
			{
				Error = TEXT("Selected weapon has no authored AirCombo/StringAttack montage");
				return false;
			}

			StringMontage = Air->StringAttack->AttackMontage;
			SlamConfig = Air;
			// 땅 충격 FX 는 무기의 SlamImpactCue 가 재생하는 연출 자산이 정한다 - 자산 경로를 여기 박으면 연출을 바꿀 때 거짓 실패한다.
			SlamSystems = FindCueWorldSystems(Air->SlamImpactCue);
			if (SlamSystems.IsEmpty())
			{
				Error = FString::Printf(TEXT("SlamImpactCue %s plays no authored world effect"), *Air->SlamImpactCue.ToString());
				return false;
			}
			FinalStringSection = Air->StringAttack->ComboSectionNames.Last();
			ExpectedFraming = Air->StringCameraFraming;
			ExpectedSlamSpeed = Air->SlamFallSpeed;
			ExpectedChaseFrames = C2.SecondsToFrames(Air->ChaseSeconds);
			ExpectedGhostLimit = Air->Staging && Air->Staging->GhostMaterial ? Air->Staging->MaxGhosts : 0;
			bExpectBodyFade = Air->Staging && Air->Staging->BodyFade;
			if (Air->Staging && Air->Staging->WhooshProfile)
			{
				ExpectedWhoosh = Air->Staging->WhooshProfile->Screen.Material;
			}

			if (const UVBCharacterAppearanceComponent* Appearance = C2.Player->FindComponentByClass<UVBCharacterAppearanceComponent>())
			{
				FadeMesh = Appearance->GetPrimaryVisualMesh();
			}

			if (FadeMesh.IsValid())
			{
				OriginalBodyMaterial = FadeMesh->GetMaterial(0);
			}

			RecordVisualSources(C2);
			SlamStart = Air->SlamStartSection;
			SlamLoop = Air->SlamLoopSection;
			SlamEnd = Air->SlamEndSection;
			bool bGranted = false;
			for (const FGameplayAbilitySpec& Spec : C2.PlayerASC->GetActivatableAbilities())
			{
				bGranted |= Spec.Ability && Spec.Ability->IsA<UVBGA_AirCombo>();
			}

			C2.Checks->Expect(TEXT("air_ability_granted"), bGranted, true, C2.Frame);
			const UVBMeleeTraceComponent* Trace = C2.Player->FindComponentByClass<UVBMeleeTraceComponent>();
			const FByteProperty* Channel = Trace ? FindFProperty<FByteProperty>(Trace->GetClass(), TEXT("TraceChannel")) : nullptr;
			if (!Channel || Channel->GetPropertyValue_InContainer(Trace) >= ECC_MAX)
			{
				Error = TEXT("Melee trace channel is not readable; contact evidence would be incomplete");
				return false;
			}

			TraceChannel = static_cast<ECollisionChannel>(Channel->GetPropertyValue_InContainer(Trace));
			if (bProbeHeights)
			{
				for (const FGameplayTag Part : ProbeShapes())
				{
					FVBResolvedMeleeShape Shape;
					if (Trace->ResolveShape(Part, Shape))
					{
						continue;
					}

					Error = FString::Printf(TEXT("Missing production shape for pose probe: %s"), *Part.ToString());
					return false;
				}
			}

			AirEndedHandle = C2.PlayerASC->OnAbilityEnded.AddLambda([this](const FAbilityEndedData& End)
			{
				if (End.AbilityThatEnded && End.AbilityThatEnded->IsA<UVBGA_AirCombo>())
				{
					bAirEnded = true;
					bAirCancelled = End.bWasCancelled;
				}
			});
			ComboWindowHandle = C2.PlayerASC->GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combo_WindowOpen)
				.AddLambda([this](const FGameplayEventData*) { if (bSawString && !bSawSlam) ++StringWindows; });
			Target = VBPlayBotSkills::SpawnStillTarget(C2, TargetClass, Distance, Error);
			if (!Target.IsValid())
			{
				return false;
			}

			TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target.Get());
			if (!TargetASC.IsValid()) { Error = TEXT("Target has no ASC"); return false; }
			if (TargetHealth > 0.0)
			{
				TargetASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxHealthAttribute(), TargetHealth);
				TargetASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), TargetHealth);
			}

			VictimDeadHandle = TargetASC->RegisterGameplayTagEvent(VBGameplayTags::State_Dead, EGameplayTagEventType::NewOrRemoved)
				.AddLambda([this](const FGameplayTag, int32 Count)
				{
					if (Count <= 0)
					{
						return;
					}

					++VictimDefeats;
					bVictimAirborneAtDefeat = bLastLivingVictimAirborne;
					VictimDefeatSection = LastLivingAirSection;
				});
			if (const AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(Target.Get()))
			{
				const UVBEnemyConfig* EnemyData = Enemy->GetEnemyConfig();
				for (UAnimMontage* Montage : EnemyData->DeathMontages)
				{
					if (Montage)
					{
						StandingDeathMontages.Add(Montage);
					}
				}

				if (EnemyData->DownedDeathMontage)
				{
					StandingDeathMontages.Add(EnemyData->DownedDeathMontage.Get());
				}
			}

			C2.Target = Target;
			C2.Watch(Target.Get());
			C2.OnHit = [this](const FVBPlayBotHit& Hit)
			{
				if (bFullString && Hit.bFromPlayer && Hit.Victim == Target && bSawString && Hit.SectionHit == 4 && FinalHitFrame < 0)
				{
					FinalHitFrame = Hit.Frame;
				}

				if (Hit.bFromPlayer && Hit.Victim == Target && bSawString && !bSawSlam)
				{
					++AirHits;
				}

				if (Hit.bFromPlayer && Hit.Victim == Target && bSawSlamEnd)
				{
					++SlamHits;
				}
			};
			return true;
		});
		Sequence.Then<FVBSkillWaitUntil>(TEXT("target grounded"), [this](FVBPlayBotContext& C2)
		{
			if (!FVBPlayBotContext::IsGrounded(Target.Get()))
			{
				return false;
			}

			VictimGroundZ = Target->GetActorLocation().Z;
			PlayerGroundZ = C2.Player->GetActorLocation().Z;
			ForceCombo->Set(LauncherVariant, ECVF_SetByConsole);
			return true;
		}, 3.0);
		Sequence.Then<FVBSkillPress>(TEXT("lock air-combo target"), FName(*LockText));
		Sequence.Then<FVBSkillWaitUntil>(TEXT("air-combo lock acquired"), [this](FVBPlayBotContext& C2)
		{
			const UVBTargetLockComponent* Lock = UVBTargetLockComponent::FindFor(C2.Player);
			return Lock && Lock->GetLockedTarget() == Target.Get();
		}, 2.0);
		Sequence.Then<FVBSkillDo>(TEXT("validate camera framing ownership"), [this](FVBPlayBotContext& C2, FString& Error)
		{
			UVBTargetLockComponent* Lock = UVBTargetLockComponent::FindFor(C2.Player);
			if (!Lock) { Error = TEXT("Lock camera owner is missing"); return false; }
			const uint64 First = Lock->BeginCameraFraming(Target.Get(), ExpectedFraming);
			const uint64 Second = Lock->BeginCameraFraming(Target.Get(), ExpectedFraming);
			Lock->EndCameraFraming(First);
			C2.Checks->Expect(TEXT("stale_camera_release_rejected"), First != 0 && Second != First
				&& Lock->HasCameraFraming(Second), true, C2.Frame);
			Lock->EndCameraFraming(Second);
			return true;
		});
		Sequence.Then<FVBSkillAttackUntil>(TEXT("launch opens chase"), [this](FVBPlayBotContext&) { return Target.Get(); }, Attack, Range,
			[](FVBPlayBotContext& C2, int32) { return C2.PlayerASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_AirOpening); }, 8.0);
		Sequence.Then<FVBSkillDo>(TEXT("release launcher override"), [this](FVBPlayBotContext& C2, FString&)
		{
			LaunchFrame = C2.Frame;
			ForceCombo->Set(-1, ECVF_SetByConsole);
			return true;
		});
		Sequence.Then<FVBSkillPress>(TEXT("chase launched target"), Attack);
		Sequence.Then<FVBSkillWaitUntil>(TEXT("air string begins"), [this](FVBPlayBotContext&) { return bSawString; }, 3.0);
		if (bCancelString)
		{
			Sequence.Then<FVBSkillDo>(TEXT("cancel active air ability"), [this](FVBPlayBotContext& C2, FString&)
			{
				FGameplayTagContainer Air(VBGameplayTags::Ability_Attack_Air);
				C2.PlayerASC->CancelAbilities(&Air);
				CheckRejectedSlamRequest(C2, C2.Player, Target.Get(), 1500.0f, TEXT("cancelled_launcher_cannot_slam"));
				return true;
			});
		}
		else if (bFullString)
		{
			for (int32 Window = 1; Window <= (ExpectedLethalHit > 0 ? ExpectedLethalHit - 1 : 3); ++Window)
			{
				Sequence.Then<FVBSkillWaitUntil>(TEXT("next air combo window"), [this, Window](FVBPlayBotContext&) { return StringWindows >= Window; }, 2.0);
				Sequence.Then<FVBSkillPress>(TEXT("continue complete air string"), Attack);
			}
		}
		else
		{
			Sequence.Then<FVBSkillDo>(TEXT("validate target slam ownership"), [this](FVBPlayBotContext& C2, FString&)
			{
				CheckRejectedSlamRequest(C2, Target.Get(), Target.Get(), 1500.0f, TEXT("other_actor_cannot_slam"));
				CheckRejectedSlamRequest(C2, C2.Player, C2.Player, 1500.0f, TEXT("wrong_target_cannot_slam"));
				CheckRejectedSlamRequest(C2, C2.Player, Target.Get(), -1.0f, TEXT("invalid_speed_cannot_slam"));
				CheckRejectedSlamRequest(C2, C2.Player, Target.Get(), 1500.0f, TEXT("missing_config_cannot_slam"), false);
				return true;
			});
			for (int32 Index = 0; Index < 2; ++Index)
			{
				Sequence.Then<FVBSkillPress>(TEXT("continue air string"), Attack);
				Sequence.Then<FVBSkillWait>(TEXT("air tap spacing"), 0.08);
			}

			Sequence.Then<FVBSkillPress>(TEXT("hold for slam"), Attack, 0.5);
			Sequence.Then<FVBSkillWaitUntil>(TEXT("slam begins"), [this](FVBPlayBotContext&) { return bSawSlam; }, 1.0);
		}

		if (bFullString)
		{
			Sequence.Then<FVBSkillWaitUntil>(TEXT("air ability ends before landing"), [this](FVBPlayBotContext&) { return bAirEnded; }, 5.0);
			Sequence.Then<FVBSkillDo>(TEXT("observe falling attack restriction"), [](FVBPlayBotContext& C2, FString&)
			{
				C2.Checks->Expect(TEXT("air_recovery_held_while_falling"), C2.Player->GetCharacterMovement()->IsFalling()
					&& C2.PlayerASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_AirRecovery), true, C2.Frame);
				return true;
			});
			Sequence.Then<FVBSkillPress>(bProbeDodge ? TEXT("dodge during air recovery") : TEXT("attack during air recovery"),
				bProbeDodge ? FName(*Config.GetString(TEXT("DodgeAction"), TEXT("Input.Dodge"))) : Attack);
			Sequence.Then<FVBSkillDo>(TEXT("verify recovery input at injection"), [this](FVBPlayBotContext& C2, FString&)
			{
				C2.Checks->Expect(TEXT("recovery_key_injected_while_airborne"), bRecoveryProbeInAir, true, C2.Frame);
				bool bDodgeActive = false;
				for (const FGameplayAbilitySpec& Spec : C2.PlayerASC->GetActivatableAbilities())
				{
					bDodgeActive |= Spec.IsActive() && Spec.Ability && Spec.Ability->GetAssetTags().HasTag(VBGameplayTags::Ability_Defense_Dodge);
				}

				if (bProbeDodge)
				{
					C2.Checks->Expect(TEXT("air_recovery_dodge_allowed"), bDodgeActive, true, C2.Frame);
				}
				else
				{
					C2.Checks->Expect(TEXT("air_recovery_attack_rejected"),
						!C2.PlayerASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Attacking), true, C2.Frame);
				}

				return true;
			});
		}

		Sequence.Then<FVBSkillWaitUntil>(TEXT("air combo returns to ground"), [](FVBPlayBotContext& C2)
		{
			return FVBPlayBotContext::IsGrounded(C2.Player) && !C2.PlayerASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Attacking_Air);
		}, 5.0);
		Sequence.Then<FVBSkillWait>(TEXT("observe recovery and impact lifetime"), 2.0);
		return true;
	}

	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override
	{
		if (const UVBAirAfterimageComponent* Ghosts = Ctx.Player ? Ctx.Player->FindComponentByClass<UVBAirAfterimageComponent>() : nullptr)
		{
			const int32 Count = Ghosts->GetLiveGhostCount();
			PeakGhosts = FMath::Max(PeakGhosts, Count);
			if (Count != LastGhosts)
				Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"air_ghosts\",\"f\":%lld,\"live\":%d,\"sampling\":%s}"), Ctx.Frame, Count, Ghosts->IsSampling() ? TEXT("true") : TEXT("false")));
			LastGhosts = Count;
		}

		Sample(Ctx);
		SampleStaging(Ctx);
		ActiveSlamEffects = 0;
		for (TObjectIterator<UNiagaraComponent> It; It; ++It)
		{
			if (It->GetWorld() != Ctx.Player->GetWorld() || !SlamSystems.Contains(TObjectKey<UNiagaraSystem>(It->GetAsset())) || !It->IsActive())
			{
				continue;
			}

			++ActiveSlamEffects;
			if (!SeenSlamEffects.Contains(*It))
			{
				SeenSlamEffects.Add(*It);
				bImpactBeforeSlam |= bFullString ? FinalHitFrame < 0 : !bSawSlam;
				Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"air_slam_fx\",\"f\":%lld,\"z\":%.2f}"), Ctx.Frame, It->GetComponentLocation().Z));
			}
		}

		const EVBPlayBotState State = Sequence.Tick(Ctx);
		if (State == EVBPlayBotState::Running)
		{
			return State;
		}

		Ctx.Checks->Expect(TEXT("air_sequence_completed"), State == EVBPlayBotState::Succeeded, true, Ctx.Frame,
			FString::Printf(TEXT("{\"failure\":\"%s\"}"), *Sequence.GetFailure().ReplaceCharWithEscapedChar()));
		Ctx.Checks->Expect(TEXT("victim_launched"), VictimRise > 50.0, true, Ctx.Frame, FString::Printf(TEXT("{\"riseCm\":%.2f}"), VictimRise));
		Ctx.Checks->Expect(TEXT("attacker_chased_airborne"), PlayerRise > 40.0, true, Ctx.Frame, FString::Printf(TEXT("{\"riseCm\":%.2f}"), PlayerRise));
		Ctx.Checks->Expect(TEXT("air_chase_immediate"), ChasePressFrame >= 0 && FlyingFrame >= ChasePressFrame
			&& FlyingFrame - ChasePressFrame <= 1,
			true, Ctx.Frame, FString::Printf(TEXT("{\"press\":%lld,\"flying\":%lld}"), ChasePressFrame, FlyingFrame));
		Ctx.Checks->Expect(TEXT("air_chase_fast_arrival"), ChasePressFrame >= 0 && StringFrame >= ChasePressFrame
			&& StringFrame - ChasePressFrame >= FMath::Max<int64>(1, ExpectedChaseFrames - 2)
			&& StringFrame - ChasePressFrame <= ExpectedChaseFrames + 3,
			true, Ctx.Frame, FString::Printf(TEXT("{\"press\":%lld,\"string\":%lld,\"expectedFrames\":%lld}"), ChasePressFrame, StringFrame, ExpectedChaseFrames));
		if (!bCancelString)
		{
			Ctx.Checks->Expect(TEXT("air_slam_does_not_relaunch"), !bRelaunchedDuringSlam, true, Ctx.Frame);
			Ctx.Checks->Expect(TEXT("air_string_hits"), AirHits >= (ExpectedLethalHit > 0 ? ExpectedLethalHit : (bFullString ? 4 : 2)), true, Ctx.Frame, FString::Printf(TEXT("{\"hits\":%d}"), AirHits));
			if (bFullString && (ExpectedLethalHit == 0 || ExpectedLethalHit == 4))
			{
				Ctx.Checks->Expect(TEXT("final_contact_starts_victim_descent"), FinalHitFrame >= 0
					&& VictimDropFrame >= FinalHitFrame && VictimDropFrame - FinalHitFrame <= 2,
					true, Ctx.Frame, FString::Printf(TEXT("{\"hit\":%lld,\"drop\":%lld}"), FinalHitFrame, VictimDropFrame));
				Ctx.Checks->Expect(TEXT("victim_lands_before_player_falls"), VictimGroundFrame > FinalHitFrame
					&& PlayerFallFrame > VictimGroundFrame,
					true, Ctx.Frame, FString::Printf(TEXT("{\"victimGround\":%lld,\"playerFall\":%lld}"), VictimGroundFrame, PlayerFallFrame));
				Ctx.Checks->Expect(TEXT("air4_followthrough_at_ground_impact"), bAir4AtImpact && !bSawSlam, true, Ctx.Frame);
				Ctx.Checks->Expect(TEXT("rear_camera_at_ground_impact"), bRearAtImpact && bTargetFramedAtImpact, true, Ctx.Frame);
			}
			else if (!bFullString)
			{
				Ctx.Checks->Expect(TEXT("authored_slam_seen"), bSawSlam && bSawSlamEnd, true, Ctx.Frame);
				Ctx.Checks->Expect(TEXT("air_slam_hits"), SlamHits >= 1, true, Ctx.Frame, FString::Printf(TEXT("{\"hits\":%d}"), SlamHits));
			}

			if (ExpectedLethalHit == 0 || ExpectedLethalHit == 4)
			{
				Ctx.Checks->Expect(TEXT("authored_victim_slam_reaction"), bSawSlamReaction, true, Ctx.Frame);
			}
		}

		const UVBTargetLockComponent* Lock = UVBTargetLockComponent::FindFor(Ctx.Player);
		Ctx.Checks->Expect(TEXT("air_camera_framing_seen_and_released"), bSawCameraFraming && Lock
			&& !Lock->HasActiveCameraFraming(), true, Ctx.Frame);
		if (ExpectedLethalHit == 0)
		{
			Ctx.Checks->Expect(TEXT("air_lock_target_preserved"), Lock && Lock->GetLockedTarget() == Target.Get(), true, Ctx.Frame);
		}
		else
		{
			Ctx.Checks->Expect(TEXT("one_airborne_defeat"), VictimDefeats == 1, true, Ctx.Frame);
			const FName ExpectedSection = ExpectedLethalHit == 1 ? FName(TEXT("Default")) : FName(*FString::Printf(TEXT("Air%d"),
				ExpectedLethalHit));
			Ctx.Checks->Expect(TEXT("defeat_on_expected_air_strike"), bVictimAirborneAtDefeat
				&& VictimDefeatSection == ExpectedSection, true, Ctx.Frame,
				FString::Printf(TEXT("{\"section\":\"%s\"}"), *VictimDefeatSection.ToString()));
			Ctx.Checks->Expect(TEXT("defeated_target_is_not_locked"), Lock && Lock->GetLockedTarget() != Target.Get(), true, Ctx.Frame);
			Ctx.Checks->Expect(TEXT("airborne_defeat_skips_ground_animation"), !bStandingDeathInAir, true, Ctx.Frame);
		}

		const bool bExpectedImpact = !bCancelString && (ExpectedLethalHit == 0 || ExpectedLethalHit == 4);
		Ctx.Checks->Expect(TEXT("ground_impact_once"), SeenSlamEffects.Num() == (bExpectedImpact ? 1 : 0) && !bImpactBeforeSlam,
			true, Ctx.Frame,
			FString::Printf(TEXT("{\"count\":%d}"), SeenSlamEffects.Num()));
		FString EffectEvidence = FString::Printf(TEXT("{\"active\":%d}"), ActiveSlamEffects);
		Ctx.Checks->Expect(TEXT("ground_impact_released"), ActiveSlamEffects == 0, true, Ctx.Frame, EffectEvidence);
		Ctx.Checks->Expect(TEXT("air_movement_released"), Ctx.Player->GetCharacterMovement()->MovementMode != MOVE_Flying, true, Ctx.Frame);
		Ctx.Checks->Expect(TEXT("air_expected_end"), bAirEnded
			&& bAirCancelled == (bCancelString || (ExpectedLethalHit > 0 && ExpectedLethalHit < 4)), true, Ctx.Frame);
		Ctx.Checks->Expect(TEXT("air_recovery_released_on_ground"),
			!Ctx.PlayerASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_AirRecovery), true, Ctx.Frame);
		if (bFullString && ExpectedLethalHit == 0)
		{
			Ctx.Checks->Expect(TEXT("pair_height_held_during_string"), HeldSamples >= 12 && HeldVictimMaxZ - HeldVictimMinZ < 1.0
				&& HeldPlayerMaxZ - HeldPlayerMinZ < 8.0, true, Ctx.Frame,
				FString::Printf(TEXT("{\"samples\":%d,\"victimDrift\":%.2f,\"playerDrift\":%.2f}"), HeldSamples, HeldVictimMaxZ - HeldVictimMinZ, HeldPlayerMaxZ - HeldPlayerMinZ));
		}

		Ctx.Checks->Expect(TEXT("air_pose_stays_at_movement_anchor"), MaxRootOffset < 1.0, true, Ctx.Frame,
			FString::Printf(TEXT("{\"maxRootOffsetCm\":%.2f}"), MaxRootOffset));
		if (ExpectedGhostLimit > 0)
		{
			const UVBAirAfterimageComponent* Ghosts = Ctx.Player->FindComponentByClass<UVBAirAfterimageComponent>();
			Ctx.Checks->Expect(TEXT("air_afterimages_sampled"), PeakGhosts > 0 && PeakGhosts <= ExpectedGhostLimit, true, Ctx.Frame,
				FString::Printf(TEXT("{\"peak\":%d,\"limit\":%d}"), PeakGhosts, ExpectedGhostLimit));
			Ctx.Checks->Expect(TEXT("air_afterimages_released"), Ghosts && !Ghosts->IsSampling()
				&& Ghosts->GetLiveGhostCount() == 0, true, Ctx.Frame);
		}

		if (bExpectBodyFade)
		{
			Ctx.Checks->Expect(TEXT("air_body_fade_visible"), MinimumCoverage < 0.8f && MinimumCoverage > 0.0f, true, Ctx.Frame,
				FString::Printf(TEXT("{\"minimumCoverage\":%.3f}"), MinimumCoverage));
			Ctx.Checks->Expect(TEXT("air_body_restored_before_string"), !bFadeInString && FadeMesh.IsValid()
				&& FadeMesh->GetMaterial(0) == OriginalBodyMaterial.Get(), true, Ctx.Frame);
		}

		if (ExpectedWhoosh.IsValid()) Ctx.Checks->Expect(TEXT("air_whoosh_material_played"), PeakWhoosh > 0.0f, true, Ctx.Frame,
			FString::Printf(TEXT("{\"peakAmount\":%.3f}"), PeakWhoosh));
		Ctx.Checks->Expect(TEXT("air_root_motion_released"),
			!Ctx.Player->GetCharacterMovement()->GetRootMotionSource(TEXT("AirStringFollow")).IsValid()
			&& !Ctx.Player->GetCharacterMovement()->GetRootMotionSource(TEXT("AirChaseMove")).IsValid(), true, Ctx.Frame);
		if (LaunchFrame >= 0)
		{
			Ctx.Checks->Look(TEXT("air_combo"), LaunchFrame, Ctx.Frame, TEXT("launch, chase, string and slam"));
		}

		return EVBPlayBotState::Succeeded;
	}

	virtual void Teardown(FVBPlayBotContext& Ctx) override
	{
		Ctx.OnHit = nullptr;
		Ctx.OnPressInjected = nullptr;
		if (Ctx.PlayerASC && AirEndedHandle.IsValid())
		{
			Ctx.PlayerASC->OnAbilityEnded.Remove(AirEndedHandle);
		}

		if (Ctx.PlayerASC && ComboWindowHandle.IsValid())
		{
			if (FGameplayEventMulticastDelegate* Event = Ctx.PlayerASC->GenericGameplayEventCallbacks.Find(VBGameplayTags::Event_Combo_WindowOpen))
			{
				Event->Remove(ComboWindowHandle);
			}
		}

		if (ForceCombo)
		{
			ForceCombo->Set(OriginalForceCombo, ECVF_SetByConsole);
		}

		if (TargetASC.IsValid() && VictimDeadHandle.IsValid())
		{
			TargetASC->RegisterGameplayTagEvent(VBGameplayTags::State_Dead, EGameplayTagEventType::NewOrRemoved).Remove(VictimDeadHandle);
		}
	}

private:
	void SampleStaging(FVBPlayBotContext& Ctx)
	{
		float Coverage = 1.0f, Amount = 0.0f;
		if (FadeMesh.IsValid())
		{
			if (UMaterialInterface* Material = FadeMesh->GetMaterial(0))
			{
				Material->GetScalarParameterValue(FHashedMaterialParameterInfo(UVBBodyFadeConfig::CoverageParameter), Coverage);
			}
		}

		MinimumCoverage = FMath::Min(MinimumCoverage, Coverage);
		if (bSawString && FadeMesh.IsValid())
		{
			bFadeInString |= FadeMesh->GetMaterial(0) != OriginalBodyMaterial.Get();
		}

		const APlayerController* PC = Cast<APlayerController>(Ctx.Player->GetController());
		APlayerCameraManager* Camera = PC ? PC->PlayerCameraManager : nullptr;
		const UVBCombatFeedbackModifier* Feedback = Camera ? Cast<UVBCombatFeedbackModifier>(Camera->FindCameraModifierByClass(UVBCombatFeedbackModifier::StaticClass())) : nullptr;
		const UMaterialInstanceDynamic* Screen = Feedback ? Feedback->GetScreenMaterialInstance() : nullptr;
		if (Screen && ExpectedWhoosh.IsValid() && Screen->GetMaterial() == ExpectedWhoosh->GetMaterial())
		{
			Screen->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ParryAmount")), Amount);
		}

		PeakWhoosh = FMath::Max(PeakWhoosh, Amount);
		if (Coverage < 1.0f || Amount > 0.0f)
			Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"air_staging\",\"f\":%lld,\"coverage\":%.4f,\"whoosh\":%.4f}"), Ctx.Frame, Coverage, Amount));
	}

	// 실제 표시 메시와 재질을 기록해 연출 자산의 적용 대상을 확인한다.
	static void RecordVisualSources(FVBPlayBotContext& Ctx)
	{
		TArray<USkeletalMeshComponent*> Meshes;
		UVBCharacterAppearanceComponent::GatherVisibleMeshes(Ctx.Player, Meshes);
		for (const USkeletalMeshComponent* Mesh : Meshes)
		{
			TArray<FString> Materials;
			for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
			{
				Materials.Add(FString::Printf(TEXT("\"%s\""), *GetPathNameSafe(Mesh->GetMaterial(Slot))));
			}

			Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"air_visual_source\",\"f\":%lld,\"mesh\":\"%s\",\"asset\":\"%s\",\"materials\":[%s]}"),
				Ctx.Frame, *Mesh->GetName(), *GetPathNameSafe(Mesh->GetSkeletalMeshAsset()), *FString::Join(Materials, TEXT(","))));
		}
	}

	// 거절돼야 할 낙하 요청이 실제 대상의 속도를 바꾸지 않는지 확인한다.
	void CheckRejectedSlamRequest(FVBPlayBotContext& Ctx, AActor* Instigator, AActor* PayloadTarget, float Speed,
		const TCHAR* Name, bool bIncludeConfig = true)
	{
		const FVector Before = Target->GetCharacterMovement()->Velocity;
		FGameplayEventData Request;
		Request.Instigator = Instigator;
		Request.Target = PayloadTarget;
		Request.EventMagnitude = Speed;
		if (bIncludeConfig)
		{
			Request.OptionalObject = SlamConfig.Get();
		}

		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Target.Get(), VBGameplayTags::Event_Combat_AirSlam, Request);
		Ctx.Checks->Expect(Name, Target->GetCharacterMovement()->Velocity.Equals(Before), true, Ctx.Frame);
	}

	// 이동 모드나 몽타주 섹션이 바뀔 때만 상태를 기록한다.
	void Sample(FVBPlayBotContext& Ctx)
	{
		if (!Target.IsValid() || LaunchFrame < 0)
		{
			return;
		}

		VictimRise = FMath::Max(VictimRise, Target->GetActorLocation().Z - VictimGroundZ);
		const UCharacterMovementComponent* VictimMove = Target->GetCharacterMovement();
		if (bSawString && !bSawSlam && FinalHitFrame < 0 && VictimMove->GravityScale == 0.0f)
		{
			if (HoldFrame < 0)
			{
				HoldFrame = Ctx.Frame;
			}

			if (Ctx.Frame > HoldFrame + 12)
			{
				++HeldSamples;
				HeldVictimMinZ = FMath::Min(HeldVictimMinZ, Target->GetActorLocation().Z);
				HeldVictimMaxZ = FMath::Max(HeldVictimMaxZ, Target->GetActorLocation().Z);
				HeldPlayerMinZ = FMath::Min(HeldPlayerMinZ, Ctx.Player->GetActorLocation().Z);
				HeldPlayerMaxZ = FMath::Max(HeldPlayerMaxZ, Ctx.Player->GetActorLocation().Z);
			}
		}

		if (TargetASC.IsValid() && TargetASC->HasMatchingGameplayTag(VBGameplayTags::State_Dead)
			&& Target->GetActorLocation().Z > VictimGroundZ + 20.0)
		{
			const UAnimInstance* VictimAnim = Target->GetMesh()->GetAnimInstance();
			bStandingDeathInAir |= VictimAnim && StandingDeathMontages.Contains(VictimAnim->GetCurrentActiveMontage());
		}

		PlayerRise = FMath::Max(PlayerRise, Ctx.Player->GetActorLocation().Z - PlayerGroundZ);
		const UAnimInstance* Anim = Ctx.Player->GetMesh()->GetAnimInstance();
		const UAnimMontage* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
		const FName Section = Montage ? Anim->Montage_GetCurrentSection(Montage) : NAME_None;
		bSawString |= Montage && Montage == StringMontage.Get() && Ctx.PlayerASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Attacking_Air);
		if (Montage == StringMontage.Get() && TargetASC.IsValid() && !TargetASC->HasMatchingGameplayTag(VBGameplayTags::State_Dead))
		{
			LastLivingAirSection = Section;
			bLastLivingVictimAirborne = Target->GetCharacterMovement()->IsFalling();
		}

		if (bSawString && StringFrame < 0)
		{
			StringFrame = Ctx.Frame;
		}

		bSawSlam |= bSawString && (Section == SlamStart || Section == SlamLoop);
		bSawSlamEnd |= bSawString && Section == SlamEnd;
		bRelaunchedDuringSlam |= (bSawSlam
			|| FinalHitFrame >= 0) && Target->GetCharacterMovement()->IsFalling() && Target->GetVelocity().Z > 1.0f;
		SampleFinish(Ctx, Montage, Section);
		if (Montage && Montage == StringMontage.Get() && !bSawSlam && Ctx.Frame % 3 == 0)
		{
			SampleContact(Ctx);
		}

		const int32 PlayerMode = Ctx.Player->GetCharacterMovement()->MovementMode;
		if (PlayerMode == MOVE_Flying && FlyingFrame < 0)
		{
			FlyingFrame = Ctx.Frame;
		}

		const int32 VictimMode = Target->GetCharacterMovement()->MovementMode;
		bool bHeld = false;
		for (const FGameplayAbilitySpec& Spec : Ctx.PlayerASC->GetActivatableAbilities())
		{
			if (Spec.IsActive() && Spec.Ability && Spec.Ability->IsA<UVBGA_AirCombo>())
			{
				bHeld = Spec.InputPressed;
			}
		}

		const FString State = FString::Printf(TEXT("%d/%d/%s/%s/%d"), PlayerMode, VictimMode, *GetNameSafe(Montage),
			*Section.ToString(), bHeld);
		if (State == LastState)
		{
			return;
		}

		LastState = State;
		Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"air_state\",\"f\":%lld,\"playerMode\":%d,\"victimMode\":%d,\"playerZ\":%.2f,\"victimZ\":%.2f,\"montage\":\"%s\",\"section\":\"%s\",\"held\":%s}"),
			Ctx.Frame, PlayerMode, VictimMode, Ctx.Player->GetActorLocation().Z, Target->GetActorLocation().Z, *GetNameSafe(Montage), *Section.ToString(), bHeld ? TEXT("true") : TEXT("false")));
	}

	void SampleFinish(FVBPlayBotContext& Ctx, const UAnimMontage* PlayerMontage, FName Section)
	{
		const UVBTargetLockComponent* Lock = UVBTargetLockComponent::FindFor(Ctx.Player);
		bSawCameraFraming |= Lock && Lock->HasActiveCameraFraming();
		const UAnimInstance* VictimAnim = Target->GetMesh()->GetAnimInstance();
		bSawSlamReaction |= VictimAnim && VictimAnim->GetCurrentActiveMontage() == SlamReaction.Get();
		if (FinalHitFrame < 0)
		{
			return;
		}

		if (VictimDropFrame < 0 && Target->GetVelocity().Z <= -ExpectedSlamSpeed * 0.5f)
		{
			VictimDropFrame = Ctx.Frame;
		}

		if (PlayerFallFrame < 0 && Ctx.Player->GetCharacterMovement()->IsFalling())
		{
			PlayerFallFrame = Ctx.Frame;
		}

		if (VictimGroundFrame >= 0 || !FVBPlayBotContext::IsGrounded(Target.Get()))
		{
			return;
		}

		VictimGroundFrame = Ctx.Frame;
		bAir4AtImpact = PlayerMontage == StringMontage.Get() && Section == FinalStringSection
			&& Ctx.Player->GetCharacterMovement()->MovementMode == MOVE_Flying;
		const APlayerCameraManager* Camera = Ctx.PC->PlayerCameraManager;
		const FVector CameraOffset = Camera->GetCameraLocation() - Ctx.Player->GetActorLocation();
		const double RearDot = FVector::DotProduct(CameraOffset.GetSafeNormal2D(), Ctx.Player->GetActorForwardVector().GetSafeNormal2D());
		bRearAtImpact = RearDot < -0.5;
		int32 Width = 0, Height = 0;
		Ctx.PC->GetViewportSize(Width, Height);
		FVector2D Screen;
		const bool bProjected = Ctx.PC->ProjectWorldLocationToScreen(Target->GetMesh()->GetSocketLocation(TEXT("pelvis")), Screen, true);
		const FVector2D Normalized = Width > 0 && Height > 0 ? Screen / FVector2D(Width, Height) : FVector2D(-1.0, -1.0);
		bTargetFramedAtImpact = bProjected && Normalized.X >= 0.05 && Normalized.X <= 0.95 && Normalized.Y >= 0.05 && Normalized.Y <= 0.95;
		Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"air_final_impact_frame\",\"f\":%lld,\"rearDot\":%.3f,\"screen\":[%.3f,%.3f],\"pitch\":%.2f,\"playerZ\":%.2f,\"victimZ\":%.2f}"),
			Ctx.Frame, RearDot, Normalized.X, Normalized.Y, Camera->GetCameraRotation().Pitch, Ctx.Player->GetActorLocation().Z, Target->GetActorLocation().Z));
	}

	// 손과 실제 피격 몸의 간격 및 현재 충돌 응답을 기록한다.
	void SampleContact(FVBPlayBotContext& Ctx)
	{
		const USkeletalMeshComponent* PlayerMesh = Ctx.Player->GetMesh();
		const USkeletalMeshComponent* TargetMesh = Target->GetMesh();
		if (bProbeHeights)
		{
			SampleCandidateHeights(Ctx);
		}

		const FVector Right = PlayerMesh->GetSocketLocation(TEXT("hand_r"));
		const FVector Left = PlayerMesh->GetSocketLocation(TEXT("hand_l"));
		const FVector Pelvis = TargetMesh->GetSocketLocation(TEXT("pelvis"));
		const FVector Root = TargetMesh->GetSocketLocation(TEXT("root"));
		const FVector PlayerRootLocal = PlayerMesh->GetSocketTransform(TEXT("root"), RTS_Component).GetLocation();
		MaxRootOffset = FMath::Max(MaxRootOffset, PlayerRootLocal.Size2D());
		const FVector PlayerRootWorld = PlayerMesh->GetSocketLocation(TEXT("root"));
		const FVector MeshWorld = PlayerMesh->GetComponentLocation();
		Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"air_player_root\",\"f\":%lld,\"local\":[%.2f,%.2f,%.2f],\"world\":[%.2f,%.2f,%.2f],\"meshWorld\":[%.2f,%.2f,%.2f]}"),
			Ctx.Frame, PlayerRootLocal.X, PlayerRootLocal.Y, PlayerRootLocal.Z, PlayerRootWorld.X, PlayerRootWorld.Y, PlayerRootWorld.Z,
			MeshWorld.X, MeshWorld.Y, MeshWorld.Z));
		FClosestPointOnPhysicsAsset ClosestR, ClosestL;
		const bool bRight = TargetMesh->GetClosestPointOnPhysicsAsset(Right, ClosestR, false);
		const bool bLeft = TargetMesh->GetClosestPointOnPhysicsAsset(Left, ClosestL, false);
		Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"air_contact\",\"f\":%lld,\"right\":[%.2f,%.2f,%.2f],\"left\":[%.2f,%.2f,%.2f],\"pelvis\":[%.2f,%.2f,%.2f],\"root\":[%.2f,%.2f,%.2f],\"closestR\":%.2f,\"closestL\":%.2f,\"validR\":%s,\"validL\":%s,\"collision\":%d,\"channel\":%d,\"response\":%d}"),
			Ctx.Frame, Right.X, Right.Y, Right.Z, Left.X, Left.Y, Left.Z, Pelvis.X, Pelvis.Y, Pelvis.Z, Root.X, Root.Y, Root.Z,
			ClosestR.Distance, ClosestL.Distance, bRight ? TEXT("true") : TEXT("false"), bLeft ? TEXT("true") : TEXT("false"),
			static_cast<int32>(TargetMesh->GetCollisionEnabled()), static_cast<int32>(TraceChannel), static_cast<int32>(TargetMesh->GetCollisionResponseToChannel(TraceChannel))));
	}

	// 실제 판정 모양을 옮겨 조회하며 캐릭터나 피해 상태는 바꾸지 않는다.
	void SampleCandidateHeights(FVBPlayBotContext& Ctx)
	{
		const UVBMeleeTraceComponent* Trace = Ctx.Player->FindComponentByClass<UVBMeleeTraceComponent>();
		if (!Trace)
		{
			return;
		}

		const UAnimInstance* Anim = Ctx.Player->GetMesh()->GetAnimInstance();
		const FName Section = Anim->Montage_GetCurrentSection(StringMontage.Get());
		for (const FGameplayTag Part : ProbeShapes())
		{
			SampleShapeHeights(Ctx, *Trace, Section, Part);
		}
	}

	static const TArray<FGameplayTag>& ProbeShapes()
	{
		static const TArray<FGameplayTag> Shapes = {VBGameplayTags::Melee_Shape_HandR, VBGameplayTags::Melee_Shape_HandL,
			VBGameplayTags::Melee_Shape_FootR, VBGameplayTags::Melee_Shape_FootL};
		return Shapes;
	}

	void SampleShapeHeights(FVBPlayBotContext& Ctx, const UVBMeleeTraceComponent& Trace, FName Section, FGameplayTag Part)
	{
		FVBResolvedMeleeShape Shape;
		if (!Trace.ResolveShape(Part, Shape))
		{
			return;
		}

		const FTransform Bone = Trace.GetMesh()->GetSocketTransform(Shape.Bone);
		const FVector Base = Bone.TransformPosition(Shape.BaseLocal), Tip = Bone.TransformPosition(Shape.TipLocal);
		const FVector Along = Tip - Base;
		const FQuat Rotation = Along.IsNearlyZero() ? FQuat::Identity : FRotationMatrix::MakeFromZ(Along.GetSafeNormal()).ToQuat();
		const FCollisionShape Capsule = FCollisionShape::MakeCapsule(Shape.Radius, Along.Size() * 0.5f + Shape.Radius);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(VBPlayBotAirPose), false, Ctx.Player);
		FString Matches;
		for (int32 Offset = -120; Offset <= 120; Offset += 10)
		{
			TArray<FOverlapResult> Overlaps;
			Ctx.Player->GetWorld()->OverlapMultiByChannel(Overlaps, (Base + Tip) * 0.5f + FVector(0.0f, 0.0f, Offset), Rotation,
				TraceChannel, Capsule, Params);
			if (Overlaps.ContainsByPredicate([this](const FOverlapResult& Hit) { return Hit.GetActor() == Target.Get(); }))
			{
				if (!Matches.IsEmpty())
				{
					Matches += TEXT(",");
				}

				Matches += FString::FromInt(Offset);
			}
		}

		const FVector Center = (Base + Tip) * 0.5f;
		Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"air_height_candidates\",\"f\":%lld,\"section\":\"%s\",\"shape\":\"%s\",\"windowOpen\":%s,\"center\":[%.2f,%.2f,%.2f],\"offsets\":[%s]}"),
			Ctx.Frame, *Section.ToString(), *Part.ToString(), Trace.HasOpenWindows() ? TEXT("true") : TEXT("false"), Center.X, Center.Y, Center.Z, *Matches));
	}

	FVBPlayBotSequence Sequence;
	TWeakObjectPtr<ACharacter> Target;
	TWeakObjectPtr<UAbilitySystemComponent> TargetASC;
	TSet<TWeakObjectPtr<UAnimMontage>> StandingDeathMontages;
	FDelegateHandle VictimDeadHandle;
	int32 ExpectedLethalHit = 0, VictimDefeats = 0, HeldSamples = 0;
	double TargetHealth = 0.0;
	int64 HoldFrame = -1;
	double HeldVictimMinZ = TNumericLimits<double>::Max(), HeldPlayerMinZ = TNumericLimits<double>::Max();
	double HeldVictimMaxZ = TNumericLimits<double>::Lowest(), HeldPlayerMaxZ = TNumericLimits<double>::Lowest();
	bool bStandingDeathInAir = false;
	bool bProbeDodge = false, bRecoveryProbeInAir = false;
	bool bVictimAirborneAtDefeat = false;
	FName VictimDefeatSection;
	FName LastLivingAirSection;
	bool bLastLivingVictimAirborne = false;
	TWeakObjectPtr<UAnimMontage> StringMontage;
	TWeakObjectPtr<const UVBAirComboConfig> SlamConfig;
	TWeakObjectPtr<UAnimMontage> SlamReaction;
	FVBTargetLockFraming ExpectedFraming;
	float ExpectedSlamSpeed = 0.0f;
	FName FinalStringSection;
	int64 FinalHitFrame = -1, VictimDropFrame = -1, VictimGroundFrame = -1, PlayerFallFrame = -1;
	bool bAir4AtImpact = false, bRearAtImpact = false, bTargetFramedAtImpact = false;
	bool bSawSlamReaction = false, bSawCameraFraming = false;
	TSet<TObjectKey<UNiagaraSystem>> SlamSystems;
	TSet<TWeakObjectPtr<UNiagaraComponent>> SeenSlamEffects;
	int32 ActiveSlamEffects = 0;
	bool bImpactBeforeSlam = false;
	IConsoleVariable* ForceCombo = nullptr;
	FDelegateHandle AirEndedHandle;
	FDelegateHandle ComboWindowHandle;
	ECollisionChannel TraceChannel = ECC_MAX;
	int32 OriginalForceCombo = -1;
	int32 LauncherVariant = INDEX_NONE;
	FName SlamStart, SlamLoop, SlamEnd;
	FString LastState;
	double VictimGroundZ = 0.0, PlayerGroundZ = 0.0, VictimRise = 0.0, PlayerRise = 0.0;
	double MaxRootOffset = 0.0;
	int64 LaunchFrame = -1;
	int64 ChasePressFrame = -1, FlyingFrame = -1, StringFrame = -1;
	int64 ExpectedChaseFrames = 0;
	int32 AirHits = 0;
	int32 SlamHits = 0;
	int32 PeakGhosts = 0, LastGhosts = -1, ExpectedGhostLimit = 0;
	int32 StringWindows = 0;
	TWeakObjectPtr<USkeletalMeshComponent> FadeMesh;
	TWeakObjectPtr<UMaterialInterface> OriginalBodyMaterial, ExpectedWhoosh;
	float MinimumCoverage = 1.0f, PeakWhoosh = 0.0f;
	bool bExpectBodyFade = false, bFadeInString = false;
	bool bFullString = false;
	bool bCancelString = false, bProbeHeights = false;
	bool bSawString = false, bSawSlam = false, bSawSlamEnd = false;
	bool bAirEnded = false;
	bool bAirCancelled = false;
	bool bRelaunchedDuringSlam = false;
};

VB_PLAYBOT_SCENARIO(FVBPlayBotScenario_AirCombo, "AirCombo")
