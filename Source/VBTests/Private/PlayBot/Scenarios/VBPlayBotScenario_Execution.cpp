// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/VBCombatFeedbackModifier.h"
#include "Character/VBCharacter.h"
#include "Character/VBFinisherComponent.h"
#include "Character/VBTargetLockComponent.h"
#include "Character/VBWeaponStateComponent.h"
#include "Data/VBFinisherConfig.h"
#include "Data/VBWeaponDefinition.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "PlayBot/VBPlayBotChecks.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "PlayBot/VBPlayBotScenarioBase.h"
#include "PlayBot/VBPlayBotSkills.h"
#include "UI/Indicator/VBExecutePromptComponent.h"
#include "UI/VBEnemyHealthBarWidget.h"
#include "Widgets/SViewport.h"

// 연속 처형(하나는 락온 중)과 처형 마무리(접촉 때 슬로우, 처형 카메라, 0.25초 안 복귀, 화면 큐)를 같은 흐름으로 본다
// 차이는 ini 구역과 검사뿐이고, 일어나지 않은 단계(창 없음, 처형 거절)는 실행 중단이 아니라 FAIL 줄이다
class FVBPlayBotScenario_Execution : public FVBPlayBotScenarioBase
{
	struct FExec
	{
		int64 WindowFrame = -1;
		int64 StartFrame = -1;
		int64 EndFrame = -1;
		int64 FirstHitFrame = -1;
		int64 SlowFrame = -1;
		int64 ReturnFrame = -1;
		int32 CameraFrames = 0;
		int32 CueInCameraFrames = 0;
		int32 ReadinessFrames = 0;
		int32 SceneSamples = 0;
		int32 SceneReadinessFrames = 0;
		bool bLockedAtPress = false;
		int64 SceneFrame = -1;
		int64 ViewReturnFrame = -1;
		TWeakObjectPtr<AActor> BaselineView;
		bool bSceneReleased = false;
	};

public:
	virtual bool Setup(FVBPlayBotContext& Ctx, FString& OutError) override
	{
		const FVBPlayBotRunConfig& C = *Ctx.Config;
		FString TargetClass, Weapon, Slot, AttackText, ExecuteText, LockText;
		if (!C.RequireString(TEXT("TargetClass"), TargetClass, OutError) || !C.RequireString(TEXT("Weapon"), Weapon, OutError)
			|| !C.RequireString(TEXT("WeaponAction"), Slot, OutError) || !C.RequireString(TEXT("AttackAction"), AttackText, OutError)
			|| !C.RequireString(TEXT("ExecuteAction"), ExecuteText, OutError))
		{
			return false;
		}

		LockText = C.GetString(TEXT("LockOnAction"));
		const int32 Count = C.GetInt(TEXT("Count"), 2);
		const double Distance = C.GetDouble(TEXT("TargetDistance"), 350.0);
		const double Lateral = C.GetDouble(TEXT("TargetSpacing"), 300.0);
		const double Range = C.GetDouble(TEXT("AttackRange"), 170.0);
		bFinisherChecks = C.GetInt(TEXT("FinisherChecks"), 0) != 0;
		ReturnFrames = C.GetInt(TEXT("ReturnFrames"), 15);
		TArray<int32> LockFor;
		for (const FString& Value : C.GetArray(TEXT("LockOnFor")))
		{
			LockFor.Add(FCString::Atoi(*Value));
		}

		if (Count <= 0 || (!LockFor.IsEmpty() && LockText.IsEmpty()))
		{
			OutError = FString::Printf(TEXT("[%s] Count must be positive and LockOnFor needs LockOnAction"), *C.Section);
			return false;
		}

		Execs.SetNum(Count);
		bLockSecond = LockFor.Contains(2);
		const FName Attack(*AttackText);
		const FName Execute(*ExecuteText);

		Sequence.Then<FVBSkillEquip>(Weapon, FName(*Slot));
		FixtureConfig = C.GetString(TEXT("FixtureFinisherConfig"));
		if (!FixtureConfig.IsEmpty())
		{
			Sequence.Then<FVBSkillDo>(TEXT("declare single-pair fixture"), [this](FVBPlayBotContext& C2, FString& Error)
			{
				const auto* Weapons = C2.Player->FindComponentByClass<UVBWeaponStateComponent>();
				const UVBWeaponDefinition* Definition = Weapons ? Weapons->GetSelectedDefinition() : nullptr;
				UVBFinisherConfig* Pair = LoadObject<UVBFinisherConfig>(nullptr, *FixtureConfig);
				if (!Definition || !Pair || !Definition->GetSummonData().FinisherConfigs.Contains(Pair))
				{
					Error = TEXT("FixtureFinisherConfig must already belong to the equipped weapon");
					return false;
				}
				FVBWeaponSummonData Candidate = Definition->GetSummonData();
				Candidate.FinisherConfigs = {Pair};
				FVBPlayBotVariant Variant;
				Variant.Name = TEXT("SinglePairFixture");
				Variant.ObjectPath = Definition->GetPathName();
				Variant.Property = TEXT("SummonData");
				FVBWeaponSummonData::StaticStruct()->ExportText(Variant.Value, &Candidate, &Candidate, nullptr, PPF_None, nullptr);
				FString Readback;
				if (!C2.Overrides->Apply(Variant, Readback, Error)) { return false; }
				C2.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"single_pair_fixture\",\"f\":%lld,\"config\":\"%s\",\"natural_selection_proof\":false}"),
					C2.Frame, *FixtureConfig.ReplaceCharWithEscapedChar()));
				return Definition->GetSummonData().FinisherConfigs.Num() == 1
					&& Definition->GetSummonData().FinisherConfigs[0] == Pair;
			});
		}
		Sequence.Then<FVBSkillDo>(TEXT("spawn targets"),
			[this, TargetClass, Count, Distance, Lateral](FVBPlayBotContext& C2, FString& Error)
		{
			const FVector Right = C2.Player->GetActorRightVector().GetSafeNormal2D();
			for (int32 Index = 0; Index < Count; ++Index)
			{
				ACharacter* Spawned = VBPlayBotSkills::SpawnStillTarget(C2, TargetClass, Distance, Error);
				if (!Spawned)
				{
					return false;
				}

				Spawned->SetActorLocation(Spawned->GetActorLocation() + Right * Lateral * (Index - (Count - 1) * 0.5));
				Targets.Add(Spawned);
				C2.Watch(Spawned);
			}

			C2.Target = Targets[0].Get();
			C2.OnHit = [this](const FVBPlayBotHit& Hit)
			{
				if (Hit.bFromPlayer && Execs.IsValidIndex(Current) && Execs[Current].StartFrame >= 0 && Execs[Current].EndFrame < 0
					&& Execs[Current].FirstHitFrame < 0)
				{
					Execs[Current].FirstHitFrame = Hit.Frame;
				}
			};
			return true;
		});
		Sequence.Then<FVBSkillWaitUntil>(TEXT("targets AI stopped and grounded"), [this](FVBPlayBotContext&)
		{
			for (const TWeakObjectPtr<ACharacter>& T : Targets)
			{
				if (!T.IsValid() || !VBPlayBotSkills::StopAI(T.Get()) || !FVBPlayBotContext::IsGrounded(T.Get()))
				{
					return false;
				}
			}

			return !Targets.IsEmpty();
		}, 5.0);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			auto GetTarget = [this, Index](FVBPlayBotContext&) { return Targets.IsValidIndex(Index) ? Targets[Index].Get() : nullptr; };
			Sequence.Then<FVBSkillDo>(FString::Printf(TEXT("target %d"), Index + 1), [this, Index](FVBPlayBotContext& C2, FString&)
			{
				Current = Index;
				C2.Target = Targets[Index].Get();
				return true;
			});
			if (LockFor.Contains(Index + 1))
			{
				Sequence.Then<FVBSkillApproach>(TEXT("close in to lock"), GetTarget, 600.0, 8.0);
				Sequence.Then<FVBSkillPress>(TEXT("lock on"), FName(*LockText));
				Sequence.Then<FVBSkillWaitUntil>(TEXT("locked"), [GetTarget](FVBPlayBotContext& C2) { return LockedTarget(C2) != nullptr; }, 1.5);
			}

			Sequence.Then<FVBSkillAttackUntil>(TEXT("hit until the execution window"), GetTarget, Attack, Range,
				[GetTarget](FVBPlayBotContext& C2, int32) { return HasTag(GetTarget(C2), VBGameplayTags::State_Combat_FinisherWindow); }, 25.0);
			Sequence.Then<FVBSkillDo>(TEXT("window seen"), [this, Index](FVBPlayBotContext& C2, FString&)
			{
				Execs[Index].WindowFrame = C2.Frame;
				Execs[Index].bLockedAtPress = LockedTarget(C2) != nullptr;
				Execs[Index].BaselineView = C2.PC->GetViewTarget();
				return true;
			});
			Sequence.Then<FVBSkillWait>(TEXT("settle execution UI"), 0.2);
			Sequence.Then<FVBSkillDo>(TEXT("check execution UI"), [this, Index](FVBPlayBotContext& C2, FString&)
			{
				CheckExecutionUI(C2, Targets[Index].Get(), Index);
				return true;
			});
			Sequence.Then<FVBSkillPress>(TEXT("execute"), Execute);
			Sequence.Then<FVBSkillWaitUntil>(TEXT("execution starts"), [](FVBPlayBotContext& C2) { return HasTag(C2.Player, VBGameplayTags::State_Combat_Finishing); }, 1.5);
			Sequence.Then<FVBSkillWaitUntil>(TEXT("execution ends"), [](FVBPlayBotContext& C2) { return !HasTag(C2.Player, VBGameplayTags::State_Combat_Finishing); }, 15.0);
			Sequence.Then<FVBSkillWait>(TEXT("watch the return"), 0.6);
		}

		return true;
	}

	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override
	{
		if (!bAssemblyRecorded)
		{
			CheckAssembly(Ctx);
			bAssemblyRecorded = true;
		}

		Sample(Ctx);
		const EVBPlayBotState State = Sequence.Tick(Ctx);
		if (State == EVBPlayBotState::Running)
		{
			return State;
		}

		if (State == EVBPlayBotState::Failed)
		{
			// 일어나지 않은 단계가 곧 발견이다 - 실행을 이어 가며 보고한다
			Ctx.Checks->Expect(TEXT("step_completed"), false, true, Ctx.Frame, FString::Printf(TEXT("{\"why\":\"%s\"}"), *Sequence.GetFailure().ReplaceCharWithEscapedChar()));
		}

		Evaluate(Ctx);
		return EVBPlayBotState::Succeeded;
	}

private:
	static void CheckAssembly(FVBPlayBotContext& Ctx)
	{
		UVBFinisherComponent* Finisher = UVBFinisherComponent::FindFor(Ctx.Player);
		Ctx.Checks->ExpectSCSComponent(Ctx.Player, UVBFinisherComponent::StaticClass(), TEXT("FinisherComponent"), Finisher,
			TEXT("RG.Assembly.Finisher"), Ctx.Frame);
		UClass* SceneClass = FindObject<UClass>(nullptr, TEXT("/Script/ContextualAnimation.ContextualAnimSceneActorComponent"));
		const UActorComponent* Scene = SceneClass ? Ctx.Player->GetComponentByClass(SceneClass) : nullptr;
		Ctx.Checks->ExpectSCSComponent(Ctx.Player, SceneClass, TEXT("FinisherSceneComponent"), Scene,
			TEXT("RG.Assembly.FinisherScene"), Ctx.Frame, true);
	}

	static bool HasTag(const AActor* Actor, const FGameplayTag& Tag)
	{
		const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
		return ASC && ASC->HasMatchingGameplayTag(Tag);
	}

	static AActor* LockedTarget(const FVBPlayBotContext& Ctx)
	{
		const AVBCharacter* Character = Cast<AVBCharacter>(Ctx.Player);
		const UVBTargetLockComponent* Lock = UVBTargetLockComponent::FindFor(Character);
		return Lock ? Lock->GetLockedTarget() : nullptr;
	}

	void Sample(FVBPlayBotContext& Ctx)
	{
		if (!Execs.IsValidIndex(Current))
		{
			return;
		}

		FExec& E = Execs[Current];
		const bool bFinishing = HasTag(Ctx.Player, VBGameplayTags::State_Combat_Finishing);
		const UVBFinisherComponent* Finisher = UVBFinisherComponent::FindFor(Ctx.Player);
		const bool bInScene = Finisher && (Finisher->IsPlaying() || UVBFinisherComponent::IsActorInScene(Ctx.Player));
		const AActor* Target = Targets.IsValidIndex(Current) ? Targets[Current].Get() : nullptr;
		const UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
		if (TargetASC)
		{
			const bool bReadyCue = TargetASC->IsGameplayCueActive(VBGameplayTags::GameplayCue_Combat_FinisherWindow);
			if (UVBFinisherComponent::IsActorInScene(Target))
			{
				++E.SceneSamples;
				E.SceneReadinessFrames += bReadyCue ? 1 : 0;
			}
			else if (E.SceneFrame < 0 && TargetASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_FinisherWindow))
			{
				E.ReadinessFrames += bReadyCue ? 1 : 0;
			}
		}
		if (bFinishing && bInScene && E.SceneFrame < 0)
		{
			E.SceneFrame = Ctx.Frame;
			if (!FixtureConfig.IsEmpty())
			{
				Ctx.Checks->Expect(TEXT("fixture_pair_selected_") + FString::FromInt(Current + 1),
					GetPathNameSafe(Finisher->GetActiveConfig()) == FixtureConfig, true, Ctx.Frame,
					FString::Printf(TEXT("{\"actual\":\"%s\"}"), *GetPathNameSafe(Finisher->GetActiveConfig()).ReplaceCharWithEscapedChar()));
			}
		}

		const float Global = Ctx.World->GetWorldSettings() ? Ctx.World->GetWorldSettings()->TimeDilation : 1.f;
		const float Actor = Ctx.Player ? Ctx.Player->CustomTimeDilation : 1.f;
		const bool bSlow = Global < 0.999f || Actor < 0.999f;
		if (bFinishing && E.StartFrame < 0)
		{
			E.StartFrame = Ctx.Frame;
		}

		if (bFinishing)
		{
			if (bSlow && E.SlowFrame < 0)
			{
				E.SlowFrame = Ctx.Frame;
			}

			APlayerCameraManager* Camera = Ctx.PC ? Ctx.PC->PlayerCameraManager.Get() : nullptr;
			const bool bOtherView = Camera && Camera->GetViewTarget() != Ctx.Player;
			if (bOtherView)
			{
				++E.CameraFrames;
				const UVBCombatFeedbackModifier* Feedback = Cast<UVBCombatFeedbackModifier>(Camera->FindCameraModifierByClass(UVBCombatFeedbackModifier::StaticClass()));
				if (Feedback && Feedback->IsFeedbackActive())
				{
					++E.CueInCameraFrames;
				}
			}
		}

		if (!bFinishing && E.StartFrame >= 0 && E.EndFrame < 0)
		{
			E.EndFrame = Ctx.Frame;
		}

		if (E.EndFrame >= 0 && E.ReturnFrame < 0 && !bSlow)
		{
			E.ReturnFrame = Ctx.Frame;
		}

		if (E.EndFrame >= 0)
		{
			E.bSceneReleased = !bInScene;
			if (!bInScene && E.BaselineView.IsValid() && Ctx.PC->GetViewTarget() == E.BaselineView.Get() && E.ViewReturnFrame < 0)
			{
				E.ViewReturnFrame = Ctx.Frame;
			}
		}
	}

	void Evaluate(FVBPlayBotContext& Ctx)
	{
		FVBPlayBotChecks& Checks = *Ctx.Checks;
		for (int32 Index = 0; Index < Execs.Num(); ++Index)
		{
			const FExec& E = Execs[Index];
			const FString N = FString::FromInt(Index + 1);
			const FString Evidence = FString::Printf(TEXT("{\"window\":%lld,\"start\":%lld,\"end\":%lld,\"firstHit\":%lld,\"slow\":%lld,\"return\":%lld,\"cameraFrames\":%d,\"cueFrames\":%d,\"locked\":%s}"),
				E.WindowFrame, E.StartFrame, E.EndFrame, E.FirstHitFrame, E.SlowFrame, E.ReturnFrame, E.CameraFrames, E.CueInCameraFrames, E.bLockedAtPress ? TEXT("true") : TEXT("false"));
			Checks.Expect(TEXT("execution_started_") + N, E.StartFrame >= 0, true, E.StartFrame >= 0 ? E.StartFrame : Ctx.Frame, Evidence);
			Checks.Expect(TEXT("execution_readiness_cue_") + N,
				E.ReadinessFrames > 0 && E.SceneSamples > 0 && E.SceneReadinessFrames == 0, true, Ctx.Frame,
				FString::Printf(TEXT("{\"readyFrames\":%d,\"sceneSamples\":%d,\"cueDuringScene\":%d}"),
					E.ReadinessFrames, E.SceneSamples, E.SceneReadinessFrames));
			Checks.Expect(TEXT("RG.C9b.FinisherLifecycle_") + N, E.SceneFrame >= 0 && E.EndFrame >= 0 && E.bSceneReleased, true, Ctx.Frame,
				FString::Printf(TEXT("{\"scene\":%lld,\"end\":%lld,\"released\":%s}"), E.SceneFrame, E.EndFrame, E.bSceneReleased ? TEXT("true") : TEXT("false")));
			Checks.Expect(TEXT("RG.C3b.ViewReturns_") + N, E.EndFrame >= 0 && E.ViewReturnFrame >= E.EndFrame
				&& E.ViewReturnFrame - E.EndFrame <= ReturnFrames, true, Ctx.Frame,
				FString::Printf(TEXT("{\"end\":%lld,\"viewReturn\":%lld,\"budgetFrames\":%d}"), E.EndFrame, E.ViewReturnFrame, ReturnFrames));
			if (E.StartFrame < 0)
			{
				continue;
			}
			// 검토 구간을 실제 찍힌 범위로 자른다 (시나리오가 일찍 실패한 경우 포함)
			Checks.Look(TEXT("execution_") + N, FMath::Max<int64>(0, E.StartFrame - 20),
				FMath::Min<int64>(Ctx.Frame, (E.EndFrame >= 0 ? E.EndFrame : E.StartFrame) + 40), TEXT("execution look"));
			if (!bFinisherChecks)
			{
				continue;
			}
			// 처형의 눈에 보이는 접촉을 알리는 신호가 없다 (데미지 이벤트는 처형 끝에 온다)
			// 그래서 접촉 시점 판단은 look 구간을 보는 사람 몫이고, 검사는 처형 중 슬로우가 난다는 것만 증명한다
			Checks.Unavailable(TEXT("slow_on_contact_") + N, E.StartFrame,
				TEXT("no contact signal; judge the slow-down timing in the look window"));
			Checks.Expect(TEXT("slow_during_execution_") + N, E.SlowFrame >= 0, true,
				E.SlowFrame >= 0 ? E.SlowFrame : E.StartFrame, Evidence);
			if (E.SlowFrame >= 0)
			{
				Checks.Look(TEXT("slow_down_") + N, E.SlowFrame - 30, E.SlowFrame + 30, TEXT("does the slow-down start on contact?"));
			}

			Checks.Expect(TEXT("execution_camera_") + N, E.CameraFrames > 0, true, E.StartFrame, Evidence);
			if (E.EndFrame < 0)
			{
				Checks.Unavailable(TEXT("return_within_0_25s_") + N, Ctx.Frame, TEXT("execution did not end"));
			}
			else
			{
				// 0.25초 = 60 Hz 에서 15프레임, 표본 여유 1프레임을 더한다
				Checks.Expect(TEXT("return_within_0_25s_") + N, E.ReturnFrame >= 0
					&& E.ReturnFrame - E.EndFrame <= ReturnFrames + 1, true, E.ReturnFrame, Evidence);
			}

			if (E.CameraFrames == 0)
			{
				Checks.Unavailable(TEXT("screen_cue_in_execution_camera_") + N, E.StartFrame, TEXT("no execution camera frames"));
			}
			else
			{
				Checks.Expect(TEXT("screen_cue_in_execution_camera_") + N, E.CueInCameraFrames > 0, true, E.StartFrame, Evidence);
			}
		}

		if (bLockSecond && Execs.Num() >= 2 && Execs[0].EndFrame >= 0 && Execs[1].StartFrame >= 0)
		{
			Checks.Expect(TEXT("second_execution_under_lock_on"), Execs[1].bLockedAtPress, true, Execs[1].StartFrame,
				FString::Printf(TEXT("{\"gapFrames\":%lld}"), Execs[1].StartFrame - Execs[0].EndFrame));
		}
	}

	// 실제 처형 대상의 위젯 값과 화면 배치를 확인한다.
	void CheckExecutionUI(FVBPlayBotContext& Ctx, ACharacter* Target, int32 Index)
	{
		FVBPlayBotChecks& Checks = *Ctx.Checks;
		const FString Suffix = TEXT("_") + FString::FromInt(Index + 1);
		const TSharedPtr<SViewport> Viewport = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetGameViewportWidget() : nullptr;
		if (!Viewport.IsValid())
		{
			Checks.Expect(TEXT("execution_ui_viewport") + Suffix, false, true, Ctx.Frame);
			return;
		}

		FVector2D ViewSize;
		GEngine->GameViewport->GetViewportSize(ViewSize);
		const int32 ExpectedX = Ctx.Config->GetInt(TEXT("ExpectedViewportX"), 0);
		const int32 ExpectedY = Ctx.Config->GetInt(TEXT("ExpectedViewportY"), 0);
		Checks.Expect(TEXT("execution_ui_resolution") + Suffix,
			(ExpectedX == 0 || FMath::IsNearlyEqual(ViewSize.X, static_cast<double>(ExpectedX), 1.0))
			&& (ExpectedY == 0 || FMath::IsNearlyEqual(ViewSize.Y, static_cast<double>(ExpectedY), 1.0)), true, Ctx.Frame,
			FString::Printf(TEXT("{\"width\":%.0f,\"height\":%.0f}"), ViewSize.X, ViewSize.Y));
		const FSlateRect ViewBounds = Viewport->GetCachedGeometry().GetRenderBoundingRect();
		auto FitsViewport = [&ViewBounds](const UWidget* Widget)
		{
			if (!Widget || !Widget->IsVisible())
			{
				return false;
			}

			const FSlateRect Rect = Widget->GetCachedGeometry().GetRenderBoundingRect();
			return Rect.Right > Rect.Left && Rect.Bottom > Rect.Top && Rect.Left >= ViewBounds.Left - 1.0f
				&& Rect.Top >= ViewBounds.Top - 1.0f && Rect.Right <= ViewBounds.Right + 1.0f && Rect.Bottom <= ViewBounds.Bottom + 1.0f;
		};
		auto Separate = [](const UWidget* A, const UWidget* B)
		{
			if (!A || !B)
			{
				return false;
			}

			const FSlateRect RA = A->GetCachedGeometry().GetRenderBoundingRect();
			const FSlateRect RB = B->GetCachedGeometry().GetRenderBoundingRect();
			return RA.Right <= RB.Left || RB.Right <= RA.Left || RA.Bottom <= RB.Top || RB.Bottom <= RA.Top;
		};
		auto TextFits = [&FitsViewport](const UTextBlock* Text)
		{
			if (!FitsViewport(Text) || Text->GetText().IsEmpty())
			{
				return false;
			}

			const FVector2D Size = Text->GetCachedGeometry().GetLocalSize();
			const FVector2D Desired = Text->GetDesiredSize();
			return Desired.X <= Size.X + 1.0 && Desired.Y <= Size.Y + 1.0;
		};
		auto Describe = [](const UWidget* Widget) -> FString
		{
			if (!Widget)
			{
				return TEXT("null");
			}

			const FSlateRect Rect = Widget->GetCachedGeometry().GetRenderBoundingRect();
			const FVector2D Size = Widget->GetCachedGeometry().GetLocalSize();
			const FVector2D Desired = Widget->GetDesiredSize();
			return FString::Printf(TEXT("{\"visible\":%s,\"rect\":[%.1f,%.1f,%.1f,%.1f],\"size\":[%.1f,%.1f],\"desired\":[%.1f,%.1f]}"),
				Widget->IsVisible() ? TEXT("true") : TEXT("false"), Rect.Left, Rect.Top, Rect.Right, Rect.Bottom,
				Size.X, Size.Y, Desired.X, Desired.Y);
		};
		UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
		TArray<UUserWidget*> Bars;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(Ctx.World, Bars, UVBEnemyHealthBarWidget::StaticClass(), false);
		bool bBarCorrect = false;
		FString BarEvidence = FString::Printf(TEXT("{\"instances\":%d}"), Bars.Num());
		for (UUserWidget* Widget : Bars)
		{
			const UVBEnemyHealthBarWidget* Bar = CastChecked<UVBEnemyHealthBarWidget>(Widget);
			if (!TargetASC || Bar->GetBoundASC() != TargetASC)
			{
				continue;
			}

			const UProgressBar* Health = Cast<UProgressBar>(Bar->GetWidgetFromName(TEXT("HealthBar")));
			const UProgressBar* Posture = Cast<UProgressBar>(Bar->GetWidgetFromName(TEXT("PostureBar")));
			const UTextBlock* Name = Cast<UTextBlock>(Bar->GetWidgetFromName(TEXT("TxtName")));
			const UTextBlock* Number = Cast<UTextBlock>(Bar->GetWidgetFromName(TEXT("HealthText")));
			BarEvidence = FString::Printf(TEXT("{\"bar\":%s,\"health\":%s,\"posture\":%s,\"name\":%s,\"number\":%s,\"percent\":%.4f}"),
				*Describe(Bar), *Describe(Health), *Describe(Posture), *Describe(Name), *Describe(Number), Posture ? Posture->GetPercent() : -1.0f);
			bBarCorrect |= FitsViewport(Bar) && FitsViewport(Health) && FitsViewport(Posture) && TextFits(Name) && TextFits(Number)
				&& FMath::IsNearlyEqual(Posture->GetPercent(), 1.0f, 1.e-4f)
				&& Separate(Name, Health) && Separate(Name, Number) && Separate(Name, Posture)
				&& Separate(Health, Number) && Separate(Health, Posture) && Separate(Number, Posture);
		}

		Checks.Expect(TEXT("execution_posture_ui") + Suffix, bBarCorrect, true, Ctx.Frame, BarEvidence);
		const UVBExecutePromptComponent* Component = Ctx.Player->FindComponentByClass<UVBExecutePromptComponent>();
		Checks.Expect(TEXT("execution_prompt_target") + Suffix, Component && Component->GetPromptTarget() == Target, true, Ctx.Frame);
		UClass* PromptClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/VB/UI/WBP_ExecutePrompt.WBP_ExecutePrompt_C"));
		UUserWidget* Prompt = PromptClass ? Ctx.FindWidget(PromptClass) : nullptr;
		const UTextBlock* Action = Prompt ? Cast<UTextBlock>(Prompt->GetWidgetFromName(TEXT("ActionLabel"))) : nullptr;
		const UTextBlock* Key = Prompt ? Cast<UTextBlock>(Prompt->GetWidgetFromName(TEXT("KeyLabel"))) : nullptr;
		const UImage* Icon = Prompt ? Cast<UImage>(Prompt->GetWidgetFromName(TEXT("ImgKey"))) : nullptr;
		const UWidget* KeyWidget = FitsViewport(Icon) && Icon->GetBrush().GetResourceObject() ? static_cast<const UWidget*>(Icon) : Key;
		Checks.Expect(TEXT("execution_prompt_ui") + Suffix, FitsViewport(Prompt) && TextFits(Action)
			&& Action->GetText().ToString() == TEXT("처형") && FitsViewport(KeyWidget)
			&& (KeyWidget != Key || TextFits(Key)) && Separate(Action, KeyWidget), true, Ctx.Frame,
			FString::Printf(TEXT("{\"prompt\":%s,\"action\":%s,\"key\":%s}"), *Describe(Prompt), *Describe(Action), *Describe(KeyWidget)));
	}

	FVBPlayBotSequence Sequence;
	TArray<TWeakObjectPtr<ACharacter>> Targets;
	TArray<FExec> Execs;
	int32 Current = INDEX_NONE;
	int32 ReturnFrames = 15;
	bool bFinisherChecks = false;
	bool bLockSecond = false;
	bool bAssemblyRecorded = false;
	FString FixtureConfig;
};

VB_PLAYBOT_SCENARIO(FVBPlayBotScenario_Execution, "Execution")
